#!/usr/bin/env python3
"""Opt-in Quest device-switch validation against a built scrcpy client.

Temporarily enables the boundary to test apply/restore, then restores its initial
property. Preserves any existing proximity override and its timeout. Does not
install an APK. Close other scrcpy sessions before running.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--client', required=True, help='Built scrcpy executable')
parser.add_argument('--server', required=True, help='Built scrcpy server APK')
parser.add_argument('--adb', default=os.environ.get('ADB', 'adb'))
parser.add_argument('--serial')
args = parser.parse_args()
adb_command = [args.adb] + (['-s', args.serial] if args.serial else [])
output = Path(__file__).resolve().parents[1] / 'build/quest-settings-tests'
output.mkdir(parents=True, exist_ok=True)
environment = dict(os.environ, ADB=args.adb, SCRCPY_SERVER_PATH=str(Path(args.server).resolve()))
environment['SCRCPY_ICON_DIR'] = str(Path(__file__).resolve().parents[2] / 'app/data')
property_name = 'debug.oculus.guardian_pause'


def adb(*command):
    return subprocess.check_output(adb_command + list(command), text=True, stderr=subprocess.STDOUT, timeout=15)


def state():
    boundary = adb('shell', 'getprop', property_name).strip()
    dump = adb('shell', 'dumpsys', 'vrpowermanager')
    match = re.search(r'^Virtual proximity state: (\w+)', dump, re.M)
    assert match, dump
    return boundary, match.group(1)


def server_pids():
    return [line.split()[0] for line in adb('shell', 'ps -A -o PID,NAME,ARGS').splitlines()
            if 'com.genymobile.scrcpy.Server' in line and line.split()[1] == 'app_process']


def set_boundary(value):
    adb('shell', 'setprop ' + property_name + " '" + value + "'")


original = state()
assert original[0] in ('', '0', '1'), original
assert original[1] in ('CLOSE', 'DISABLED'), original
assert not server_pids(), 'Close existing scrcpy sessions first'
print('Original:', original, flush=True)
try:
    for mode in ('normal', 'kill'):
        set_boundary('0')
        before = state()
        log_path = output / (mode + '.log')
        command = [args.client, '--quest-disable-proximity', '--quest-disable-boundary',
                   '--no-audio', '--no-playback', '--max-size=1280', '--max-fps=30',
                   '--time-limit=14', '--record=' + str(output / (mode + '.mp4')), '--verbosity=debug']
        if args.serial:
            command += ['--serial=' + args.serial]
        if mode == 'kill':
            command += ['--no-cleanup']
        with log_path.open('w') as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=environment)
            try:
                deadline = time.monotonic() + 25
                while time.monotonic() < deadline:
                    if 'Quest device overrides active: proximity=true, boundary=true' in log_path.read_text():
                        break
                    assert process.poll() is None, log_path.read_text()
                    time.sleep(.1)
                else:
                    raise AssertionError('Settings helper did not become ready')
                active = state()
                assert active == ('1', 'CLOSE'), active
                if mode == 'kill':
                    targets = server_pids()
                    assert len(targets) == 1, targets
                    adb('shell', 'kill', '-9', targets[0])
                    print('Killed owned server:', targets[0], flush=True)
                process.wait(timeout=40)
                if mode == 'normal':
                    assert process.returncode == 0, log_path.read_text()
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    after = state()
                    if after == before:
                        break
                    time.sleep(.2)
                assert after == before, (before, active, after)
                print(mode, 'before:', before, 'active:', active, 'after:', after, flush=True)
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
finally:
    set_boundary(original[0])
    print('Restored original boundary:', state(), flush=True)
