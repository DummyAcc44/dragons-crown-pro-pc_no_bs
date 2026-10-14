"""Per-title facts for the games this port knows, keyed by TITLE_ID in sce_sys/param.sfo.

Everything here was read from the supplied package; nothing is assumed.
"""
from pathlib import Path

from ps4_module import parse_sfo

PROFILES = {
    'CUSA10454': {
        'name': "Dragon's Crown Pro",
        'content_id': 'UP2611-CUSA10454_00-DRAGONSCROWNPRO1',
        'versions': {
            # The only package seen is a base ('gd') declaring APP_VER 01.00 and VERSION 01.03.
            '1.03': {
                'kind': 'base', 'app_ver': '01.00', 'sfo_version': '01.03',
                'eboot_sha256': 'f9bff150c4c28507096e7e143a07492063ed89bf05ac6cb995a55a0946e3508a',
            },
        },
        'default_version': '1.03',
        'entry': 'eboot.bin',
        'extra_executables': (),
        # Linked after the eboot in this order (libc.prx needs libSceFios2.prx, as in the other ports).
        'modules': ('libc.prx', 'libSceFios2.prx', 'libSceNpToolkit2.prx'),
        # What the loader needs; the game data (DRACRO*.CPK) is only unpacked with --scope full.
        'core_patterns': ('eboot.bin', 'sce_module/*', 'sce_sys/*', '*.plt'),
    },
}


def title_id(game):
    """TITLE_ID of a game folder, or None."""
    try:
        return parse_sfo((Path(game) / 'sce_sys' / 'param.sfo').read_bytes()).get('TITLE_ID')
    except (OSError, ValueError, KeyError):
        return None


def profile(game_or_title):
    """The profile for a TITLE_ID or a game folder, or None for an unknown title."""
    key = str(game_or_title)
    if key not in PROFILES:
        key = title_id(game_or_title) or ''
    return PROFILES.get(key)


def version_of(game):
    """The profile version whose param.sfo VERSION matches the game folder, or None."""
    try:
        sfo = parse_sfo((Path(game) / 'sce_sys' / 'param.sfo').read_bytes())
    except (OSError, ValueError):
        return None
    found = PROFILES.get(sfo.get('TITLE_ID', ''))
    if not found:
        return None
    return next((v for v, info in found['versions'].items()
                 if info['app_ver'] == sfo.get('APP_VER') and info['sfo_version'] == sfo.get('VERSION')), None)
