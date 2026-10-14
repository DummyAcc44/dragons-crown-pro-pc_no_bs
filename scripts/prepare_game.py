"""Prepare a runnable game tree from the user's package.

  game/*.pkg                  the user's package (read-only)
  game/extracted/base         what the package contains (resumable)
  game/prepared/v<version>    the /app0 tree the loader uses (hard links into extracted/)

Usage: python scripts/prepare_game.py [--version 1.03] [--scope core|full] [--game-root DIR]
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_profiles  # noqa: E402
import game_view  # noqa: E402
import pkg_info  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TITLE_ID = 'CUSA10454'


def fail(message):
    print(f'error: {message}', file=sys.stderr)
    sys.exit(1)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def find_base_package(game_root, title):
    """The base package of `title` among the .pkg files in game_root, classified by metadata, not by name."""
    found = []
    for path in sorted(game_root.glob('*.pkg')):
        try:
            info = pkg_info.inspect(str(path))
        except (OSError, ValueError) as error:
            fail(f'{path.name}: {error}')
        if pkg_info.classify(info) == 'base' and pkg_info.title_id(info) == title:
            found.append((path, info))
    if not found:
        fail(f'no base package for {title} found in {game_root}')
    if len(found) > 1:
        fail(f'several base packages for {title}: {", ".join(p.name for p, _ in found)}')
    return found[0]


def progress_printer(label):
    last = [-1]

    def report(done, total, rate):
        percent = done * 100 // max(total, 1)
        if percent >= last[0] + 5 or percent == 100 and last[0] != 100:
            last[0] = percent
            print(f'  {label}: {percent:3d}%  {done >> 20} of {total >> 20} MB  {rate:.0f} MB/s', flush=True)
    return report


def extract(pkg_path, target, only, label):
    import pkg_extract
    if pkg_extract.is_extracted(str(pkg_path), str(target), only):
        print(f'  {label}: already extracted ({target})')
        return
    scope = 'everything' if not only else 'executables, modules, metadata'
    print(f'  {label}: extracting {scope} -> {target}', flush=True)
    started = time.time()
    try:
        pkg_extract.extract(str(pkg_path), str(target), progress_printer(label), None, only)
    except (OSError, ValueError) as error:
        fail(f'{label}: extraction failed: {error}')
    print(f'  {label}: done in {time.time() - started:.0f}s')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game-root', type=Path, default=ROOT / 'game')
    parser.add_argument('--version', help='game version to prepare (default: the profile default)')
    parser.add_argument('--scope', choices=('core', 'full'), default='core',
                        help='core: executables, modules and metadata only; full: all game data')
    args = parser.parse_args()
    sys.stdout.reconfigure(errors='replace')

    profile = game_profiles.PROFILES[TITLE_ID]
    version = args.version or profile['default_version']
    if version not in profile['versions']:
        fail(f'unsupported version {version}; supported: {", ".join(profile["versions"])}')
    wanted = profile['versions'][version]
    root = args.game_root.resolve()
    only = list(profile['core_patterns']) if args.scope == 'core' else None

    package, info = find_base_package(root, TITLE_ID)
    sfo = info['param_sfo']
    print(f'package: {package.name}  (APP_VER {sfo.get("APP_VER")}, VERSION {sfo.get("VERSION")})')
    if info['content_id'] != profile['content_id']:
        fail(f'content id {info["content_id"]} does not match the profile')
    if (sfo.get('APP_VER'), sfo.get('VERSION')) != (wanted['app_ver'], wanted['sfo_version']):
        fail(f'package is APP_VER {sfo.get("APP_VER")} / VERSION {sfo.get("VERSION")}; version {version} needs '
             f'{wanted["app_ver"]} / {wanted["sfo_version"]}')
    if not info['fake_pkg']:
        fail('package is not a fake PKG: it needs console keys and cannot be unpacked here')

    layers = [root / 'extracted' / 'base']
    extract(package, layers[0], only, 'base')

    view = root / 'prepared' / f'v{version}'
    stats = game_view.build_view(layers, view, f'{TITLE_ID} {version}')
    print(f'view   : {view}  ({stats["link"]} links, {stats["copy"]} copies)')

    problems = []
    if game_profiles.version_of(view) != version:
        problems.append(f'sce_sys/param.sfo does not report version {version}')
    eboot = view / profile['entry']
    if not eboot.is_file():
        problems.append(f'{profile["entry"]} missing')
    elif sha256(eboot) != wanted['eboot_sha256']:
        problems.append(f'{profile["entry"]} sha256 does not match the known {version} build')
    for name in profile['modules']:
        if not (view / 'sce_module' / name).is_file():
            problems.append(f'sce_module/{name} missing')
    if problems:
        fail('prepared tree is not valid:\n  ' + '\n  '.join(problems))
    scope = 'full' if json.loads((layers[0] / '.pkg_extracted').read_text())['only'] is None else 'core'
    (view / game_view.MARKER).write_text(json.dumps(
        {**json.loads((view / game_view.MARKER).read_text()), 'version': version, 'scope': scope}, indent=2))
    print(f'ready  : {view}  ({TITLE_ID} {version}, extracted scope {scope})')


if __name__ == '__main__':
    main()
