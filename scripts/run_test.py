#!/usr/bin/env python3
"""记录每个 CTest 子进程的完整输出、元数据和真实退出状态。"""
import argparse
import datetime
import os
import pathlib
import shlex
import signal
import subprocess
import sys


def main():
    """执行测试并在成功、失败和超时时统一关闭日志。"""
    parser = argparse.ArgumentParser()
    parser.add_argument('--name', required=True)
    parser.add_argument('--dut', required=True)
    parser.add_argument('--logs', required=True)
    parser.add_argument('--build', required=True)
    parser.add_argument('--timeout', type=float, default=30)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    start = datetime.datetime.now()
    directory = pathlib.Path(args.logs)
    directory.mkdir(parents=True, exist_ok=True)
    stem = f'{args.name}_{start:%Y%m%d_%H%M%S}'
    index = 0
    while True:
        path = directory / f'{stem}{"_" + str(index) if index else ""}.log'
        try:
            log = path.open('x', encoding='utf-8')
            break
        except FileExistsError:
            index += 1
    return_code = 125
    result = 'ERROR'
    with log:
        log.write(f'Test: {args.name}\nDUT: {args.dut}\nCommand: {shlex.join(command)}\nBuild: {args.build}\nStart: {start:%Y%m%d_%H%M%S}\n')
        log.flush()
        try:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                return_code = process.wait(timeout=args.timeout)
                result = 'PASS' if return_code == 0 else 'FAIL'
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                return_code = process.wait()
                result = 'TIMEOUT'
        except OSError as error:
            log.write(f'Runner error: {error}\n')
        log.write(f'\nEnd: {datetime.datetime.now():%Y%m%d_%H%M%S}\nExit code: {return_code}\nResult: {result}\n')
    print(path.read_text(encoding='utf-8'), end='')
    print(f'Log: {path}')
    if return_code < 0:
        # 将信号终止保留为同一信号，而不是误转为通过。
        os.kill(os.getpid(), -return_code)
    return return_code


if __name__ == '__main__':
    sys.exit(main())
