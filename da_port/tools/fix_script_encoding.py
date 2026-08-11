#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Сведение файла к ОДНОЙ кодировке.

Скрипты мода лежат частью в cp1251, частью в UTF-8 — это нормально, движок читает их как байты.
Опасна СМЕСЬ в одном файле: дописали UTF-8 комментарий в cp1251-файл (или наоборот), и русский
текст начинает разъезжаться, а редакторы спорят о кодировке при каждом сохранении.

Разбор идёт побайтово: сначала пробуем прочесть последовательность как UTF-8, не вышло — читаем
один байт как cp1251. Так восстанавливаются обе половины смешанного файла.

Запуск:
    python fix_script_encoding.py <файл> [<файл> ...] --to utf-8
    python fix_script_encoding.py <файл> --check          # только доложить, не трогать
"""

import argparse
import os
import re
import sys


def sniff(data):
    """Что в файле: 'utf-8', 'cp1251' или 'смесь'."""
    try:
        data.decode('utf-8')
        return 'utf-8'
    except UnicodeDecodeError:
        pass
    # признак UTF-8-вставок в не-UTF-8 файле: ведущие байты кириллицы UTF-8
    if re.search(rb'[\xd0\xd1][\x80-\xbf]', data):
        return 'смесь'
    return 'cp1251'


def decode_mixed(data):
    """Читает файл, где перемешаны UTF-8 и cp1251."""
    out = []
    i, n = 0, len(data)
    while i < n:
        b = data[i]
        if b < 0x80:
            out.append(chr(b))
            i += 1
            continue
        # пробуем UTF-8: длина последовательности по ведущему байту
        if 0xC2 <= b <= 0xDF:
            size = 2
        elif 0xE0 <= b <= 0xEF:
            size = 3
        elif 0xF0 <= b <= 0xF4:
            size = 4
        else:
            size = 0
        if size and i + size <= n:
            chunk = data[i:i + size]
            try:
                out.append(chunk.decode('utf-8'))
                i += size
                continue
            except UnicodeDecodeError:
                pass
        # не UTF-8 — значит cp1251
        out.append(data[i:i + 1].decode('cp1251'))
        i += 1
    return ''.join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('--to', default='utf-8', choices=('utf-8', 'cp1251'))
    ap.add_argument('--check', action='store_true', help='только доложить')
    args = ap.parse_args()

    bad = 0
    for path in args.files:
        if not os.path.isfile(path):
            print('нет файла: %s' % path)
            bad += 1
            continue
        data = open(path, 'rb').read()
        kind = sniff(data)
        name = os.path.basename(path)
        if kind != 'смесь':
            print('%-32s %s — трогать нечего' % (name, kind))
            continue

        bad += 1
        if args.check:
            print('%-32s СМЕСЬ кодировок' % name)
            continue

        text = decode_mixed(data)
        open(path, 'wb').write(text.encode(args.to))
        after = sniff(open(path, 'rb').read())
        print('%-32s смесь -> %s (проверка: %s)' % (name, args.to, after))

    return 1 if (args.check and bad) else 0


if __name__ == '__main__':
    sys.exit(main())
