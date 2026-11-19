#!/usr/bin/env python3
"""验证合成镜像不会静默漏掉文件系统或接受超过分区容量的产物。"""

from pathlib import Path
import tempfile
import unittest

from make_release import collect_images, image_layout


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "partitions.csv").write_text(
            "# 分区表\n"
            "otadata,data,ota,0xe000,0x2000,\n"
            "app0,app,ota_0,0x10000,0x120000,\n"
            "littlefs,data,spiffs,0x260000,0x190000,\n", encoding="utf-8")
        self.layout = image_layout(self.root, self.root)
        for _, _, path in self.layout:
            full = path if path.is_absolute() else self.root / path
            full.parent.mkdir(parents=True, exist_ok=True)
            full.write_bytes(b"image")

    def test_offsets_follow_partition_table(self):
        self.assertEqual(self.layout[-1][:2], (0x260000, 0x190000))

    def test_missing_filesystem_fails(self):
        (self.root / "littlefs.bin").unlink()
        with self.assertRaisesRegex(ValueError, "buildfs"):
            collect_images(self.root, self.layout)

    def test_no_fs_is_explicit(self):
        images = collect_images(self.root, self.layout, no_fs=True)
        self.assertEqual(len(images), 4)
        self.assertNotIn("littlefs.bin", [path.name for _, path in images])

    def test_missing_application_fails_even_without_fs(self):
        (self.root / "firmware.bin").unlink()
        with self.assertRaisesRegex(ValueError, "firmware.bin"):
            collect_images(self.root, self.layout, no_fs=True)

    def test_oversize_application_fails(self):
        (self.root / "firmware.bin").write_bytes(b"\xff" * (0x120000 + 1))
        with self.assertRaisesRegex(ValueError, "产物大小无效"):
            collect_images(self.root, self.layout)


if __name__ == "__main__":
    unittest.main()
