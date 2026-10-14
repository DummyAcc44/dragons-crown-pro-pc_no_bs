"""Run the loader on the development folders.

The loader needs none of this to run: started alone it finds its folders next to the executable and writes the next
numbered log (build/logs/dragons_crown_pro_NNN.log here) itself. Its output is echoed to the console.

Usage: python scripts/run_game.py [--version 1.03] [--loader PATH] [--check-imports] [--timeout SECONDS] [--log FILE] [-- loader args]
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_profiles  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
LOAD_EXEC_EXIT = 30  # dcp-loader: sceSystemServiceLoadExec ended the process


def find_loader():
    candidates = sorted((ROOT / 'build' / 'cmake').glob('*/bin/dcp-loader.exe'), key=lambda p: p.stat().st_mtime)
    return candidates[-1] if candidates else None


def emit(line):
    print(line, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--version', default='1.03')
    parser.add_argument('--loader', type=Path)
    parser.add_argument('--check-imports', action='store_true', help='list imports the runtime lacks, without running the game')
    parser.add_argument('--user-dir', type=Path, default=ROOT / 'user')
    parser.add_argument('--log', type=Path)
    parser.add_argument('--timeout', type=int, default=0, help='kill the loader after this many seconds (0: never)')
    parser.add_argument('extra', nargs='*', help='further loader arguments (after --)')
    args = parser.parse_args()

    view = ROOT / 'game' / 'prepared' / f'v{args.version}'
    image = ROOT / 'build' / 'image' / f'v{args.version}'
    loader = args.loader or find_loader()
    if not (image / 'boot.bin').is_file():
        sys.exit(f'No boot image for {args.version}: run tools/build.ps1')
    if not loader or not loader.is_file():
        sys.exit('No dcp-loader.exe: run tools/build.ps1')

    user = args.user_dir
    user.mkdir(parents=True, exist_ok=True)
    command = [str(loader), str(image / 'boot.bin'), '--content-profile', str(image / 'content.bin'),
               '--app0', str(view), '--user', str(user)]
    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        command += ['--log', str(args.log)]
    else:
        command += ['--logs', str(ROOT / 'build' / 'logs')]
    if args.check_imports:
        command += ['--check-imports']
    if args.timeout:
        command += ['--dcp-skip-launcher']  # automated runs must not wait at the launcher
    command += args.extra

    profile = game_profiles.profile(view)
    emit(f'[run] {profile["name"] if profile else "unknown title"} {args.version}')
    try:
        report = json.loads((image / 'image.json').read_text())
        emit(f'[run] {report["title_id"]} APP_VER {report["app_ver"]}, eboot sha256 {report["eboot_sha256"][:16]}, '
             f'entry {report["entry"]}, image {report["image_bytes"]:,} bytes, sdk {report["sdk_version"]}')
        emit(f'[run] modules {", ".join(m["file"] for m in report["modules"])}; imports {report["imports"]} '
             f'({report["native_bindings"]} bound natively, {report["unresolved"]} left to the runtime)')
    except (OSError, KeyError, ValueError) as error:
        emit(f'[run] image.json unreadable: {error}')
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, cwd=loader.parent)
    timer = threading.Timer(args.timeout, process.kill) if args.timeout else None
    if timer:
        timer.start()
    for raw in iter(process.stdout.readline, b''):
        emit(raw.decode('utf-8', 'replace').rstrip('\r\n'))
    code = process.wait()
    if timer:
        timer.cancel()
    emit(f'[run] exit code {code}')
    if code == LOAD_EXEC_EXIT:
        emit('[run] the game requested sceSystemServiceLoadExec (see the LoadExec lines above)')
    return code


if __name__ == '__main__':
    sys.exit(main())
