#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Расшифровка стека аварии Dead Air x64: адреса из лога -> имена функций.

    python symbolicate.py <лог> [--symbols <каталог с .dll>] [--map <каталог с .map>]

Зачем. Движок пишет в лог голые адреса, а имя рядом с ними БЕСПОЛЕЗНО: там ближайший экспорт,
до настоящей функции могут быть мегабайты. 01.08 три отчёта от тестера не удалось разобрать
вообще - релиз собран с межмодульной оптимизацией, addr2line отвечает "<artificial>", а поиск по
ближайшему символу выдаёт заведомо чужое имя (на вылет в интерфейсе назвал деструктор состояния
псевдособаки). Этот скрипт закрывает вопрос тремя разными путями, от точного к грубому.

Как работает.
  1. Читает блок "[DA_MODULES]" - карту модулей, которую движок печатает в момент падения: база,
     размер, путь. Именно она позволяет пересчитать абсолютный адрес в смещение внутри модуля,
     даже если у нас на диске тот же файл загрузился бы по другому адресу.
  2. Для каждого кадра стека определяет модуль по диапазону [база, база+размер).
  3. Ищет имя, по очереди:
       - addr2line по отладочной информации (точно, но пусто при межмодульной оптимизации);
       - файл .map линковщика (таблица "адрес -> символ", составленная ПОСЛЕ всех слияний, поэтому
         оптимизация ей не мешает - ради этого карты и включены в релизной сборке);
       - ближайший символ из таблицы дизассемблера (грубо, помечается как приблизительное).

