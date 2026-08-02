#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Снимок символов пакета: сохраняет ТЕ ЖЕ библиотеки, что уходят игрокам.

    python keep_symbols.py <каталог bin пакета> [--note "что за сборка"]

Зачем. Расшифровать чужой стек можно только по тому самому файлу, который был у игрока: адреса
привязаны к конкретной сборке, и пересобранная библиотека, пусть даже из того же исходника, даёт
ДРУГИЕ адреса. Пока снимка нет, отчёт о вылете превращается в набор чисел - именно это и вышло
01.08 с тремя логами тестера.

Снимок кладётся в da_port/symbols/<дата>/ вместе с описью: имя, размер, контрольная сумма и
предпочтительный базовый адрес. Опись нужна, чтобы потом ответить на вопрос «а это точно та
сборка?» - размер и сумма сверяются с тем, что стоит у игрока, за секунду.

Игрокам этот каталог не отдаётся: он нужен нам, а не им.
"""

import argparse
import hashlib
import os
import shutil
import struct
import sys
import time

SYMROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'symbols')


def pe_image_base(path):
    try:
        with open(path, 'rb') as f:
            head = f.read(0x400)
        off = struct.unpack_from('<I', head, 0x3C)[0]
        magic = struct.unpack_from('<H', head, off + 24)[0]
        if magic == 0x20B:
            return struct.unpack_from('<Q', head, off + 24 + 24)[0]
        return struct.unpack_from('<I', head, off + 24 + 28)[0]
    except Exception:
        return None


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()[:16]


def main():
    try:
        sys.stdout.reconfigure(errors='replace')
    except Exception:
        pass

    ap = argparse.ArgumentParser()
    ap.add_argument('bindir', help='каталог bin пакета, который уходит игрокам')
    ap.add_argument('--note', default='', help='пометка: что это за сборка')
    args = ap.parse_args()

    if not os.path.isdir(args.bindir):
        print('нет такого каталога:', args.bindir)
        return 1

    stamp = time.strftime('%Y%m%d_%H%M')
    dst = os.path.join(SYMROOT, stamp)
    os.makedirs(dst, exist_ok=True)

    rows, total = [], 0
    for f in sorted(os.listdir(args.bindir)):
        low = f.lower()
        # Наши модули и карты линковки. Системные и сторонние библиотеки не нужны: их стек всё
        # равно не разбирается, а места они занимают втрое больше остального.
        if not (low.startswith(('xr', 'da_')) and low.endswith(('.dll', '.exe', '.map'))):
            continue
        src = os.path.join(args.bindir, f)
        shutil.copy2(src, os.path.join(dst, f))
        size = os.path.getsize(src)
        total += size
        base = pe_image_base(src) if low.endswith(('.dll', '.exe')) else None
        rows.append('%-24s %11d  %s  база %s' % (f, size, sha(src),
                                                 hex(base) if base else '-'))

    manifest = os.path.join(dst, 'ОПИСЬ.txt')
    with open(manifest, 'w', encoding='utf-8') as w:
        w.write('снимок символов пакета\n')
        w.write('дата: %s\n' % time.strftime('%Y-%m-%d %H:%M'))
        w.write('источник: %s\n' % os.path.abspath(args.bindir))
        if args.note:
            w.write('пометка: %s\n' % args.note)
        w.write('\nимя                          размер  sha256(16)        база\n')
        w.write('\n'.join(rows) + '\n')
        w.write('\nКак пользоваться:\n')
        w.write('  python da_port/tools/symbolicate.py <лог игрока> --symbols "%s"\n' % dst)

    print('снимок: %s' % dst)
    print('файлов: %d, объём: %.1f МБ' % (len(rows), total / 1048576))
    print('опись:  %s' % manifest)
    return 0


if __name__ == '__main__':
    sys.exit(main())
