$ET = 'C:\Users\Administrator\.platformio\packages\tool-esptoolpy\esptool.py'
$R  = 'D:\works\3.98\merged-adblock-epd'
for ($i = 1; $i -le 6; $i++) {
  "--- attempt $i ---"
  python $ET --chip esp32s2 --port COM3 --before default_reset --after no_reset read_flash 0x8000 0x1000 "$R\part_on_device.bin"
  if ((Test-Path "$R\part_on_device.bin") -and ((Get-Item "$R\part_on_device.bin").Length -eq 4096)) { "PART-READ-OK"; break }
  Start-Sleep -Seconds 4
}
if (Test-Path "$R\part_on_device.bin") {
  $h = (Get-FileHash -LiteralPath "$R\part_on_device.bin" -Algorithm SHA256).Hash
  "device part: " + $h
  $h2 = (Get-FileHash -LiteralPath "$R\.pio\build\s2mini\partitions.bin" -Algorithm SHA256).Hash
  "local  part: " + $h2
}
