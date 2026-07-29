#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Авто-тесты Lua-скриптов Dead Air.

Запуск:  python xray-16/da_port/tests/run_tests.py
Каталог запуска значения не имеет: все пути считаются от этого файла.

Что проверяется:
  syntax    — каждый скрипт разбирается тем же LuaJIT, что стоит в движке;
  hygiene   — байтовая гигиена наших loose-файлов (удвоенный CR, BOM, кодировка);
  globals   — глобалы, которые читаются, но нигде не присваиваются (класс бага `warn`);
  engine    — вызовы level.* и game.* против биндингов порта;
  options   — ключи opt_* без единого читателя;
  unit      — поведение конкретных функций на моках движка.

Интерпретатор собирается из Externals/LuaJIT при первом запуске (нужен mingw32-make из msys2)
и кладётся в tests/bin — это ТА ЖЕ версия 2.1.0-beta3, что линкуется в движок.
"""

import os
import re
import subprocess
import sys

TESTS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(TESTS))            # …/xray-16
GAME = os.path.join(os.path.dirname(REPO), 'Dead Air')     # …/Dead Air/Dead Air
BIN = os.path.join(TESTS, 'bin')
LUAJIT = os.path.join(BIN, 'luajit.exe')

LOOSE_SCRIPTS = os.path.join(GAME, 'gamedata', 'scripts')
DUMP_SCRIPTS = os.path.join(GAME, 'appdata', 'logs', 'vfs_scripts')
OPTIONS_LTX = os.path.join(GAME, 'gamedata', 'configs', 'atmosfear_options.ltx')
LEVEL_SCRIPT_CPP = os.path.join(REPO, 'src', 'xrGame', 'level_script.cpp')

results = []


def report(name, ok, note=''):
    results.append((name, ok))
    mark = 'ok  ' if ok else 'ПРОВАЛ'
    print('  %-6s %-28s %s' % (mark, name, note))


# ---------------------------------------------------------------------------
# Интерпретатор
# ---------------------------------------------------------------------------
def ensure_luajit():
    """Возвращает путь к luajit.exe, при необходимости собирая его."""
    if os.path.exists(LUAJIT):
        return LUAJIT

    src = os.path.join(REPO, 'Externals', 'LuaJIT')
    if not os.path.isdir(src):
        return None

    print('  … собираю LuaJIT из Externals/LuaJIT (один раз)')
    import shutil
    import tempfile
    tmp = tempfile.mkdtemp(prefix='luajit_')
    build = os.path.join(tmp, 'LuaJIT')
    shutil.copytree(src, build)

    env = dict(os.environ)
    env['PATH'] = r'C:\msys64\mingw64\bin;' + env.get('PATH', '')
    rc = subprocess.run(['mingw32-make', '-j8'], cwd=build, env=env,
                        capture_output=True, text=True, errors='replace')
    if rc.returncode != 0:
        print('    не собралось:', (rc.stderr or rc.stdout)[-400:])
        shutil.rmtree(tmp, ignore_errors=True)
        return None

    os.makedirs(BIN, exist_ok=True)
    for f in ('luajit.exe', 'lua51.dll'):
        shutil.copy2(os.path.join(build, 'src', f), os.path.join(BIN, f))
    # модули jit/*.lua нужны для разбора байт-кода (jit.bc)
    shutil.copytree(os.path.join(build, 'src', 'jit'), os.path.join(BIN, 'jit'),
                    dirs_exist_ok=True)
    shutil.rmtree(tmp, ignore_errors=True)
    return LUAJIT if os.path.exists(LUAJIT) else None


def run_lua(script, extra_env=None):
    env = dict(os.environ)
    env['LUA_PATH'] = ';'.join([
        os.path.join(BIN, '?.lua'),
        os.path.join(TESTS, 'unit', '?.lua'),
        os.path.join(TESTS, 'lint', '?.lua'),
        '',
    ])
    env['DA_SCRIPT_DIRS'] = ';'.join(p for p in (LOOSE_SCRIPTS, DUMP_SCRIPTS) if os.path.isdir(p))
    env['DA_SCRIPTS'] = LOOSE_SCRIPTS
    if extra_env:
        env.update(extra_env)
    rc = subprocess.run([LUAJIT, script], env=env, capture_output=True, text=True,
                        errors='replace', cwd=TESTS)
    out = (rc.stdout or '') + (rc.stderr or '')
    sys.stdout.write(out if out.endswith('\n') or not out else out + '\n')
    return rc.returncode == 0


# ---------------------------------------------------------------------------
# Проверка: байтовая гигиена
# ---------------------------------------------------------------------------
def check_hygiene():
    """Удвоенный возврат каретки, BOM, битая кодировка.

    Удвоенный CR — не косметика: LuaJIT считает `\\r\\r\\n` за ДВА перевода строки, поэтому номера
    строк в сообщениях об ошибках уезжают вдвое, а diff против оригинала показывает файл целиком
    изменённым. Однажды это уже случилось с семью нашими файлами.
    """
    problems = []
    for fn in sorted(os.listdir(LOOSE_SCRIPTS)):
        if not fn.endswith('.script'):
            continue
        data = open(os.path.join(LOOSE_SCRIPTS, fn), 'rb').read()
        if b'\r\r\n' in data:
            problems.append('%s: удвоенный возврат каретки (%d строк)' % (fn, data.count(b'\r\r\n')))
        if data.startswith(b'\xef\xbb\xbf'):
            problems.append('%s: BOM в начале файла' % fn)
        # Скрипты мода лежат в двух кодировках: часть в cp1251, часть в UTF-8 — это нормально,
        # движок читает их как байты. Опасна СМЕСЬ: если дописать UTF-8 комментарий в cp1251-файл,
        # русский текст в нём начнёт разъезжаться, а редакторы будут спорить о кодировке файла.
        try:
            data.decode('utf-8')
        except UnicodeDecodeError:
            if re.search(rb'[\xd0\xd1][\x80-\xbf]', data):
                problems.append('%s: смесь cp1251 и UTF-8 в одном файле' % fn)
    for p in problems:
        print('         ' + p)
    report('hygiene', not problems, 'файлов: %d' % len([f for f in os.listdir(LOOSE_SCRIPTS)
                                                        if f.endswith('.script')]))
    return not problems


# ---------------------------------------------------------------------------
# Проверка: вызовы движка против биндингов порта
# ---------------------------------------------------------------------------
def check_engine_calls():
    """Каждый level.X, который зовут скрипты, должен быть зарегистрирован в level_script.cpp.

    Ловит расхождение «мод зовёт — порт не отдал», то есть ровно тот класс дыр, из-за которых
    механика молча не работает.
    """
    if not os.path.exists(LEVEL_SCRIPT_CPP):
        report('engine', True, 'пропущено: нет исходников порта')
        return True

    bindings = set(re.findall(r'def\("([a-z_0-9]+)"', open(LEVEL_SCRIPT_CPP, encoding='utf-8',
                                                           errors='replace').read()))
    used = {}
    src_dir = DUMP_SCRIPTS if os.path.isdir(DUMP_SCRIPTS) else LOOSE_SCRIPTS
    for fn in sorted(os.listdir(src_dir)):
        if not fn.endswith('.script'):
            continue
        text = open(os.path.join(src_dir, fn), encoding='utf-8', errors='replace').read()
        text = re.sub(r'--[^\n]*', '', text)
        for name in re.findall(r'\blevel\.([a-z_][a-z_0-9]*)', text):
            used.setdefault(name, set()).add(fn)

    known_missing = load_baseline('known_missing_bindings.txt')
    missing = {n: f for n, f in used.items() if n not in bindings and n not in known_missing}
    for name, files in sorted(missing.items()):
        print('         level.%s  <- %s' % (name, ', '.join(sorted(files))))
    report('engine', not missing, 'вызовов level.*: %d' % len(used))
    return not missing


# ---------------------------------------------------------------------------
# Проверка: настройки без читателя
# ---------------------------------------------------------------------------
def check_options():
    """Ключ opt_* есть в конфиге, а читать его некому.

    Так были мертвы предупреждения о выбросе и судьба NPC: меню писало, конфиг хранил, никто не
    читал. Заведомо мёртвые ключи перечислены в known_dead_options.txt с причиной.
    """
    if not os.path.exists(OPTIONS_LTX):
        report('options', True, 'пропущено: нет atmosfear_options.ltx')
        return True

    keys = sorted(set(re.findall(r'^\s*(opt_[a-z_0-9]+)', open(OPTIONS_LTX, encoding='utf-8',
                                                               errors='replace').read(),
                                 re.MULTILINE)))
    src_dir = DUMP_SCRIPTS if os.path.isdir(DUMP_SCRIPTS) else LOOSE_SCRIPTS
    corpus = {}
    for fn in os.listdir(src_dir):
        if fn.endswith('.script'):
            corpus[fn] = open(os.path.join(src_dir, fn), encoding='utf-8', errors='replace').read()
    # loose перекрывает архивную версию — читаем её же
    for fn in os.listdir(LOOSE_SCRIPTS):
        if fn.endswith('.script'):
            corpus[fn] = open(os.path.join(LOOSE_SCRIPTS, fn), encoding='utf-8',
                              errors='replace').read()

    known_dead = load_baseline('known_dead_options.txt')
    dead = []
    for key in keys:
        readers = [fn for fn, text in corpus.items()
                   if ('"%s"' % key) in text and 'r_value' in text]
        # ключи вида opt_<уровень>_period_* читаются динамически: "opt_"..level.."_period_good"
        dynamic = re.match(r'opt_.+_period_(good|bad)(_length)?$', key)
        if not readers and not dynamic and key not in known_dead:
            dead.append(key)
    for key in dead:
        print('         %s — ни одного читателя' % key)
    report('options', not dead, 'ключей: %d' % len(keys))
    return not dead


def load_baseline(name):
    path = os.path.join(TESTS, 'lint', name)
    if not os.path.exists(path):
        return set()
    out = set()
    for line in open(path, encoding='utf-8'):
        line = line.split('--')[0].strip()
        if line:
            out.add(line)
    return out


# ---------------------------------------------------------------------------
def main():
    print('Тесты Lua-скриптов Dead Air')
    print('  игра:    %s' % GAME)
    print('  скрипты: %s' % LOOSE_SCRIPTS)
    print()

    if not os.path.isdir(LOOSE_SCRIPTS):
        print('  не найден каталог скриптов игры — проверять нечего')
        return 1

    check_hygiene()
    check_engine_calls()
    check_options()

    if not ensure_luajit():
        print('  ПРОВАЛ luajit                       не собран, проверки на Lua пропущены')
        results.append(('luajit', False))
    else:
        ok = run_lua(os.path.join(TESTS, 'lint', 'syntax.lua'))
        results.append(('syntax', ok))
        ok = run_lua(os.path.join(TESTS, 'lint', 'globals.lua'),
                     {'DA_KNOWN_GLOBALS': os.path.join(TESTS, 'lint', 'known_globals.txt'),
                      'DA_KNOWN_MOD_BUGS': os.path.join(TESTS, 'lint', 'known_mod_bugs.txt')})
        results.append(('globals', ok))
        unit_dir = os.path.join(TESTS, 'unit')
        for fn in sorted(os.listdir(unit_dir)):
            if fn.startswith('test_') and fn.endswith('.lua'):
                ok = run_lua(os.path.join(unit_dir, fn))
                results.append((fn[:-4], ok))

    failed = [n for n, ok in results if not ok]
    print()
    if failed:
        print('ПРОВАЛ: %d из %d (%s)' % (len(failed), len(results), ', '.join(failed)))
        return 1
    print('всё зелено: %d проверок' % len(results))
    return 0


if __name__ == '__main__':
    sys.exit(main())
