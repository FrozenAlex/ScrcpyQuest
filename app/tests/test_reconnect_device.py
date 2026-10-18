#!/usr/bin/env python3
"""Opt-in reconnect test on a connected Quest. Close other scrcpy sessions first.

Kills only the test's uniquely identified Android server, then reconnects the
selected ADB transport. Confirms three separate 1080p recordings and preservation
of an existing numbered file. No APK is installed; uses the supplied server.
"""
import argparse
import json
import os
import re
from pathlib import Path
import subprocess
import signal
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--client', required=True)
parser.add_argument('--server', required=True)
parser.add_argument('--adb', default='adb')
parser.add_argument('--serial', required=True)
parser.add_argument('--ffprobe', default='ffprobe')
parser.add_argument('--output', required=True)
parser.add_argument('--server-loss-only', action='store_true', help='Keep USB connected; kill the test server for both losses')
parser.add_argument('--stop-on-retry', action='store_true', help='Test console interruption while waiting to reconnect')
parser.add_argument('--playback', action='store_true', help='Exercise the playback window as well as recording')
parser.add_argument('--audio', action='store_true', help='Require audio capture and verify it in each recording')
parser.add_argument('--check-window', action='store_true', help='Windows: verify native window identity and geometry across losses')
parser.add_argument('--close-on-retry', action='store_true', help='Windows: close the retained window during the retry delay')
parser.add_argument('--fullscreen', action='store_true', help='Start the playback test fullscreen')
args = parser.parse_args()
if args.check_window or args.close_on_retry:
    assert os.name == 'nt' and args.playback, 'Window checks require Windows and --playback'
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    enum_proc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [enum_proc, wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

    def window_state(pid):
        windows = []
        @enum_proc
        def visit(hwnd, _):
            owner = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value == pid and user32.IsWindowVisible(hwnd):
                rect = wintypes.RECT()
                assert user32.GetWindowRect(hwnd, ctypes.byref(rect))
                windows.append((hwnd, rect.left, rect.top, rect.right, rect.bottom))
            return True
        user32.EnumWindows(visit, 0)
        assert len(windows) == 1, windows
        return windows[0]

stopping = args.stop_on_retry or args.close_on_retry
output = Path(args.output).resolve()
output.mkdir(parents=True, exist_ok=False)
adb_command = [args.adb, '-s', args.serial]


def adb(*command):
    return subprocess.check_output(adb_command + list(command), text=True, stderr=subprocess.STDOUT, timeout=30)


def server_pids():
    return [line.split()[0] for line in adb('shell', 'ps -A -o PID,NAME,ARGS').splitlines()
            if 'com.genymobile.scrcpy.Server' in line and line.split()[1] == 'app_process']


def wait_for(predicate, process, log_path, seconds=45):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        text = log_path.read_text(errors='replace')
        if predicate(text):
            return
        assert process.poll() is None, text
        time.sleep(.1)
    raise AssertionError(log_path.read_text(errors='replace'))


assert not server_pids(), 'Close other scrcpy sessions first'
sentinel = output / 'capture.part002.mp4'
sentinel.write_bytes(b'keep-existing-recording')
environment = dict(os.environ, ADB=args.adb, SCRCPY_SERVER_PATH=str(Path(args.server).resolve()))
environment['SCRCPY_ICON_DIR'] = str(Path(__file__).resolve().parents[1] / 'data')
command = [args.client, '--serial=' + args.serial, '--reconnect', '--quest-cinematic',
           '--quest-disable-proximity', '--quest-disable-boundary',
           '--time-limit=12', '--verbosity=debug',
           '--record=' + str(output / 'capture.mp4')]
command += ['--require-audio'] if args.audio else ['--no-audio']
if not args.playback:
    command += ['--no-playback', '--no-window']
elif args.fullscreen:
    command += ['--fullscreen']
else:
    command += ['--window-x=120', '--window-y=140', '--window-width=960', '--window-height=540']
log_path = output / 'client.log'
with log_path.open('w') as log:
    creation_flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == 'nt' else 0
    process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=environment, creationflags=creation_flags)
    original_window = None
    try:
        for session in (1, 2):
            wait_for(lambda text: text.count('Quest cinematic FOV:') >= session, process, log_path)
            time.sleep(2)  # Keep playable video before the injected loss.
            if args.check_window or args.close_on_retry:
                current = window_state(process.pid)
                if original_window is None:
                    original_window = current
                assert current == original_window, (original_window, current)
            if session == 1 or args.server_loss_only:
                targets = server_pids()
                assert len(targets) == 1, targets
                adb('shell', 'kill', '-9', targets[0])
                print('Killed test server', targets[0], flush=True)
            else:
                print(adb('reconnect').strip(), flush=True)
            wait_for(lambda text: text.count('Reconnecting to') >= session, process, log_path)
            if args.check_window or args.close_on_retry:
                assert window_state(process.pid) == original_window
                print('Retained native window:', original_window, flush=True)
            if stopping:
                if args.close_on_retry:
                    assert user32.PostMessageW(original_window[0], 0x0010, 0, 0) # WM_CLOSE
                else:
                    process.send_signal(signal.CTRL_BREAK_EVENT if os.name == 'nt' else signal.SIGINT)
                break
        if not stopping:
            wait_for(lambda text: text.count('Quest cinematic FOV:') >= 3, process, log_path)
            if args.check_window:
                time.sleep(2)
                assert window_state(process.pid) == original_window
        process.wait(timeout=45)
        assert process.returncode == 0, log_path.read_text()
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=15)

assert sentinel.read_bytes() == b'keep-existing-recording'
recordings = sorted(path for path in output.glob('*.mp4') if path != sentinel)
assert len(recordings) == (1 if stopping else 3), recordings
if args.playback:
    text = log_path.read_text(errors='replace')
    retained = re.findall(r'Retaining playback window (\d+)', text)
    resumed = re.findall(r'Resuming playback in window (\d+)', text)
    assert retained and len(set(retained + resumed)) == 1, (retained, resumed)
    if not stopping:
        assert len(resumed) == 2, resumed
for path in recordings:
    probe = subprocess.check_output([args.ffprobe, '-v', 'error', '-select_streams', 'v:0',
                                    '-show_entries', 'stream=width,height,nb_frames', '-of', 'json', str(path)], text=True)
    stream = json.loads(probe)['streams'][0]
    assert (stream['width'], stream['height']) == (1920, 1080), stream
    assert int(stream['nb_frames']) > 0, stream
    if args.audio:
        probe = subprocess.check_output([args.ffprobe, '-v', 'error', '-select_streams', 'a:0',
                                        '-show_entries', 'stream=codec_type', '-of', 'json', str(path)], text=True)
        assert json.loads(probe)['streams'], 'Missing audio in ' + str(path)
    print(path.name, stream, flush=True)
assert not server_pids()
print('PASS: window close during retry' if args.close_on_retry else
      'PASS: console interruption' if args.stop_on_retry else
      'PASS: repeated reconnect, numbered recordings, collision preservation, and time-limit exit', flush=True)
