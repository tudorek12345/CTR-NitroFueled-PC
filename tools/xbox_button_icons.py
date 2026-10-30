#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch all six CUSA13795 font copies in a prepared game folder using CC0 Xbox icons.

Requires Pillow. Base and update folders must contain the original extracted files;
--game is the separate merged folder made by the launcher. Original archives stay intact.
Only font prompts change; prerecorded videos and controller diagrams keep their artwork.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import buttons  # noqa: E402
import iga  # noqa: E402

FONTS = {
    'languagestartup.pak': ('fonts/geekspeak.igz',),
    'permanent.pak': ('fonts/geekspeak_outline.igz', 'fonts/zoinks_outline.igz'),
    'update.pak': buttons.FONTS,
}


def patch_fonts(base, update, game, verify_only=False):
    base, update, game = (Path(p).resolve(strict=True) for p in (base, update, game))
    if game in (base, update):
        raise ValueError('--game must be a separate prepared folder')
    reference = iga.Archive(update / buttons.ARCHIVE)
    reference_rects = buttons.atlas(reference.read(buttons.FONTS[0]))[3]
    with Image.open(ROOT / 'assets/button_icons/xbox.png') as image:
        template = image.convert('RGBA')
    for name, fonts in FONTS.items():
        source = update / 'archives' / name
        if not source.is_file():
            source = base / 'archives' / name
        target = game / 'archives' / name
        if not target.is_file() or target.is_symlink():
            raise ValueError(f'expected a regular prepared archive: {target}')
        if target.resolve() in (source.resolve(), (base / 'archives' / name).resolve(),
                                (update / 'archives' / name).resolve()):
            raise ValueError(f'target resolves to an original archive: {target}')
        archive = iga.Archive(source)
        expected, records = {}, []
        for font in fonts:
            data = archive.read(font)
            _, width, height, rects, _ = buttons.atlas(data)
            sheet = Image.new('RGBA', (width, height))
            for button, (x0, y0, x1, y1) in rects.items():
                a, b, c, d = reference_rects[button]
                icon = template.crop((a, b, c + 1, d + 1)).resize(
                    (x1 - x0 + 1, y1 - y0 + 1), Image.Resampling.LANCZOS)
                sheet.paste(icon, (x0, y0))
            expected[font] = buttons.restyle(data, (width, height, sheet.tobytes()))
            records += iga.replacement(archive, font, expected[font], shrink=True)

        def matches(path):
            current = iga.Archive(path)
            return all(current.read(font) == data for font, data in expected.items())

        if not matches(target):
            if verify_only:
                raise ValueError(f'Xbox font verification failed: {name}')
            fd, temporary = tempfile.mkstemp(prefix=f'.{name}-', suffix='.tmp', dir=target.parent)
            os.close(fd)
            temporary = Path(temporary)
            try:
                shutil.copy2(source, temporary)
                with temporary.open('r+b') as output:
                    for offset, data in records:
                        output.seek(offset)
                        output.write(data)
                if not matches(temporary):
                    raise ValueError(f'rebuilt archive did not verify: {name}')
                # Replace the directory entry, never write through the launcher's hard link.
                os.replace(temporary, target)
            finally:
                temporary.unlink(missing_ok=True)
        if os.path.samefile(source, target):
            raise ValueError(f'patched archive still shares the original file: {name}')
        print(f'{name}: {len(fonts)} Xbox font copies verified')
    if not verify_only:
        (game / '.xbox-icons.json').write_text(json.dumps({
            'style': 'xbox', 'font_archives': FONTS,
            'icons': 'Xelu Free Controller Prompts (CC0-1.0)',
            'code': 'GPL-2.0-or-later; see THIRD-PARTY-NOTICES.md',
        }, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', required=True, type=Path)
    parser.add_argument('--update', required=True, type=Path)
    parser.add_argument('--game', required=True, type=Path)
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    try:
        patch_fonts(args.base, args.update, args.game, args.verify_only)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'Xbox icons: {error}\n')


if __name__ == '__main__':
    main()
