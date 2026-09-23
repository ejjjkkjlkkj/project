param(
  [Parameter(Mandatory=$true)][string]$OutDir
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$letters = [ordered]@{
  a='a'; b='bé'; c='cé'; d='dé'; e='e'; f='èf'; g='gé'; h='ache'; i='i'; j='ji';
  k='ka'; l='elle'; m='ème'; n='ène'; o='o'; p='pé'; q='ku'; r='ère'; s='esse';
  t='té'; u='u'; v='vé'; w='double vé'; x='ixe'; y='i grec'; z='zède'
}
$digits = [ordered]@{
  '0'='zéro'; '1'='un'; '2'='deux'; '3'='trois'; '4'='quatre'; '5'='cinq';
  '6'='six'; '7'='sept'; '8'='huit'; '9'='neuf'
}
$words = @(
  'advanced','action','asus','back','bios','boot','button','change','checked',
  'configuration','cpu','default','device','disabled','enabled','enter','escape',
  'exit','help','left','main','memory','network','nvme','option','password',
  'processor','recovery','restore','right','save','secure','security','settings',
  'setup','storage','system','tpm','up','down','usb','value'
)
$phrases = @(
  "Prêt. F un, aide.",
  "Flèches pour naviguer.",
  "Entrée active. Échap retour.",
  "Aucun changement.",
  "Modifications annulées.",
  "Coché.",
  "Non coché.",
  "Protégé."
)

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

foreach ($key in $letters.Keys) {
  Write-VoiceWav ("letter_{0}.wav" -f $key) ([string]$letters[$key]) $fr.VoiceInfo.Name 1
}
foreach ($key in $digits.Keys) {
  Write-VoiceWav ("digit_{0}.wav" -f $key) ([string]$digits[$key]) $fr.VoiceInfo.Name 1
}
foreach ($w in $words) {
  Write-VoiceWav ("word_{0}.wav" -f $w) ([string]$w) $en.VoiceInfo.Name 1
}
for ($i = 0; $i -lt $phrases.Count; $i++) {
  Write-VoiceWav ("phrase_{0}.wav" -f $i) ([string]$phrases[$i]) $fr.VoiceInfo.Name 1
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
