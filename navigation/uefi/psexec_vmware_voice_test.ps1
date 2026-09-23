param(
  [Parameter(Mandatory=$true)][string]$Workspace,
  [Parameter(Mandatory=$true)][string]$RunId
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-RequiredFile([string[]]$Candidates, [string]$Label) {
  foreach ($candidate in $Candidates) {
    if ($candidate -and (Test-Path -LiteralPath $candidate)) {
      return (Resolve-Path -LiteralPath $candidate).Path
    }
  }
  throw "$Label not found. Checked: $($Candidates -join '; ')"
}

function Invoke-Checked([string]$Exe, [string[]]$Arguments) {
  & $Exe @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$Exe failed with exit code $LASTEXITCODE"
  }
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent().Name
if ($identity -ne 'NT AUTHORITY\SYSTEM') {
  throw "This stage must run through PsExec as SYSTEM. Current identity: $identity"
}

$Workspace = (Resolve-Path -LiteralPath $Workspace).Path
Set-Location -LiteralPath $Workspace
$buildDir = Join-Path $Workspace 'build'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$python = Resolve-RequiredFile @(
  'C:\Python316\python.exe',
  'C:\Python315\python.exe',
  'C:\Python314\python.exe',
  'C:\Python313\python.exe',
  'C:\Python312\python.exe',
  (Get-Command python.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1)
) 'Python'

$clang = Resolve-RequiredFile @(
  'C:\Program Files\LLVM\bin\clang.exe',
  (Get-Command clang.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1)
) 'clang'

$lld = Resolve-RequiredFile @(
  'C:\Program Files\LLVM\bin\lld-link.exe',
  (Get-Command lld-link.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1)
) 'lld-link'

$vmrun = Resolve-RequiredFile @(
  "$env:ProgramFiles\VMware\VMware Workstation\vmrun.exe",
  "${env:ProgramFiles(x86)}\VMware\VMware Workstation\vmrun.exe"
) 'VMware vmrun'

$cs = Get-CimInstance Win32_ComputerSystem
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$bios = Get-CimInstance Win32_BIOS
$os = Get-CimInstance Win32_OperatingSystem
if ($cs.Model -notmatch 'M1603QA') {
  throw "Wrong physical target: expected ASUS M1603QA, got '$($cs.Model)'"
}
if ($cpu.Name -notmatch 'Ryzen 7 5800H') {
  throw "Wrong CPU target: expected Ryzen 7 5800H, got '$($cpu.Name)'"
}
@(
  "HOST_MANUFACTURER=$($cs.Manufacturer)",
  "HOST_MODEL=$($cs.Model)",
  "HOST_CPU=$($cpu.Name)",
  "HOST_CORES=$($cpu.NumberOfCores)",
  "HOST_LOGICAL_PROCESSORS=$($cpu.NumberOfLogicalProcessors)",
  "HOST_RAM_BYTES=$($cs.TotalPhysicalMemory)",
  "HOST_BIOS_VERSION=$($bios.SMBIOSBIOSVersion)",
  "HOST_OS=$($os.Caption)",
  "HOST_OS_VERSION=$($os.Version)",
  "HOST_OS_BUILD=$($os.BuildNumber)"
) | Set-Content -LiteralPath (Join-Path $buildDir 'host-evidence.txt') -Encoding utf8

$voiceDir = Join-Path $buildDir 'system-voice'
if (Test-Path $voiceDir) { Remove-Item -Recurse -Force $voiceDir }
New-Item -ItemType Directory -Force -Path $voiceDir | Out-Null

Write-Host "PSEXEC_SYSTEM_IDENTITY=PASS"
Write-Host "HOST_ASUS_M1603QA=PASS"
Write-Host "HOST_RYZEN_5800H=PASS"
Write-Host "PSEXEC_IDENTITY=$identity"
Write-Host "PYTHON=$python"
Write-Host "CLANG=$clang"
Write-Host "LLDLINK=$lld"
Write-Host "VMRUN=$vmrun"

Invoke-Checked 'powershell.exe' @(
  '-NoProfile','-ExecutionPolicy','Bypass',
  '-File',(Join-Path $Workspace 'navigation\uefi\generate_system_voice_assets.ps1'),
  '-OutDir',$voiceDir
)

$voiceEvidence = Join-Path $voiceDir 'system-voice-evidence.txt'
if (-not (Test-Path $voiceEvidence)) {
  throw "system-voice-evidence.txt was not generated"
}
$voiceText = Get-Content -Raw -LiteralPath $voiceEvidence
if ($voiceText -notmatch 'SYSTEM_SPEECH_FR_NATIVE=True') {
  throw "No native French System.Speech voice is visible under LocalSystem"
}
if ($voiceText -notmatch 'SYSTEM_SPEECH_OUTPUT_RATE=16000' -or
    $voiceText -notmatch 'SYSTEM_SPEECH_OUTPUT_BITS=16' -or
    $voiceText -notmatch 'SYSTEM_SPEECH_OUTPUT_CHANNELS=1' -or
    $voiceText -notmatch 'SYSTEM_SPEECH_OUTPUT_ENCODING=PCM') {
  throw "Windows voice source format contract failed"
}
Write-Host "SYSTEM_SPEECH_NATIVE_FR=PASS"
Get-Content -LiteralPath $voiceEvidence

$qualitySource = Join-Path $voiceDir 'quality_reference.wav'
if (-not (Test-Path $qualitySource)) {
  throw "quality_reference.wav was not generated"
}
$qualityRoundtrip = Join-Path $buildDir 'quality-reference-firmware-roundtrip.wav'
$qualityReport = Join-Path $buildDir 'quality-reference-firmware-roundtrip.json'
Invoke-Checked $python @(
  (Join-Path $Workspace 'navigation\uefi\roundtrip_voice_quality.py'),
  $qualitySource,
  $qualityRoundtrip,
  $qualityReport
)

Add-Type -AssemblyName System
foreach ($item in @(
  @{ Name='SOURCE_WINDOWS_TTS'; Path=$qualitySource },
  @{ Name='FIRMWARE_CODEC_ROUNDTRIP'; Path=$qualityRoundtrip }
)) {
  Write-Host "AUDIBLE_REFERENCE=$($item.Name)"
  $player = New-Object System.Media.SoundPlayer $item.Path
  $player.Load()
  $player.PlaySync()
  Start-Sleep -Milliseconds 400
}

$env:QEV_EXTERNAL_VOICE_DIR = $voiceDir
$unitsC = Join-Path $buildDir 'navigation_units.c'
$unitsMeta = Join-Path $buildDir 'navigation_units.txt'
Invoke-Checked $python @(
  (Join-Path $Workspace 'navigation\uefi\generate_units.py'),
  $unitsC,
  $unitsMeta
)

$flags = @(
  '--target=x86_64-pc-windows-msvc',
  '-DQEV_INTERACTIVE_NAV=1',
  '-ffreestanding',
  '-fshort-wchar',
  '-fno-stack-protector',
  '-fno-builtin',
  '-mno-red-zone',
  '-nostdlib',
  '-O2',
  '-Wall',
  '-Wextra',
  '-Werror'
)

Invoke-Checked $clang ($flags + @(
  '-c',(Join-Path $Workspace 'navigation\uefi\semantic_core.c'),
  '-o',(Join-Path $buildDir 'semantic_core.obj')
))
Invoke-Checked $clang ($flags + @(
  '-c',(Join-Path $Workspace 'navigation\uefi\hii_graph_prompt_speech_uefi.c'),
  '-o',(Join-Path $buildDir 'navigation.obj')
))
Invoke-Checked $clang ($flags + @(
  '-c',$unitsC,
  '-o',(Join-Path $buildDir 'navigation_units.obj')
))

$efi = Join-Path $buildDir 'NAVIGATION.EFI'
Invoke-Checked $lld @(
  '/subsystem:efi_application',
  '/entry:efi_main',
  '/nodefaultlib',
  '/machine:x64',
  '/timestamp:0',
  "/out:$efi",
  (Join-Path $buildDir 'navigation.obj'),
  (Join-Path $buildDir 'navigation_units.obj'),
  (Join-Path $buildDir 'semantic_core.obj')
)
if (-not (Test-Path $efi) -or (Get-Item $efi).Length -lt 65536) {
  throw "NAVIGATION.EFI missing or unexpectedly small"
}
Write-Host "NAVIGATION_EFI_REAL_VOICE_BUILD=PASS"

$vmDir = Join-Path $buildDir ("vmware-smoke-" + $RunId)
if (Test-Path $vmDir) { Remove-Item -Recurse -Force $vmDir }
New-Item -ItemType Directory -Force -Path $vmDir | Out-Null

$img = Join-Path $vmDir 'uefi-floppy.img'
Invoke-Checked $python @(
  (Join-Path $Workspace 'navigation\uefi\create_fat12_boot_image.py'),
  $efi,
  $img
)

$vmx = Join-Path $vmDir 'qevarynx-uefi.vmx'
@'
.encoding = "windows-1252"
config.version = "8"
virtualHW.version = "20"
displayName = "QEVARYNOX UEFI PsExec Voice CI"
guestOS = "other-64"
firmware = "efi"
uefi.secureBoot.enabled = "FALSE"
memsize = "512"
numvcpus = "1"
cpuid.coresPerSocket = "1"
floppy0.present = "TRUE"
floppy0.fileType = "file"
floppy0.fileName = "uefi-floppy.img"
floppy0.startConnected = "TRUE"
bios.bootOrder = "floppy"
serial0.present = "TRUE"
serial0.fileType = "file"
serial0.fileName = "serial.log"
serial0.tryNoRxLoss = "FALSE"
sound.present = "TRUE"
sound.virtualDev = "hdaudio"
sound.autodetect = "TRUE"
ethernet0.present = "FALSE"
usb.present = "FALSE"
tools.syncTime = "FALSE"
'@ | Set-Content -LiteralPath $vmx -Encoding ascii

$serial = Join-Path $vmDir 'serial.log'
try {
  Invoke-Checked $vmrun @('-T','ws','start',$vmx,'nogui')

  $deadline = (Get-Date).AddSeconds(75)
  while ((Get-Date) -lt $deadline) {
    if (Test-Path $serial) {
      $serialText = Get-Content -Raw -LiteralPath $serial -ErrorAction SilentlyContinue
      if ($serialText -match 'HII_GRAPH_NAV_DISCOVERY_PROMPT=PASS' -or $serialText -match 'STATUS=BLOCKED') {
        break
      }
    }
    Start-Sleep -Milliseconds 500
  }

  if (-not (Test-Path $serial)) {
    throw "VMware serial log missing; EFI did not reach COM output"
  }
  $serialText = Get-Content -Raw -LiteralPath $serial
  $serialFinal = Join-Path $vmDir 'serial-final.txt'
  $serialText | Set-Content -LiteralPath $serialFinal -Encoding utf8

  if ($serialText -notmatch 'QEVARYNOX-UEFI-HII-GRAPH-PROMPT-SPEECH-V1') {
    throw "VMware did not execute NAVIGATION.EFI"
  }
  if ($serialText -notmatch 'STATE=START') {
    throw "EFI entry marker missing"
  }
  if ($serialText -notmatch 'HII_GRAPH_NAV_DISCOVERY_PROMPT=PASS') {
    $reason = [regex]::Match($serialText, 'REASON=([^\r\n]+)').Groups[1].Value
    if (-not $reason) { $reason = 'DISCOVERY_PROMPT_NOT_REACHED' }
    throw "UEFI speech discovery failed: $reason"
  }

  Write-Host "VMWARE_UEFI_BOOT=PASS"
  Write-Host "VMWARE_SCREENREADER_DISCOVERY=PASS"
  if ($serialText -match 'HDA_CONTROLLER_CODEC=PASS') {
    Write-Host "VMWARE_HDA_DISCOVERY=PASS"
  } else {
    Write-Host "VMWARE_HDA_DISCOVERY=NOT_ESTABLISHED"
  }
}
finally {
  & $vmrun -T ws stop $vmx hard 2>$null
}

$evidenceFile = Join-Path $voiceDir 'system-voice-evidence.txt'
$summaryFile = Join-Path $buildDir 'psexec-physical-summary.txt'
$summary = @(
  'PSEXEC_REQUIRED=PASS',
  "IDENTITY=$identity",
  'REAL_WINDOWS_VOICE_BUILD=PASS',
  'VOICE_CODEC_ROUNDTRIP=PASS',
  'AUDIBLE_AB_REFERENCE=PLAYED',
  'NAVIGATION_EFI_REAL_VOICE_BUILD=PASS',
  'VMWARE_UEFI_BOOT=PASS',
  'VMWARE_SCREENREADER_DISCOVERY=PASS'
)
if (Test-Path $evidenceFile) {
  $summary += Get-Content -LiteralPath $evidenceFile
}
$summary | Set-Content -LiteralPath $summaryFile -Encoding utf8
Write-Host "PSEXEC_PHYSICAL_PIPELINE=PASS"
