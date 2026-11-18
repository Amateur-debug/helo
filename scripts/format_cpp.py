#!/usr/bin/env python3
"""使用固定版本clang-format检查或整理项目自有C++，不修改第三方源码。"""
import argparse
import pathlib
import re
import shutil
import subprocess
import sys

FORMAT_VERSION = '18.1.8'
ROOT = pathlib.Path(__file__).resolve().parents[1]


def source_files():
    """按稳定顺序列出公共接口、模型、测试和驱动中的自有C++文件。"""
    paths = []
    for relative in ('hw/include', 'hw/model', 'hw/verif', 'sw/driver'):
        for path in (ROOT / relative).rglob('*'):
            if path.suffix in ('.h', '.cpp') and 'softfloat' not in path.parts:
                paths.append(path)
    return sorted(paths)


def main():
    """验证工具版本后执行只读检查，只有显式write选项才修改文件。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang-format', help='clang-format 18.1.8的可执行文件路径')
    parser.add_argument('--write', action='store_true', help='应用格式化，默认仅检查')
    args = parser.parse_args()
    executable = args.clang_format or shutil.which('clang-format-18') or shutil.which('clang-format')
    if not executable:
        parser.error('需要clang-format 18.1.8；请安装或指定--clang-format')
    version = subprocess.check_output([executable, '--version'], text=True)
    match = re.search(r'version (\d+\.\d+\.\d+)', version)
    if not match or match.group(1) != FORMAT_VERSION:
        parser.error(f'需要clang-format {FORMAT_VERSION}，实际为{version.strip()}')
    paths = source_files()
    command = [executable, '--style=file']
    command += ['-i'] if args.write else ['--dry-run', '--Werror']
    result = subprocess.run(command + [str(path) for path in paths], cwd=ROOT)
    print(f'{"Formatted" if args.write else "Checked"} {len(paths)} C++ files with clang-format {FORMAT_VERSION}')
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
