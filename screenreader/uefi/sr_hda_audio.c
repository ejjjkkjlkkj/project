#include "sr_uefi.h"
#include "sr_hda_audio.h"

#define HDA_MAX_NID 256u
#define HDA_MAX_CONN 64u
#define HDA_INVALID_NID 0xffu
#define HDA_INVALID_RESP 0xffffffffu

#define HDA_WIDGET_AUDIO_OUTPUT 0x0u
#define HDA_WIDGET_AUDIO_INPUT  0x1u
#define HDA_WIDGET_MIXER        0x2u
#define HDA_WIDGET_SELECTOR     0x3u
#define HDA_WIDGET_PIN          0x4u
#define HDA_WIDGET_POWER        0x5u
#define HDA_WIDGET_VOLUME       0x6u
#define HDA_WIDGET_VENDOR       0xfu

#define HDA_DMA_PAGES 2048u
#define HDA_DMA_BYTES (HDA_DMA_PAGES * 4096u)
#define HDA_PCM_OFFSET 0x2000u
#define HDA_MAX_BDL 128u
#define HDA_BDL_CHUNK 0x10000u

typedef sr_efi_status (*hda_stall_fn)(sr_size microseconds);
typedef sr_efi_status (*hda_allocate_pages_fn)(
    sr_u32 type, sr_u32 memory_type, sr_size pages, sr_u64 *memory);

static sr_hda_audio *g_audio;
static hda_stall_fn g_stall;
static hda_allocate_pages_fn g_allocate_pages;

static sr_u8 g_type[HDA_MAX_NID];
static sr_u32 g_widget_cap[HDA_MAX_NID];
static sr_u8 g_conn_count[HDA_MAX_NID];
static sr_u8 g_conn[HDA_MAX_NID][HDA_MAX_CONN];
static sr_u8 g_pin_output[HDA_MAX_NID];
static sr_u8 g_seen[HDA_MAX_NID];
static sr_u8 g_parent[HDA_MAX_NID];
static sr_u8 g_depth[HDA_MAX_NID];
static sr_u8 g_queue[HDA_MAX_NID];
static sr_u8 g_route_index[HDA_MAX_NID];

static inline void hda_outl(sr_u16 port, sr_u32 value) {
    __asm__ volatile("outl %0, %1" :: "a"(value), "d"(port));
}

static inline sr_u32 hda_inl(sr_u16 port) {
    sr_u32 value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "d"(port));
    return value;
}

