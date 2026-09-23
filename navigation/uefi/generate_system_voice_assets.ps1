param(
  [Parameter(Mandatory=$true)][string]$OutDir
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$manifestPath = Join-Path $root 'system_voice_manifest.py'
if (-not (Test-Path $manifestPath)) { throw "Missing manifest: $manifestPath" }

$py = @'
import importlib.util, json, sys
p=sys.argv[1]
spec=importlib.util.spec_from_file_location("m",p)
m=importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
print(json.dumps({
 "letters":m.LETTER_TEXT,
 "digits":m.DIGIT_TEXT,
 "words":list(m.PRIORITY_WORDS),
 "phrases":list(m.PHRASE_SPOKEN),
}, ensure_ascii=False))
'@
$manifestJson = $py | python - $manifestPath
if ($LASTEXITCODE) { throw "Manifest extraction failed." }
$manifest = $manifestJson | ConvertFrom-Json

$probe = New-Object System.Speech.Synthesis.SpeechSynthesizer
$voices = @($probe.GetInstalledVoices() | Where-Object { $_.Enabled })
if (-not $voices) { throw "No enabled System.Speech voice is available under this account." }

$fr = $voices | Where-Object { $_.VoiceInfo.Culture.Name -like 'fr-*' } | Select-Object -First 1
$en = $voices | Where-Object { $_.VoiceInfo.Culture.Name -like 'en-*' } | Select-Object -First 1
if (-not $fr) { $fr = $voices | Select-Object -First 1 }
if (-not $en) { $en = $fr }

$voiceEvidence = @(
  "SYSTEM_SPEECH_VOICE_COUNT=$($voices.Count)"
  "SYSTEM_SPEECH_FR=$($fr.VoiceInfo.Name)"
  "SYSTEM_SPEECH_FR_CULTURE=$($fr.VoiceInfo.Culture.Name)"
  "SYSTEM_SPEECH_EN=$($en.VoiceInfo.Name)"
  "SYSTEM_SPEECH_EN_CULTURE=$($en.VoiceInfo.Culture.Name)"
)
$voiceEvidence | Set-Content -Path (Join-Path $OutDir 'system-voice-evidence.txt') -Encoding utf8
$probe.Dispose()

function Write-VoiceWav([string]$FileName, [string]$Text, [string]$VoiceName, [int]$Rate) {
  $path = Join-Path $OutDir $FileName
  $synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
  try {
    $synth.SelectVoice($VoiceName)
    $synth.Rate = $Rate
    $synth.Volume = 100
    $synth.SetOutputToWaveFile($path)
    $synth.Speak($Text)
  }
  finally {
    $synth.Dispose()
  }
  if (-not (Test-Path $path) -or (Get-Item $path).Length -lt 128) {
    throw "Speech asset generation failed: $FileName"
  }
}

foreach ($p in $manifest.letters.PSObject.Properties) {
  Write-VoiceWav ("letter_{0}.wav" -f $p.Name) ([string]$p.Value) $fr.VoiceInfo.Name 1
}
foreach ($p in $manifest.digits.PSObject.Properties) {
  Write-VoiceWav ("digit_{0}.wav" -f $p.Name) ([string]$p.Value) $fr.VoiceInfo.Name 1
}
foreach ($w in $manifest.words) {
  Write-VoiceWav ("word_{0}.wav" -f $w) ([string]$w) $en.VoiceInfo.Name 1
}
for ($i = 0; $i -lt $manifest.phrases.Count; $i++) {
  Write-VoiceWav ("phrase_{0}.wav" -f $i) ([string]$manifest.phrases[$i]) $fr.VoiceInfo.Name 0
}

$generated = @(Get-ChildItem -Path $OutDir -Filter '*.wav')
if ($generated.Count -lt 70) {
  throw "Too few real-voice assets generated: $($generated.Count)"
}
"REAL_VOICE_ASSET_COUNT=$($generated.Count)" | Add-Content -Path (Join-Path $OutDir 'system-voice-evidence.txt') -Encoding utf8
Write-Host "SYSTEM_SPEECH_REAL_VOICE=PASS"
Write-Host "SYSTEM_SPEECH_ASSET_COUNT=$($generated.Count)"
Write-Host "SYSTEM_SPEECH_FR_VOICE=$($fr.VoiceInfo.Name)"
Write-Host "SYSTEM_SPEECH_EN_VOICE=$($en.VoiceInfo.Name)"
