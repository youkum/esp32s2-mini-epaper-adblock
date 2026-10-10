$ET = 'C:\Users\Administrator\.platformio\packages\tool-esptoolpy\esptool.py'
$R  = 'D:\works\3.98\merged-adblock-epd'
$p = @()
$p += (Test-Path $ET)
"esptool present: " + $p[0]
if (-not $p[0]) { Get-ChildItem 'C:\Users\Administrator\.platformio\packages' -Filter 'tool-esptool*' | Select-Object -ExpandProperty Name; exit 1 }

"=== read partition table @0x8000 ==="
python $ET --chip esp32s2 --port COM3 --before default_reset --after no_reset read_flash 0x8000 0x1000 "$R\part_on_device.bin"
"=== read littlefs head @0x250000 ==="
python $ET --chip esp32s2 --port COM3 --before default_reset --after no_reset read_flash 0x250000 0x100 "$R\fs_head_on_device.bin"
"=== read coredump @0x3F0000 ==="
python $ET --chip esp32s2 --port COM3 --before default_reset --after no_reset read_flash 0x3F0000 0x10000 "$R\coredump.bin"

"=== hashes ==="
foreach ($f in @("$R\part_on_device.bin", "$R\.pio\build\s2mini\partitions.bin", "$R\fs_head_on_device.bin", "$R\coredump.bin", "$R\data\blocklist.bin")) {
  if (Test-Path $f) { $h = (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash; $len = (Get-Item -LiteralPath $f).Length; "$f  $len  $h" } else { "$f  MISSING" }
}