static inline void hda_fence(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static sr_u32 pci_read32(sr_u32 cfg) {
    hda_outl(0xcf8u, cfg);
    return hda_inl(0xcfcu);
}

static void pci_write32(sr_u32 cfg, sr_u32 value) {
    hda_outl(0xcf8u, cfg);
    hda_outl(0xcfcu, value);
}

static sr_u16 mmio16(sr_u32 off) {
    return *(volatile sr_u16 *)(g_audio->mmio + off);
}

static sr_u32 mmio32(sr_u32 off) {
    return *(volatile sr_u32 *)(g_audio->mmio + off);
}

static void mmio16w(sr_u32 off, sr_u16 value) {
    *(volatile sr_u16 *)(g_audio->mmio + off) = value;
    hda_fence();
}

static void mmio32w(sr_u32 off, sr_u32 value) {
    *(volatile sr_u32 *)(g_audio->mmio + off) = value;
    hda_fence();
}

static sr_u32 immediate(sr_u32 command) {
    sr_u32 timeout = 100000u;
    while (timeout-- && (mmio16(0x68u) & 1u)) {}
    if (!timeout) return HDA_INVALID_RESP;

    mmio16w(0x68u, 2u);
    mmio32w(0x60u, command);
    mmio16w(0x68u, 1u);

    timeout = 100000u;
    while (timeout-- && !(mmio16(0x68u) & 2u)) {}
    if (!timeout) return HDA_INVALID_RESP;

    {
        sr_u32 response = mmio32(0x64u);
        mmio16w(0x68u, 2u);
        return response;
    }
}

static sr_u32 encode_verb12(sr_u8 cad, sr_u8 nid, sr_u16 verb, sr_u8 payload) {
    return ((sr_u32)cad << 28) |
           ((sr_u32)nid << 20) |
           ((sr_u32)verb << 8) |
           payload;
}

static sr_u32 encode_verb4(sr_u8 cad, sr_u8 nid, sr_u8 verb, sr_u16 payload) {
    return ((sr_u32)cad << 28) |
           ((sr_u32)nid << 20) |
           ((sr_u32)(verb & 0x0fu) << 16) |
           payload;
}

static sr_u32 verb12(sr_u8 nid, sr_u16 verb, sr_u8 payload) {
    return immediate(encode_verb12(g_audio->cad, nid, verb, payload));
}

static sr_u32 verb4(sr_u8 nid, sr_u8 verb, sr_u16 payload) {
    return immediate(encode_verb4(g_audio->cad, nid, verb, payload));
}

static sr_u32 get_param(sr_u8 nid, sr_u8 param) {
    return verb12(nid, 0xf00u, param);
}

static sr_u32 get_conn_entry(sr_u8 nid, sr_u8 index) {
    return verb12(nid, 0xf02u, index);
}

static sr_u32 get_conn_select(sr_u8 nid) {
    return verb12(nid, 0xf01u, 0u);
}

static sr_u32 set_conn_select(sr_u8 nid, sr_u8 index) {
    return verb12(nid, 0x701u, index);
}

static void clear_graph(void) {
    sr_u32 i;
    sr_u32 j;
    for (i = 0; i < HDA_MAX_NID; ++i) {
        g_type[i] = HDA_INVALID_NID;
        g_widget_cap[i] = 0;
        g_conn_count[i] = 0;
        g_pin_output[i] = 0;
        g_seen[i] = 0;
        g_parent[i] = HDA_INVALID_NID;
        g_depth[i] = 0;
        g_queue[i] = 0;
        g_route_index[i] = 0;
        for (j = 0; j < HDA_MAX_CONN; ++j)
            g_conn[i][j] = 0;
    }
}

static int add_conn(sr_u8 node, sr_u16 nid) {
    sr_u8 count;
    if (!nid || nid >= HDA_MAX_NID) return 0;
    count = g_conn_count[node];
    if (count >= HDA_MAX_CONN) return 0;
    g_conn[node][count] = (sr_u8)nid;
    g_conn_count[node] = (sr_u8)(count + 1u);
    return 1;
}

static int decode_connections(sr_u8 node) {
    sr_u32 parameter = get_param(node, 0x0eu);
    sr_u8 raw_count;
    int long_form;
    sr_u8 per_response;
    sr_u16 mask;
    sr_u16 range_bit;
    sr_u16 previous = 0;
    int have_previous = 0;
    int previous_was_range = 0;
    sr_u8 base;

    if (parameter == HDA_INVALID_RESP) return 0;
    raw_count = (sr_u8)(parameter & 0x7fu);
    if (!raw_count) return 1;

    long_form = !!(parameter & 0x80u);
    per_response = long_form ? 2u : 4u;
    mask = long_form ? 0x7fffu : 0x007fu;
    range_bit = long_form ? 0x8000u : 0x0080u;

    for (base = 0; base < raw_count; base = (sr_u8)(base + per_response)) {
        sr_u32 response = get_conn_entry(node, base);
        sr_u8 slot;
        if (response == HDA_INVALID_RESP) return 0;

        for (slot = 0; slot < per_response; ++slot) {
            sr_u8 raw_index = (sr_u8)(base + slot);
            sr_u16 value;
            sr_u16 nid;
            int is_range;
            if (raw_index >= raw_count) break;

            value = long_form
                ? (sr_u16)((response >> (slot * 16u)) & 0xffffu)
                : (sr_u16)((response >> (slot * 8u)) & 0xffu);
            nid = (sr_u16)(value & mask);
            is_range = !!(value & range_bit);
            if (!nid) return 0;

            if (is_range) {
                sr_u16 expanded;
                if (!have_previous || previous_was_range || previous >= nid)
                    return 0;
                for (expanded = (sr_u16)(previous + 1u);
                     expanded <= nid;
                     ++expanded) {
                    if (!add_conn(node, expanded)) return 0;
                }
            } else if (!add_conn(node, nid)) {
                return 0;
            }

            previous = nid;
            have_previous = 1;
            previous_was_range = is_range;
        }
    }
    return 1;
}

static int traversable(sr_u8 type) {
    return type == HDA_WIDGET_MIXER ||
           type == HDA_WIDGET_SELECTOR ||
           type == HDA_WIDGET_POWER ||
           type == HDA_WIDGET_VOLUME ||
           type == HDA_WIDGET_VENDOR;
}

static int selectable(sr_u8 type) {
    return type == HDA_WIDGET_AUDIO_INPUT ||
           type == HDA_WIDGET_SELECTOR ||
           type == HDA_WIDGET_PIN ||
           type == HDA_WIDGET_VENDOR;
}

static int find_route(sr_u8 pin, sr_u8 *dac_out, sr_u8 *selectors_out) {
    sr_u32 i;
    sr_u16 qhead = 0;
    sr_u16 qtail = 0;
    sr_u8 found = HDA_INVALID_NID;

    for (i = 0; i < HDA_MAX_NID; ++i) {
        g_seen[i] = 0;
        g_parent[i] = HDA_INVALID_NID;
        g_depth[i] = 0;
        g_route_index[i] = 0;
    }

    g_queue[qtail++] = pin;
    g_seen[pin] = 1;

    while (qhead < qtail) {
        sr_u8 node = g_queue[qhead++];
        sr_u8 count;
        sr_u8 ci;
        if (g_depth[node] >= 16u) continue;

        count = g_conn_count[node];
        for (ci = 0; ci < count; ++ci) {
            sr_u8 upstream = g_conn[node][ci];
            sr_u8 type;
            if (g_seen[upstream]) continue;
            type = g_type[upstream];
            if (type == HDA_INVALID_NID) continue;

            g_seen[upstream] = 1;
            g_parent[upstream] = node;
            g_route_index[upstream] = ci;
            g_depth[upstream] = (sr_u8)(g_depth[node] + 1u);

            if (type == HDA_WIDGET_AUDIO_OUTPUT) {
                found = upstream;
                qhead = qtail;
                break;
            }

            if (traversable(type) && qtail < HDA_MAX_NID)
                g_queue[qtail++] = upstream;
        }
    }

    if (found == HDA_INVALID_NID) return 0;

    {
        sr_u8 selectors = 0;
        sr_u8 cur = found;
        while (cur != pin) {
            sr_u8 child = g_parent[cur];
            if (child == HDA_INVALID_NID) return 0;
            if (g_conn_count[child] > 1u) {
                sr_u8 type = g_type[child];
                if (type == HDA_WIDGET_MIXER) {
                } else if (selectable(type)) {
                    ++selectors;
                } else {
                    return 0;
                }
            }
            cur = child;
        }
        *dac_out = found;
        *selectors_out = selectors;
    }
    return 1;
}

static int apply_route(sr_u8 pin, sr_u8 dac, sr_u8 *applied_out) {
    sr_u8 applied = 0;
    sr_u8 cur = dac;

    while (cur != pin) {
        sr_u8 child = g_parent[cur];
        if (child == HDA_INVALID_NID) return 0;

        if (g_conn_count[child] > 1u) {
            sr_u8 type = g_type[child];
            if (type == HDA_WIDGET_MIXER) {
            } else if (selectable(type)) {
                sr_u8 index = g_route_index[cur];
                sr_u32 readback;
                if (set_conn_select(child, index) == HDA_INVALID_RESP) return 0;
                readback = get_conn_select(child);
                if (readback == HDA_INVALID_RESP ||
                    (sr_u8)readback != index) return 0;
                ++applied;
            } else {
                return 0;
            }
        }
        cur = child;
    }

    *applied_out = applied;
    return 1;
}

static sr_u32 widget_amp_cap(sr_u8 nid, sr_u8 param) {
    if (g_audio->afg == HDA_INVALID_NID) return HDA_INVALID_RESP;
    if (g_widget_cap[nid] & 0x08u) return get_param(nid, param);
    return get_param(g_audio->afg, param);
}

static sr_u8 amp_nominal_gain(sr_u32 cap) {
    sr_u8 offset = (sr_u8)(cap & 0x7fu);
    sr_u8 steps = (sr_u8)((cap >> 8) & 0x7fu);
    return offset <= steps ? offset : steps;
}

static int unmute_output_amp(sr_u8 nid) {
    sr_u32 cap;
    sr_u8 gain;
    sr_u32 left;
    sr_u32 right;

    if (!(g_widget_cap[nid] & 0x04u)) return 1;
    cap = widget_amp_cap(nid, 0x12u);
    if (cap == HDA_INVALID_RESP) return 0;
    gain = amp_nominal_gain(cap);

    if (verb4(nid, 0x3u, (sr_u16)(0xb000u | gain)) == HDA_INVALID_RESP)
        return 0;
    left = verb4(nid, 0xbu, 0xa000u);
    right = verb4(nid, 0xbu, 0x8000u);
    if (left == HDA_INVALID_RESP || right == HDA_INVALID_RESP) return 0;
    if ((left & 0x80u) || (right & 0x80u)) return 0;
    if ((left & 0x7fu) != gain || (right & 0x7fu) != gain) return 0;
    return 1;
}

static int unmute_input_amp(sr_u8 nid, sr_u8 index) {
    sr_u32 cap;
    sr_u8 gain;
    sr_u16 set_payload;
    sr_u32 left;
    sr_u32 right;

    if (!(g_widget_cap[nid] & 0x02u)) return 1;
    if (index > 0x0fu) return 0;
    cap = widget_amp_cap(nid, 0x0du);
    if (cap == HDA_INVALID_RESP) return 0;
    gain = amp_nominal_gain(cap);
    set_payload = (sr_u16)(0x7000u | ((sr_u16)index << 8) | gain);

    if (verb4(nid, 0x3u, set_payload) == HDA_INVALID_RESP) return 0;
    left = verb4(nid, 0xbu, (sr_u16)(0x2000u | index));
    right = verb4(nid, 0xbu, index);
    if (left == HDA_INVALID_RESP || right == HDA_INVALID_RESP) return 0;
    if ((left & 0x80u) || (right & 0x80u)) return 0;
    if ((left & 0x7fu) != gain || (right & 0x7fu) != gain) return 0;
    return 1;
}

static int wait_node_d0(sr_u8 nid) {
    sr_u32 attempt;
    for (attempt = 0; attempt < 100u; ++attempt) {
        sr_u32 state = verb12(nid, 0xf05u, 0u);
        if (state == HDA_INVALID_RESP || (state & 0x00000100u)) return 0;
        if ((state & 0x0fu) == 0u && ((state >> 4) & 0x0fu) == 0u)
            return 1;
        if (g_stall) (void)g_stall(1000u);
    }
    return 0;
}

static int power_up_afg(void) {
    sr_u32 supported;
    if (g_audio->afg == HDA_INVALID_NID) return 0;
    supported = get_param(g_audio->afg, 0x0fu);
    if (supported == HDA_INVALID_RESP || !(supported & 0x01u))
        return g_audio->preferred_controller ? 0 : 1;
    if (verb12(g_audio->afg, 0x705u, 0u) == HDA_INVALID_RESP) return 0;
    return wait_node_d0(g_audio->afg);
}

static int power_up_route_widget(sr_u8 nid) {
    sr_u32 supported;
    if (!(g_widget_cap[nid] & 0x00000400u)) return 1;
    supported = get_param(nid, 0x0fu);
    if (supported == HDA_INVALID_RESP || !(supported & 0x01u)) return 0;
    if (verb12(nid, 0x705u, 0u) == HDA_INVALID_RESP) return 0;
    return wait_node_d0(nid);
}

static int configure_output_path(sr_u8 pin, sr_u8 dac) {
    sr_u8 cur;
    sr_u32 pin_cap;
    sr_u32 pin_ctl;
    sr_u8 desired_pin_ctl;

    if (!power_up_afg()) return 0;

    cur = dac;
    for (;;) {
        if (!power_up_route_widget(cur)) return 0;
        if (cur == pin) break;
        cur = g_parent[cur];
        if (cur == HDA_INVALID_NID) return 0;
    }

    cur = dac;
    for (;;) {
        sr_u8 child;
        if (!unmute_output_amp(cur)) return 0;
        if (cur == pin) break;
        child = g_parent[cur];
        if (child == HDA_INVALID_NID) return 0;
        if (!unmute_input_amp(child, g_route_index[cur])) return 0;
        cur = child;
    }

    pin_cap = get_param(pin, 0x0cu);
    if (pin_cap == HDA_INVALID_RESP) return 0;
    if (pin_cap & 0x00010000u) {
        sr_u32 eapd = verb12(pin, 0xf0cu, 0u);
        sr_u8 desired_eapd;
        if (eapd == HDA_INVALID_RESP) return 0;
        desired_eapd = (sr_u8)eapd | 0x02u;
        if (verb12(pin, 0x70cu, desired_eapd) == HDA_INVALID_RESP) return 0;
        eapd = verb12(pin, 0xf0cu, 0u);
        if (eapd == HDA_INVALID_RESP || !(eapd & 0x02u)) return 0;
    }

    if (verb12(dac, 0x706u, 0x10u) == HDA_INVALID_RESP) return 0;
    {
        sr_u32 stream_channel = verb12(dac, 0xf06u, 0u);
        if (stream_channel == HDA_INVALID_RESP ||
            (stream_channel & 0xffu) != 0x10u) return 0;
    }

    /* 48 kHz, 16-bit, stereo. */
    if (verb4(dac, 0x2u, 0x0011u) == HDA_INVALID_RESP) return 0;
    {
        sr_u32 format = verb4(dac, 0xau, 0u);
        if (format == HDA_INVALID_RESP ||
            (format & 0xffffu) != 0x0011u) return 0;
    }

    pin_ctl = verb12(pin, 0xf07u, 0u);
    if (pin_ctl == HDA_INVALID_RESP) return 0;
    desired_pin_ctl = (sr_u8)pin_ctl | 0x40u;
    if (verb12(pin, 0x707u, desired_pin_ctl) == HDA_INVALID_RESP) return 0;
    pin_ctl = verb12(pin, 0xf07u, 0u);
    if (pin_ctl == HDA_INVALID_RESP || !(pin_ctl & 0x40u)) return 0;
    return 1;
}

static int physical_pin_score(sr_u8 pin, sr_u32 *config_out) {
    sr_u32 config = verb12(pin, 0xf1cu, 0u);
    sr_u8 connectivity;
    sr_u8 device;
    int score = 1;

    if (config_out) *config_out = config;
    if (config == HDA_INVALID_RESP) return 0;

    connectivity = (sr_u8)((config >> 30) & 0x03u);
    device = (sr_u8)((config >> 20) & 0x0fu);
    if (connectivity == 0x01u) return -1;

    if (device == 0x01u) score = 100;
    else if (device == 0x02u) score = 80;
    else if (device == 0x00u) score = 60;
    else if (device == 0x04u || device == 0x05u) score = 40;

    if (connectivity == 0x02u) score += 20;
    else if (connectivity == 0x03u) score += 10;
    return score;
}

static int discover_controller(void) {
    sr_u32 cfg = 0;
    int found = 0;
    sr_u32 pass;

    for (pass = 0; pass < 2u && !found; ++pass) {
        sr_u32 bdf;
        for (bdf = 0; bdf < 0x10000u; ++bdf) {
            sr_u32 base = 0x80000000u | (bdf << 8);
            sr_u32 vd = pci_read32(base);
            sr_u32 classreg;
            if ((vd & 0xffffu) == 0xffffu) continue;
            classreg = pci_read32(base | 0x08u);
            if (((classreg >> 16) & 0xffffu) != 0x0403u) continue;
            if (pass == 0u && vd != 0x15e31022u) continue;
            cfg = base;
            found = 1;
            g_audio->preferred_controller = (sr_u8)(pass == 0u);
            break;
        }
    }
    if (!found) return 0;

    {
        sr_u32 command_status = pci_read32(cfg | 0x04u);
        sr_u32 command = (command_status & 0x0000ffffu) | 0x00000006u;
        pci_write32(cfg | 0x04u, command);
        if ((pci_read32(cfg | 0x04u) & 0x00000006u) != 0x00000006u)
            return 0;
    }

    {
        sr_u32 bar0 = pci_read32(cfg | 0x10u);
        sr_u64 bar;
        if (bar0 & 1u) return 0;
        bar = (sr_u64)(bar0 & 0xfffffff0u);
        if ((bar0 & 0x6u) == 0x4u)
            bar |= ((sr_u64)pci_read32(cfg | 0x14u)) << 32;
        if (!bar) return 0;
        g_audio->mmio = (volatile sr_u8 *)(sr_size)bar;
    }

    if (!mmio16(0x00u) || !*(volatile sr_u8 *)(g_audio->mmio + 0x03u))
        return 0;

    {
        sr_u32 gctl = mmio32(0x08u);
        sr_u32 timeout;
        sr_u16 state;
        sr_u8 cad;

        mmio32w(0x08u, gctl & ~1u);
        timeout = 100000u;
        while (timeout-- && (mmio32(0x08u) & 1u)) {}
        if (!timeout) return 0;
        if (g_stall) (void)g_stall(100u);

        mmio32w(0x08u, mmio32(0x08u) | 1u);
        timeout = 100000u;
        while (timeout-- && !(mmio32(0x08u) & 1u)) {}
        if (!timeout) return 0;
        if (g_stall) (void)g_stall(1000u);

        state = mmio16(0x0eu);
        if (!state) return 0;

        for (cad = 0; cad < 15u; ++cad) {
            sr_u32 codec_id;
            if (!(state & (1u << cad))) continue;
            g_audio->cad = cad;
            codec_id = get_param(0u, 0x00u);
            if (codec_id == HDA_INVALID_RESP ||
                codec_id == 0u ||
                codec_id == 0xffffffffu) continue;

            if (g_audio->preferred_controller &&
                codec_id != 0x10ec0256u) continue;

            g_audio->codec_vendor_id = codec_id;
            return 1;
        }
    }
    return 0;
}

static int discover_live_graph(sr_u8 *pin_out,
                               sr_u8 *dac_out,
                               sr_u8 *selectors_out) {
    sr_u32 root_nodes;
    sr_u8 root_start;
    sr_u8 root_count;
    sr_u8 afg = HDA_INVALID_NID;
    sr_u16 n;
    sr_u32 widget_nodes;
    sr_u8 start;
    sr_u8 count;

    clear_graph();
    root_nodes = get_param(0u, 0x04u);
    if (root_nodes == HDA_INVALID_RESP) return 0;
    root_start = (sr_u8)((root_nodes >> 16) & 0xffu);
    root_count = (sr_u8)(root_nodes & 0xffu);
    if (!root_count) return 0;

    for (n = root_start; n < (sr_u16)root_start + root_count; ++n) {
        sr_u32 type = get_param((sr_u8)n, 0x05u);
        if (type != HDA_INVALID_RESP && (type & 0xffu) == 1u) {
            afg = (sr_u8)n;
            break;
        }
    }
    if (afg == HDA_INVALID_NID) return 0;
    g_audio->afg = afg;

    widget_nodes = get_param(afg, 0x04u);
    if (widget_nodes == HDA_INVALID_RESP) return 0;
    start = (sr_u8)((widget_nodes >> 16) & 0xffu);
    count = (sr_u8)(widget_nodes & 0xffu);
    if (!count) return 0;

    for (n = start; n < (sr_u16)start + count; ++n) {
        sr_u8 nid = (sr_u8)n;
        sr_u32 cap = get_param(nid, 0x09u);
        sr_u8 type;
        if (cap == HDA_INVALID_RESP) return 0;
        g_widget_cap[nid] = cap;
        type = (sr_u8)((cap >> 20) & 0x0fu);
        g_type[nid] = type;
        if (type == HDA_WIDGET_PIN) {
            sr_u32 pin_cap = get_param(nid, 0x0cu);
            if (pin_cap == HDA_INVALID_RESP) return 0;
            if (pin_cap & 0x10u) g_pin_output[nid] = 1;
        }
        if (!decode_connections(nid)) return 0;
    }

    if (!g_audio->preferred_controller) {
        for (n = start; n < (sr_u16)start + count; ++n) {
            sr_u8 pin = (sr_u8)n;
            sr_u8 dac = 0;
            sr_u8 selectors = 0;
            if (!g_pin_output[pin]) continue;
            if (find_route(pin, &dac, &selectors)) {
                *pin_out = pin;
                *dac_out = dac;
                *selectors_out = selectors;
                return 1;
            }
        }
        return 0;
    }

    {
        sr_u8 best_pin = HDA_INVALID_NID;
        int best_score = -1;
        sr_u32 best_config = HDA_INVALID_RESP;

        for (n = start; n < (sr_u16)start + count; ++n) {
            sr_u8 pin = (sr_u8)n;
            sr_u8 candidate_dac = 0;
            sr_u8 candidate_selectors = 0;
            sr_u32 config = HDA_INVALID_RESP;
            int score;
            if (!g_pin_output[pin]) continue;
            if (!find_route(pin, &candidate_dac, &candidate_selectors))
                continue;
            score = physical_pin_score(pin, &config);
            if (score > best_score) {
                best_score = score;
                best_pin = pin;
                best_config = config;
            }
        }

        if (best_pin == HDA_INVALID_NID) return 0;
        if (!find_route(best_pin, dac_out, selectors_out)) return 0;
        *pin_out = best_pin;
        g_audio->internal_speaker =
            (sr_u8)((((best_config >> 30) & 0x03u) == 0x02u) &&
                    (((best_config >> 20) & 0x0fu) == 0x01u));
    }
    return 1;
}

static volatile sr_u8 *stream_descriptor(void) {
    sr_u16 gcap;
    sr_u8 iss;
    if (!g_audio || !g_audio->mmio) return 0;
    gcap = mmio16(0x00u);
    iss = (sr_u8)((gcap >> 8) & 0x0fu);
    return g_audio->mmio + 0x80u + ((sr_u32)iss * 0x20u);
}

void sr_hda_audio_stop(sr_hda_audio *audio) {
    volatile sr_u8 *sd;
    sr_u32 timeout;
    if (!audio || audio != g_audio) return;

    sd = stream_descriptor();
    if (sd) {
        sd[0] = (sr_u8)(sd[0] & ~2u);
        timeout = 100000u;
        while (timeout-- && (sd[0] & 2u)) {}
        sd[3] = 0x1cu;
        hda_fence();
        if (g_stall) (void)g_stall(1000u);
    }

    audio->playing = 0;
    audio->staged_bytes = 0;
    audio->stops++;
}

static int ensure_dma(sr_hda_audio *audio) {
    sr_u64 base;
    if (audio->dma_base) return 1;
    if (!g_allocate_pages) return 0;

    base = 0xffffffffu;
    if (g_allocate_pages(
            1u, /* AllocateMaxAddress */
            4u, /* EfiBootServicesData */
            HDA_DMA_PAGES,
            &base) != 0u ||
        !base ||
        base > 0xffffffffu) {
        return 0;
    }

    audio->dma_base = base;
    return 1;
}

static int stage_silence(sr_hda_audio *audio, sr_u32 stereo_frames) {
    volatile sr_u8 *pcm;
    sr_u32 bytes = stereo_frames * 4u;
    sr_u32 i;
    if (!ensure_dma(audio)) return 0;
    if (audio->staged_bytes > HDA_DMA_BYTES - HDA_PCM_OFFSET ||
        bytes > HDA_DMA_BYTES - HDA_PCM_OFFSET - audio->staged_bytes)
        return 0;

    pcm = (volatile sr_u8 *)(sr_size)(audio->dma_base + HDA_PCM_OFFSET);
    for (i = 0; i < bytes; ++i)
        pcm[audio->staged_bytes + i] = 0;
    audio->staged_bytes += bytes;
    return 1;
}

static int sink_write_pcm(void *ctx,
                          const sr_pcm16 *mono,
                          sr_u32 frames,
                          sr_u32 sample_rate) {
    sr_hda_audio *audio = (sr_hda_audio *)ctx;
    volatile sr_pcm16 *pcm;
    sr_u32 required;
    sr_u32 i;
    sr_u32 sample_index;

    if (!audio || !audio->ready || !mono || !frames ||
        sample_rate != 48000u) return 0;
    if (!ensure_dma(audio)) return 0;

    if (!audio->staged_bytes) {
        /* 20 ms codec/amp lead-in. */
        if (!stage_silence(audio, 960u)) return 0;
    }

    required = frames * 4u;
    if (audio->staged_bytes > HDA_DMA_BYTES - HDA_PCM_OFFSET ||
        required > HDA_DMA_BYTES - HDA_PCM_OFFSET - audio->staged_bytes)
        return 0;

    pcm = (volatile sr_pcm16 *)(sr_size)(audio->dma_base + HDA_PCM_OFFSET);
    sample_index = audio->staged_bytes / 2u;
    for (i = 0; i < frames; ++i) {
        sr_pcm16 s = mono[i];
        pcm[sample_index++] = s;
        pcm[sample_index++] = s;
    }
    audio->staged_bytes += required;
    return 1;
}

static void sink_flush(void *ctx) {
    sr_hda_audio *audio = (sr_hda_audio *)ctx;
    volatile sr_u8 *bdl;
    volatile sr_u8 *pcm;
    volatile sr_u8 *sd;
    sr_u32 payload;
    sr_u32 described = 0;
    sr_u32 entries = 0;
    sr_u32 timeout;

    if (!audio || !audio->ready || !audio->staged_bytes || !audio->dma_base)
        return;

    /* 35 ms tail prevents final-phoneme clipping on physical codecs. */
    if (!stage_silence(audio, 1680u)) {
        audio->underruns++;
        return;
    }

    payload = (audio->staged_bytes + 127u) & ~127u;
    if (payload < audio->staged_bytes ||
        payload > HDA_DMA_BYTES - HDA_PCM_OFFSET) {
        audio->underruns++;
        return;
    }

    pcm = (volatile sr_u8 *)(sr_size)(audio->dma_base + HDA_PCM_OFFSET);
    while (audio->staged_bytes < payload)
        pcm[audio->staged_bytes++] = 0;

    bdl = (volatile sr_u8 *)(sr_size)audio->dma_base;
    while (described < payload) {
        sr_u32 len;
        volatile sr_u8 *entry;
        if (entries >= HDA_MAX_BDL) {
            audio->underruns++;
            return;
        }
        len = payload - described;
        if (len > HDA_BDL_CHUNK) len = HDA_BDL_CHUNK;
        entry = bdl + entries * 16u;
        *(volatile sr_u64 *)(entry + 0x00u) =
            audio->dma_base + HDA_PCM_OFFSET + described;
        *(volatile sr_u32 *)(entry + 0x08u) = len;
        *(volatile sr_u32 *)(entry + 0x0cu) = 0u;
        described += len;
        ++entries;
    }

    if (!entries) return;
    *(volatile sr_u32 *)(bdl + (entries - 1u) * 16u + 0x0cu) = 1u;
    hda_fence();

    sd = stream_descriptor();
    if (!sd) return;

    sd[0] = (sr_u8)(sd[0] & ~2u);
    timeout = 100000u;
    while (timeout-- && (sd[0] & 2u)) {}
    if (!timeout) {
        audio->underruns++;
        return;
    }

    if (!audio->stream_initialized) {
        sd[0] = (sr_u8)(sd[0] | 1u);
        timeout = 100000u;
        while (timeout-- && !(sd[0] & 1u)) {}
        if (!timeout) return;
        sd[0] = (sr_u8)(sd[0] & ~1u);
        timeout = 100000u;
        while (timeout-- && (sd[0] & 1u)) {}
        if (!timeout) return;
        audio->stream_initialized = 1u;
    }

    sd[3] = 0x1cu;
    if (g_stall) (void)g_stall(1000u);

    *(volatile sr_u32 *)(sd + 0x08u) = payload;
    *(volatile sr_u16 *)(sd + 0x0cu) = (sr_u16)(entries - 1u);
    *(volatile sr_u16 *)(sd + 0x12u) = 0x0011u;
    *(volatile sr_u32 *)(sd + 0x18u) = (sr_u32)audio->dma_base;
    *(volatile sr_u32 *)(sd + 0x1cu) = (sr_u32)(audio->dma_base >> 32);
    hda_fence();

    sd[3] = 0x1cu;
    sd[2] = 0x10u; /* stream tag 1 */
    sd[0] = (sr_u8)(sd[0] | 2u);
    hda_fence();

    audio->playing = 1u;
    audio->starts++;
}

static void sink_stop(void *ctx) {
    sr_hda_audio_stop((sr_hda_audio *)ctx);
}

int sr_hda_audio_init(sr_hda_audio *audio, void *system_table) {
    void *bs;
    sr_u8 pin = 0;
    sr_u8 dac = 0;
    sr_u8 selectors = 0;
    sr_u8 applied = 0;

    if (!audio || !system_table) return 0;

    audio->system_table = system_table;
    audio->mmio = 0;
    audio->dma_base = 0;
    audio->staged_bytes = 0;
    audio->codec_vendor_id = 0;
    audio->underruns = 0;
    audio->starts = 0;
    audio->stops = 0;
    audio->cad = 0;
    audio->afg = HDA_INVALID_NID;
    audio->pin = 0;
    audio->dac = 0;
    audio->preferred_controller = 0;
    audio->internal_speaker = 0;
    audio->stream_initialized = 0;
    audio->ready = 0;
    audio->playing = 0;

    g_audio = audio;
    bs = sr_uefi_boot_services(system_table);
    if (!bs) return 0;

    g_allocate_pages =
        *(hda_allocate_pages_fn *)((sr_u8 *)bs + 0x28u);
    g_stall =
        *(hda_stall_fn *)((sr_u8 *)bs + 0xf8u);
    if (!g_allocate_pages || !g_stall) return 0;

    if (!discover_controller()) return 0;
    if (!discover_live_graph(&pin, &dac, &selectors)) return 0;
    if (!apply_route(pin, dac, &applied) || applied != selectors) return 0;
    if (!configure_output_path(pin, dac)) return 0;

    audio->pin = pin;
    audio->dac = dac;
    audio->ready = 1u;
    return 1;
}

sr_audio_sink sr_hda_audio_as_sink(sr_hda_audio *audio) {
    sr_audio_sink sink;
    sink.ctx = audio;
    sink.write_pcm = sink_write_pcm;
    sink.flush = sink_flush;
    sink.stop = sink_stop;
    return sink;
}
