#!/usr/bin/env python3
"""编译并运行固件共享 C++ 核心与真实面板驱动的模拟测试。"""
import argparse, os, pathlib, shutil, subprocess, tempfile
p=argparse.ArgumentParser()
p.add_argument('--compiler',default=os.environ.get('CXX','g++'),help='g++/clang++ 或 zig 可执行文件路径')
a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]
compiler=shutil.which(a.compiler) or a.compiler
cmd=[compiler]
if pathlib.Path(compiler).stem.lower()=='zig': cmd+=['c++']
with tempfile.TemporaryDirectory(prefix='adblock-native-') as d:
    exe=pathlib.Path(d)/('core.exe' if os.name=='nt' else 'core')
    subprocess.run(cmd+['-std=c++17','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror',
        '-DBOARD_PROFILE_ESP32_S2_MINI','-DEPD_PANEL_75_JDY79668',
        '-I'+str(root/'tools/native'),'-I'+str(root/'src'),
        str(root/'tools/native/test_core.cpp'),str(root/'src/epd_driver.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