Если карты модулей в логе нет (лог от старой сборки) - работает по предпочтительному базовому
адресу из заголовка PE и честно об этом предупреждает.
"""

import argparse
import bisect
import os
import re
import subprocess
import gzip
import struct
import sys

RE_MODULE = re.compile(r'\[DA_MODULES\]\s+([0-9A-Fa-f]{8,16})\s+(\d+)\s+(.+?)\s*$')
RE_FRAME = re.compile(r'^\s*(.+?)\s+at\s+([0-9A-Fa-f]{8,16})\b')
RE_FAULT = re.compile(r'адрес кода\s+([0-9A-Fa-f]{8,16})')


def pe_image_base(path):
    """Предпочтительный базовый адрес из заголовка PE - запасной путь, если карты модулей нет."""
    try:
        with open(path, 'rb') as f:
            head = f.read(0x400)
        off = struct.unpack_from('<I', head, 0x3C)[0]
        magic = struct.unpack_from('<H', head, off + 24)[0]
        if magic == 0x20B:  # PE32+
            return struct.unpack_from('<Q', head, off + 24 + 24)[0]
        return struct.unpack_from('<I', head, off + 24 + 28)[0]
    except Exception:
        return None


def which(name):
    for d in os.environ.get('PATH', '').split(os.pathsep) + [r'C:\msys64\mingw64\bin']:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    return None


class Module:
    def __init__(self, name, base, size, path_in_log):
        self.name = name
        self.base = base
        self.size = size
        self.path_in_log = path_in_log
        self.local = None      # найденный у нас файл
        self.map_syms = None   # [(addr, name)] из .map
        self.dis_syms = None   # [(addr, name)] из дизассемблера

    def contains(self, addr):
        return self.size and self.base <= addr < self.base + self.size


def load_map(path):
    """Разбор карты линковщика GNU ld: строки вида '0x00000001800012a0   Class::method'.

    Принимает и сжатые карты (.map.gz): снимок символов (_archive_symbols.sh) хранит их именно так,
    88 МБ карт ужимаются до 5.6 МБ, и держать их можно бессрочно."""
    syms = []
    opener = gzip.open if path.endswith('.gz') else open
    try:
        with opener(path, 'rt', encoding='utf-8', errors='replace') as f:
            for line in f:
                m = re.match(r'\s*0x([0-9a-fA-F]{8,16})\s+(\S.*?)\s*$', line)
                if m:
                    name = m.group(2).strip()
                    # Отсев не-символов. Прежний сторож пропускал строки объектных файлов, если в
                    # пути были скобки, а у архивных членов вида "libfoo.a(bar.o)" они всегда есть.
                    # Такие записи засоряют таблицу и перекрывают настоящий символ при поиске по
                    # ближайшему адресу - то есть дают ЧУЖОЕ имя, что хуже отсутствия имени.
                    if not name or name.startswith('.'):
                        continue
                    if '/' in name or chr(92) in name:      # путь к объектному файлу
                        continue
                    if name.startswith('0x'):                  # размер секции, а не имя
                        continue
                    if '=' in name:                            # присваивание вида "__ImageBase = 0x..."
                        continue
                    if ' ' in name and '(' not in name:        # прочий служебный текст
                        continue
                    syms.append((int(m.group(1), 16), name))
    except OSError:
        return None
    syms.sort()
    return syms or None


def load_disasm_syms(dll):
    """Заголовки функций из дизассемблера - последний рубеж, имена приблизительные."""
    objdump = which('objdump.exe') or which('objdump')
    if not objdump:
        return None
    try:
        out = subprocess.run([objdump, '-d', '-C', dll], capture_output=True, text=True,
                             errors='replace', timeout=900).stdout
    except Exception:
        return None
    syms = []
    for line in out.splitlines():
        m = re.match(r'^([0-9a-f]{8,16}) <(.+)>:$', line)
        if m:
            syms.append((int(m.group(1), 16), m.group(2)))
    syms.sort()
    return syms or None


def lookup(syms, addr):
    if not syms:
        return None
    keys = [s[0] for s in syms]
    i = bisect.bisect_right(keys, addr) - 1
    if i < 0:
        return None
    return syms[i][1], addr - syms[i][0]


def addr2line(dll, rva_addr):
    """Возвращает (имя_функции|None, файл|None). Частичный ответ тоже ценен.

    В наших быстрых сборках отладочная информация неполная: имени функции нет, а ФАЙЛ есть - и
    файла обычно достаточно, чтобы понять слой. Раньше такой ответ отбрасывался целиком, потому
    что начинался с "??" - и мы теряли единственное, что там было полезного.
    """
    exe = which('addr2line.exe') or which('addr2line')
    if not exe:
        return None, None
    try:
        out = subprocess.run([exe, '-e', dll, '-f', '-C', '-p', hex(rva_addr)],
                             capture_output=True, text=True, errors='replace', timeout=60).stdout.strip()
    except Exception:
        return None, None
    if not out or '<artificial>' in out:
        return None, None
    m = re.match(r'^(.*?)\s+at\s+(.+?):(\d+|\?)\s*$', out)
    if not m:
        return None, None
    func = m.group(1).strip()
    src = m.group(2).strip()
    if func.startswith('??'):
        func = None
    if src.startswith('??'):
        src = None
    if src and m.group(3) != '?':
        src = '%s:%s' % (src, m.group(3))
    return func, src


def main():
    # Консоль Windows нередко в cp1251: не роняем инструмент из-за символа, который в неё
    # не лезет - это диагностика, она обязана работать даже в убогом терминале.
    try:
        sys.stdout.reconfigure(errors='replace')
    except Exception:
        pass
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--symbols', default=r'D:\Dead Air\xray-16\bin\AMD64\Release',
                    help='каталог с теми же .dll, что были у игрока')
    ap.add_argument('--map', default=None, help='каталог с .map линковщика (по умолчанию рядом с symbols)')
    args = ap.parse_args()

    text = open(args.log, encoding='utf-8', errors='replace').read().splitlines()

    # 1. карта модулей из лога
    modules = []
    for line in text:
        m = RE_MODULE.search(line)
        if m:
            path = m.group(3)
            modules.append(Module(os.path.basename(path), int(m.group(1), 16), int(m.group(2)), path))

    if modules:
        print('карта модулей из лога: %d шт.' % len(modules))
    else:
        print('!! карты модулей в логе НЕТ (лог от сборки до 01.08).')
        print('  Работаю по предпочтительному базовому адресу из PE — если образ был перемещён,')
        print('  имена будут неверными, и это никак не проверить.')

    # 2. привязка к нашим файлам
    mapdir = args.map or args.symbols
    for mod in modules:
        cand = os.path.join(args.symbols, mod.name)
        if os.path.exists(cand):
            mod.local = cand
            base = os.path.join(mapdir, os.path.splitext(mod.name)[0] + '.map')
            # снимок символов хранит карты сжатыми, каталог сборки — как есть
            for mp in (base, base + '.gz'):
                if os.path.exists(mp):
                    mod.map_syms = load_map(mp)
                    break

    # 3. кадры стека
    print()
    seen_any = False
    for line in text:
        mf = RE_FRAME.match(line.rstrip())
        if not mf:
            continue
        addr = int(mf.group(2), 16)
        mod = next((m for m in modules if m.contains(addr)), None)

        if mod is None:
            # запасной путь: по имени файла в самой строке кадра
            name = os.path.basename(mf.group(1))
            cand = os.path.join(args.symbols, name)
            if not os.path.exists(cand):
                continue
            base = pe_image_base(cand)
            if base is None:
                continue
            mod = Module(name, base, 1 << 40, cand)
            mod.local = cand

        if not mod.local:
            print('%-16s %012X  (файла %s у нас нет)' % (mod.name, addr, mod.name))
            continue

        rva = addr - mod.base
        seen_any = True

        # предпочтительный базовый адрес нужен, потому что символы в файле лежат от него
        prefer = pe_image_base(mod.local) or 0
        file_addr = prefer + rva

        func, src = addr2line(mod.local, file_addr)

        # Имя функции: сперва точное из отладочной информации, затем карта линковки, затем
        # ближайший символ. Последний путь честно помечается - на сборке с межмодульной
        # оптимизацией он выдаёт заведомо чужие имена.
        approx = False
        if not func:
            got = lookup(mod.map_syms, file_addr)
            if got:
                func = '%s + %d' % (got[0], got[1])
            else:
                if mod.dis_syms is None:
                    mod.dis_syms = load_disasm_syms(mod.local) or []
                got = lookup(mod.dis_syms, file_addr)
                if got:
                    func = '%s + %d' % (got[0], got[1])
                    # Если файл известен, ближайший символ почти наверняка тот самый: они сошлись
                    # независимо. Помечаем приблизительным только когда подтвердить нечем.
                    approx = src is None

        parts = [p for p in (func, src) if p]
        if not parts:
            print('%-16s %012X  имя не найдено' % (mod.name, addr))
            continue
        print('%-16s %012X  %s%s' % (mod.name, addr, '  '.join(parts),
                                     '   ~~ приблизительно' if approx else ''))

    if not seen_any:
        print('Кадров, привязанных к нашим модулям, не нашлось.')
        print('Проверь --symbols: там должны лежать ТЕ ЖЕ файлы, что были у игрока.')


if __name__ == '__main__':
    main()
