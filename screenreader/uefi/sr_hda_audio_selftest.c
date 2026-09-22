#include <stdio.h>
#include "sr_hda_audio.h"

int main(void) {
    if (!sr_hda_audio_static_selftest()) {
        fprintf(stderr, "FAIL: HDA static route self-test\n");
        return 1;
    }
    printf("UEFI_HDA_STATIC=PASS\n");
    return 0;
}
