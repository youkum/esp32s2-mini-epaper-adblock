#!/usr/bin/env python3
"""构建器失败保护和非 2 次幂 Bloom 参数的回归检查。"""
import pathlib, subprocess, sys, tempfile, unittest
from build_blocklist import fnv64, bloom_positions
ROOT=pathlib.Path(__file__).resolve().parents[1]
class BuilderRegression(unittest.TestCase):
    def run_builder(self,*args):
        return subprocess.run([sys.executable,str(ROOT/'tools/build_blocklist.py'),*map(str,args)],capture_output=True)
    def test_non_power_two(self):
        self.assertEqual(list(bloom_positions(fnv64(b'example.com'),1000000,6)),[184070,635625,87180,538735,990290,441845])
    def test_failures_keep_output(self):
        with tempfile.TemporaryDirectory() as d:
            root=pathlib.Path(d);out=root/'out.bin';source=root/'source.txt';source.write_text('ads.example.com\n')
            for case in ['missing','empty','bits0','bitsnegative','bitshuge','partial']:
                with self.subTest(case=case):
                    out.write_bytes(b'old-list')
                    if case=='empty':source.write_text('# 没有域名\n',encoding='utf-8')
                    else:source.write_text('ads.example.com\n')
                    args=[out,source]
                    if case=='missing':args=[out,root/'missing.txt']
                    elif case=='partial':args.append(root/'missing.txt')
                    elif case.startswith('bits'):args+=['--bloom-bits',{'bits0':'0','bitsnegative':'-1','bitshuge':'16777217'}[case]]
                    self.assertNotEqual(self.run_builder(*args).returncode,0)
                    self.assertEqual(out.read_bytes(),b'old-list')
    def test_non_power_two_file(self):
        with tempfile.TemporaryDirectory() as d:
            root=pathlib.Path(d);out=root/'out.bin';source=root/'source.txt';source.write_text('example.com\n')
            self.assertEqual(self.run_builder(out,source,'--bloom-bits','1000000').returncode,0)
            blob=out.read_bytes();self.assertEqual(int.from_bytes(blob[10:14],'little'),1000000)
            self.assertEqual(len(blob),20+125000+5)
            for pos in bloom_positions(fnv64(b'example.com'),1000000,blob[14]):self.assertTrue(blob[20+(pos>>3)]&(1<<(pos&7)))
if __name__=='__main__':unittest.main()
