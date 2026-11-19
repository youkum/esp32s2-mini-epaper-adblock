# 构建、烧写 s2mini 应用程序，并捕获原生 USB CDC 串口日志。
# 不烧写 LittleFS。首次部署另行执行 uploadfs；升级时保留已有文件系统。
# 等待端口稳定并重试，以处理 USB 重新枚举；捕获时启用 DTR 和 RTS。
param(
  [string]$Port = "COM3",
  [string]$Root = (Split-Path -Parent $PSScriptRoot),
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

$Root = (Resolve-Path -LiteralPath $Root -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath (Join-Path $Root "platformio.ini"))) {
  throw "Root 必须指向包含 platformio.ini 的固件目录"
}
Set-Location -LiteralPath $Root

if (-not $SkipBuild) {
  "正在构建（日志：build.log）"
  python -m platformio run -d $Root -e s2mini *>> "$Root\build.log"
  if ($LASTEXITCODE -ne 0) { "构建失败，请检查 build.log"; exit 1 }
}

"正在上传应用程序（日志：upload.log）"
$uploaded = $false
for ($i = 1; $i -le $MaxTries; $i++) {
  if (-not (Wait-StablePort -Name $Port)) { "端口 $Port 未出现（第 $i 次尝试）"; continue }
  python -m platformio run -d $Root -e s2mini -t upload --upload-port $Port *>> "$Root\upload.log"
  if ($LASTEXITCODE -eq 0) { "第 $i 次尝试上传成功"; $uploaded = $true; break }
  "第 $i 次上传失败，准备重试"
  Start-Sleep -Seconds 6
}
if (-not $uploaded) { "上传失败，请检查 upload.log"; exit 1 }

"正在捕获串口日志，持续 $CaptureSec 秒"
Start-Sleep -Milliseconds 800
$sp = New-Object System.IO.Ports.SerialPort($Port, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.ReadTimeout = 200
$sb = New-Object System.Text.StringBuilder
$t0 = Get-Date
$drops = 0
try { $sp.Open() } catch { "打开串口失败：$_" }
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
"串口异常次数：$drops，捕获字符数：" + $text.Length
$text
