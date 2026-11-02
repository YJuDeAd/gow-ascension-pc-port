"""Build the Brazilian Portuguese mod from your own US disc.

    python tools/make_ptbr_mod.py <US USRDIR or .iso> [--eu <EU USRDIR>] [--out mods/PT-BR]

The European disc (BCES-01741) already carries the Brazilian text
(RESOURCES.PSARC: R_LANG_PT-BR.WAD); it only lacks the voices and the entry in
its language table. The mod adds them, leaving European Portuguese as it is:

  mod.json                a recipe: GAME.PSARC's LANGS_BCES01741.TXT gets "17 pt-br
                          BR_PORTUGUESE" (the launcher applies it to your disc's copy)
  BR_PORTUGUESE.PSARC     \\ from the US disc (BCUS-98232), as they are
  MP-BR_PORTUGUESE.PSARC  /
  LOCALIZED_ARCHIVES      the EU list with the two archives above
                          and {"language": "pt-BR"}: the launcher offers Português (Brasil)

The game then loads them for console language 17 (PS3_SYS_LANG=17, or bytes
0x1C/0x1D = 17 in its system save, which the launcher writes).
"""
import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import psarc_replace  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
LANGS = '/WAD/PS3_BE/LANGS_BCES01741.TXT'


def mount_iso(iso):
    """Mount an .iso read-only (Windows) and return (USRDIR, unmount function)."""
    ps = (f"$i = Mount-DiskImage -ImagePath '{iso}' -PassThru; ($i | Get-Volume).DriveLetter")
    letter = subprocess.run(['powershell', '-NoProfile', '-Command', ps], capture_output=True,
                            text=True, check=True).stdout.strip().splitlines()[-1]

    def unmount():
        subprocess.run(['powershell', '-NoProfile', '-Command', f"Dismount-DiskImage -ImagePath '{iso}'"],
                       capture_output=True)
    return Path(f'{letter}:/PS3_GAME/USRDIR'), unmount


def build(us, eu, out, log=print):
    for need in (us / 'BR_PORTUGUESE.PSARC', us / 'MP-BR_PORTUGUESE.PSARC'):
        if not need.is_file():
            raise SystemExit(f'not the US disc? missing {need}')
    if not (eu / 'GAME.PSARC').is_file():
        raise SystemExit(f'missing the EU GAME.PSARC in {eu}')
    out.mkdir(parents=True, exist_ok=True)

    # The language table is changed by recipe (mod.json, psarc_patches): the
    # launcher applies it to the disc's GAME.PSARC together with any other
    # mod that touches that archive. An older build of this mod shipped a
    # whole GAME.PSARC; it would override the others.
    stale = out / 'GAME.PSARC'
    if stale.exists():
        stale.chmod(0o666)
        stale.unlink()
    eol = b'\r\n' if b'\r\n' in (eu / 'LOCALIZED_ARCHIVES').read_bytes() else b'\n'
    lines = (eu / 'LOCALIZED_ARCHIVES').read_bytes().split(eol)
    body = []
    for line in lines[1:]:
        if line in (b'BR_PORTUGUESE.PSARC', b'MP-BR_PORTUGUESE.PSARC'):
            continue
        body.append(line)
        if line == b'PORTUGUESE.PSARC':
            body.append(b'BR_PORTUGUESE.PSARC')
        if line == b'MP-PORTUGUESE.PSARC':
            body.append(b'MP-BR_PORTUGUESE.PSARC')
    count = len([b for b in body if b.strip()])
    (out / 'LOCALIZED_ARCHIVES').write_bytes(eol.join([str(count).encode()] + body))

    for name in ('BR_PORTUGUESE.PSARC', 'MP-BR_PORTUGUESE.PSARC'):
        log(f'{name}: copying from the US disc')
        dst = out / name
        if dst.exists():
            dst.chmod(0o666)
        shutil.copyfile(us / name, dst)
    (out / 'mod.json').write_text(json.dumps({
        'name': 'Português (Brasil)',
        'language': 'pt-BR',
        'description': 'Brazilian Portuguese voices and text: the voices from the US disc and the text the '
                       'European disc already has, with the entry in the game\'s language table.',
        'translations': {
            'pt-BR': {'description': 'Vozes e textos em português do Brasil: as vozes do disco americano e o texto '
                                     'que o disco europeu já traz, com a entrada na tabela de idiomas do jogo.'},
            'es': {'description': 'Voces y textos en portugués de Brasil: las voces del disco estadounidense y el '
                                  'texto que ya trae el disco europeo, con la entrada en la tabla de idiomas del juego.'},
        },
        'psarc_patches': {'GAME.PSARC': {LANGS: {'insert_after': '16 pl POLISH',
                                                  'line': '17 pt-br BR_PORTUGUESE'}}},
    }, ensure_ascii=False, indent=2), encoding='utf-8')
    log(f'done: {out}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('us', help='the US disc: its PS3_GAME/USRDIR folder, or the .iso')
    ap.add_argument('--eu', default=str(ROOT.parent / 'work' / 'disc' / 'PS3_GAME' / 'USRDIR'))
    ap.add_argument('--out', default=str(ROOT / 'mods' / 'PT-BR'))
    a = ap.parse_args()
    us, unmount = Path(a.us), None
    if us.suffix.lower() == '.iso':
        us, unmount = mount_iso(us)
    elif (us / 'PS3_GAME' / 'USRDIR').is_dir():
        us = us / 'PS3_GAME' / 'USRDIR'
    try:
        build(us, Path(a.eu), Path(a.out))
    finally:
        if unmount:
            unmount()
    return 0


if __name__ == '__main__':
    sys.exit(main())
