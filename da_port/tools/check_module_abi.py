# -*- coding: utf-8 -*-
"""Сверка экспортов и импортов внутри набора модулей.

ЗАЧЕМ. 12.08.2026 я выложил в игру одну свежесобранную xrGame.dll, оставив остальные модули от
прошлого выпуска. Игра не дошла даже до меню: соседний модуль импортировал из xrGame функцию
da_lp_record, которой в новой сборке там уже нет — она переехала в xrCore. Ни размер файла, ни
дата, ни «собралось без ошибок» этого не показывают: несоответствие вскрывается только загрузчиком
Windows, в момент запуска, окном «Точка входа не найдена».

Проверка механическая и занимает секунды: у каждого модуля берём таблицу импорта, у каждого —
таблицу экспорта, и смотрим, что всякий импорт из НАШЕГО модуля этим модулем действительно
экспортируется. Системные библиотеки (KERNEL32, msvcrt, libstdc++ и прочие) не наши — их не трогаем.

ПРИМЕНЕНИЕ:
    python check_module_abi.py                      # набор в игре
    python check_module_abi.py "D:/путь/к/bin"       # любой другой каталог

Код возврата 0 — набор согласован; 1 — есть несходящиеся импорты; 2 — не запустился objdump.
"""
import os
import re
import subprocess
import sys

DEFAULT_DIR = r"D:\Dead Air\Dead Air\bin"

# objdump из msys2; в PATH его может не быть, поэтому пробуем и полный путь.
OBJDUMP_CANDIDATES = ["objdump", r"C:\msys64\mingw64\bin\objdump.exe"]

RE_DLL_NAME = re.compile(r"^\s*DLL Name:\s*(\S+)\s*$")
RE_IMPORT = re.compile(r"^\s+[0-9a-fA-F]{4,}\s+(?:<none>|\d+)\s+[0-9a-fA-F]+\s+(\S+)\s*$")
RE_EXPORT = re.compile(r"^\s*\[\s*\d+\]\s*\+base\[\s*\d+\]\s+[0-9a-fA-F]+\s+(\S+)\s*$")


def find_objdump():
    for c in OBJDUMP_CANDIDATES:
        try:
            subprocess.run([c, "--version"], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, check=True)
            return c
        except (OSError, subprocess.CalledProcessError):
            continue
    return None


def dump(objdump, path):
    r = subprocess.run([objdump, "-p", path], stdout=subprocess.PIPE,
                       stderr=subprocess.DEVNULL)
    return r.stdout.decode("latin-1", "replace").splitlines()


def parse(lines):
    """-> (exports:set, imports:dict{dll_lower: set})"""
    exports, imports = set(), {}
    cur_dll = None
    in_export_table = False

    for ln in lines:
        m = RE_DLL_NAME.match(ln)
        if m:
            cur_dll = m.group(1).lower()
            imports.setdefault(cur_dll, set())
            in_export_table = False
            continue

        if "[Ordinal/Name Pointer] Table" in ln:
            in_export_table = True
            cur_dll = None
            continue

        if in_export_table:
            m = RE_EXPORT.match(ln)
            if m:
                exports.add(m.group(1))
            elif ln.strip() and not ln.startswith("\t") and not ln.startswith(" "):
                in_export_table = False
            continue

        if cur_dll:
            m = RE_IMPORT.match(ln)
            if m:
                imports[cur_dll].add(m.group(1))

    return exports, imports


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DIR
    objdump = find_objdump()
    if not objdump:
        print("objdump не найден — проверить нечем")
        return 2

    files = sorted(f for f in os.listdir(root)
                   if f.lower().endswith((".dll", ".exe")))
    if not files:
        print("в %s нет модулей" % root)
        return 2

    print("Сверка ABI набора: %s" % root)
    print("  модулей: %d\n" % len(files))

    exports, imports, sizes = {}, {}, {}
    for f in files:
        p = os.path.join(root, f)
        sizes[f.lower()] = os.path.getsize(p)
        e, i = parse(dump(objdump, p))
        exports[f.lower()] = e
        imports[f.lower()] = i

    present = set(exports)

    # Кого вообще грузят. Модуль, на который никто не ссылается и который не запускается сам,
    # в память не попадает — его несходящиеся импорты игре безразличны.
    #
    # 🪤 Иначе заслон кричит без причины: в bin с мая лежит OPCODE.dll из MSVC-эпохи, тянущая из
    # xrCore семь символов в чужом манглинге. Её не грузит никто, игра работает — но красная
    # строка в отчёте есть, и через неделю такой отчёт перестают читать целиком.
    referenced = set()
    for f in files:
        referenced |= set(imports[f.lower()])
    def is_loaded(name):
        return name in referenced or name.endswith(".exe")

    broken = 0
    dormant = []
    empty = [f for f in files if sizes[f.lower()] == 0]

    for f in files:
        for dll, syms in sorted(imports[f.lower()].items()):
            if dll not in present:      # системная или отсутствующая — не наша забота
                continue
            missing = sorted(s for s in syms if s not in exports[dll])
            if not missing:
                continue
            if not is_loaded(f.lower()):
                dormant.append((f, dll, len(missing)))
                continue
            broken += len(missing)
            print("  ПРОВАЛ %s импортирует из %s, а там этого нет:" % (f, dll))
            for s in missing[:12]:
                print("      %s" % s)
            if len(missing) > 12:
                print("      ... и ещё %d" % (len(missing) - 12))

    for f, dll, n in dormant:
        print("  внимание: %s не сходится с %s (%d символов), но его никто не грузит — "
              "мёртвый файл в каталоге" % (f, dll, n))

    if empty:
        print("  ПРОВАЛ модули НУЛЕВОГО размера: %s" % ", ".join(empty))

    if broken or empty:
        print("\nНАБОР НЕ СОГЛАСОВАН: несходящихся импортов %d, пустых модулей %d."
              % (broken, len(empty)))
        print("Так игра не запустится — загрузчик скажет «Точка входа не найдена».")
        return 1

    print("  ok  все импорты между нашими модулями находят свой экспорт")
    return 0


if __name__ == "__main__":
    sys.exit(main())
