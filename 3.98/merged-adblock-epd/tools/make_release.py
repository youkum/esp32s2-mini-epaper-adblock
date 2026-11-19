#!/usr/bin/env python3
"""将 s2mini 构建产物合成为从 0x0 烧写的镜像。

先运行固件构建和 buildfs，再运行本脚本。默认必须包含 LittleFS；只有显式
传入 --no-fs 才省略它。生成镜像不等于已完成第三方许可核对或真机验收。
"""

import argparse
import csv
import hashlib
import os
from pathlib import Path
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[1]
PIO_HOME = Path(os.environ.get("PLATFORMIO_CORE_DIR") or Path.home() / ".platformio")
BUILD = PROJECT / ".pio" / "build" / "s2mini"
OUT = PROJECT / "firmware-merged.bin"


def image_layout(project, pio_home):
    """分区偏移和容量从 partitions.csv 读取，与固件保持一致。"""
    partitions = {}
    with (project / "partitions.csv").open(encoding="utf-8") as source:
        for line in source:
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            row = next(csv.reader([line]))
            partitions[row[0].strip()] = (int(row[3].strip(), 0), int(row[4].strip(), 0))
    ota_offset, ota_size = partitions["otadata"]
    app_offset, app_size = partitions["app0"]
    fs_offset, fs_size = partitions["littlefs"]
    return [
        (0x1000, 0x7000, Path("bootloader.bin")),
        (0x8000, 0x1000, Path("partitions.bin")),
        (ota_offset, ota_size, pio_home / "packages" / "framework-arduinoespressif32" /
         "tools" / "partitions" / "boot_app0.bin"),
        (app_offset, app_size, Path("firmware.bin")),
        (fs_offset, fs_size, Path("littlefs.bin")),
    ]


def collect_images(build, layout, no_fs=False):
    """缺文件或超过分区容量时拒绝生成不完整的镜像。"""
    images = []
    for offset, capacity, path in layout:
        if no_fs and path.name == "littlefs.bin":
            continue
        full = path if path.is_absolute() else build / path
        if not full.is_file():
            hint = "；请先运行 -t buildfs，或显式传入 --no-fs" if path.name == "littlefs.bin" else "；请先完成固件构建"
            raise ValueError("缺少构建产物：%s%s" % (full, hint))
        size = full.stat().st_size
        if size == 0 or size > capacity:
            raise ValueError("产物大小无效：%s（%d 字节，容量 %d 字节）" % (full, size, capacity))
        images.append((offset, full))
    return images


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-fs", action="store_true", help="显式省略 LittleFS 镜像")
    parser.add_argument("--output", type=Path, default=OUT, help="合成镜像输出路径")
    options = parser.parse_args()
    esptool = PIO_HOME / "packages" / "tool-esptoolpy" / "esptool.py"
    if not esptool.is_file():
        parser.exit(1, "未找到 %s；请先完成 s2mini 构建，并保持 PLATFORMIO_CORE_DIR 一致\n" % esptool)
    try:
        images = collect_images(BUILD, image_layout(PROJECT, PIO_HOME), options.no_fs)
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, str(error) + "\n")

    args = [sys.executable, str(esptool), "--chip", "esp32s2", "merge_bin",
            "-o", str(options.output), "--flash_mode", "dio", "--flash_freq", "40m",
            "--flash_size", "4MB"]
    for offset, path in images:
        print("  %#06x  %s" % (offset, path.name))
        args += ["%#x" % offset, str(path)]
    subprocess.check_call(args)
    data = options.output.read_bytes()
    print("\n%s（%d 字节）" % (options.output, len(data)))
    print("SHA-256：" + hashlib.sha256(data).hexdigest())
    print("烧写命令见仓库根目录 GUIDE.md；合成镜像会覆盖配置分区。")


if __name__ == "__main__":
    main()
