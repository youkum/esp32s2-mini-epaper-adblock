# Build, flash and capture the ESP32-S2 serial console.
#
# Two things this script exists for:
#  1. pio's upload target fails intermittently with
#     "OSError(22, 'device does not support the request')" while the board is
#     re-enumerating on USB. Waiting for a stable port and retrying fixes it.
#  2. The console is the chip's native USB CDC, and USBCDC only marks itself
#     connected when DTR *and* RTS are both asserted. .NET's SerialPort leaves
#     both false by default, which silently drops every byte the device sends.
param(
  [string]$Port = "COM3",
  [string]$Root = "D:\works\3.98\merged-adblock-epd",
  [int]$CaptureSec = 70,
  [int]$MaxTries = 5,
  [switch]$SkipBuild
)

function Get-Ports { return [System.IO.Ports.SerialPort]::GetPortNames() }

function Wait-StablePort {
  param([string]$Name, [int]$StableChecks = 6)
  $stable = 0
  for ($i = 0; $i -lt 80; $i++) {
    if ((Get-Ports) -contains $Name) { $stable++ } else { $stable = 0 }
    if ($stable -ge $StableChecks) { return $true }
    Start-Sleep -Milliseconds 500
  }
  return $false
}

Set-Location $Root

if (-not $SkipBuild) {
  "=== build ==="
  python -m platformio run -d $Root *>> "$Root\build.log"
  if ($LASTEXITCODE -ne 0) { "BUILD FAILED"; exit 1 }
}

"=== upload ==="
$uploaded = $false
for ($i = 1; $i -le $MaxTries; $i++) {
  if (-not (Wait-StablePort -Name $Port)) { "port $Port never appeared (try $i)"; continue }
  python -m platformio run -d $Root -t upload --upload-port $Port *>> "$Root\upload.log"
  if ($LASTEXITCODE -eq 0) { "uploaded on try $i"; $uploaded = $true; break }
  "upload try $i failed, retrying"
  Start-Sleep -Seconds 6
}
if (-not $uploaded) { "UPLOAD FAILED"; exit 1 }

"=== capture for $CaptureSec s ==="
Start-Sleep -Milliseconds 800
$sp = New-Object System.IO.Ports.SerialPort($Port, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.ReadTimeout = 200
$sb = New-Object System.Text.StringBuilder
$t0 = Get-Date
$drops = 0
try { $sp.Open() } catch { "open failed: $_" }
while (((Get-Date) - $t0).TotalSeconds -lt $CaptureSec) {
  try {
    $chunk = $sp.ReadExisting()
    if ($chunk) { [void]$sb.Append($chunk) }
  } catch {
    $drops++
    try { $sp.Close() } catch {}
    Start-Sleep -Milliseconds 400
    try { $sp.Open() } catch {}
  }
  Start-Sleep -Milliseconds 50
}
try { $sp.Close() } catch {}
$text = $sb.ToString()
$text | Set-Content -Encoding utf8 "$Root\serial.log"
"=== drops: $drops, chars: " + $text.Length + " ==="
$text
