#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Проверка пакета перед выдачей тестерам.

    python check_package.py <корень пакета> [--game <наша установка>]

Проверяет то, на чём пакет уже ломался или мог сломаться молча. Каждая проверка — из настоящего
случая, а не из общих соображений:

  1. Нулевые библиотеки. Прерванная сборка оставляет файл нулевого размера, а ninja потом бодро
     сообщает "finished 0s". Пакет с таким файлом не запускается вовсе.
  2. Полнота набора. Список модулей взят из карты, которую движок печатает при аварии, — то есть из
     того, что он ДЕЙСТВИТЕЛЬНО грузит. Отдельно проверяется библиотека OpenGL: она не используется,
     но без неё exe не стартует.
  3. Данные. Loose-шейдеры, скрипты и конфиги пакета сверяются с нашей установкой: правки делаются в
     игре и молча не доезжают до пакета (так уже терялись правки TAA и свечения).
  4. Настройки. Ключи, значение которых мы задавали осознанно (потолок теней, метки реактивности,
     размытие при прицеливании), — чтобы тестер не получил чужие.
  5. Символы. Есть ли снимок под ЭТУ сборку: без него отчёт о вылете не расшифровать, а сделать
     снимок потом нельзя — библиотеки пересоберутся с другими адресами.
"""

import argparse
import hashlib
import os
import re
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
DA_PORT = os.path.dirname(TOOLS)
SYMROOT = os.path.join(DA_PORT, 'symbols')

# Модули из карты аварии — то, что движок грузит на самом деле.
REQUIRED = [
    'xrEngine.dll', 'xrCore.dll', 'xrGame.dll', 'xrRenderPC_R4.dll', 'xrAPI.dll', 'xrCDB.dll',
    'xrAICore.dll', 'xrLuabind.dll', 'xrLuaJIT.dll', 'xrNetServer.dll', 'xrMaterialSystem.dll',
    'xrScriptEngine.dll', 'xrSound.dll', 'xrParticles.dll', 'xrUICore.dll', 'xrPhysics.dll',
    'xrOPCODE.dll', 'xrODE.dll', 'xrGameSpy.dll',
]
# Не грузится в обычной игре, но без файла exe не стартует - проверено на пакете 30.07.
START_ONLY = ['xrRender_GL.dll']

EXPECT_SETTINGS = {
    'r__light_shadow_budget': 'st_opt_shadow_lights_off',
    'r__reactive_emissive': '0.',
    'r__reactive_transparent': '0.',
    'r2_exp_donttest_shad': 'off',
    'g_weapon_dof': '0',
}

problems = []
notes = []


def check(ok, name, note=''):
    print('  %-6s %-38s %s' % ('ok  ' if ok else 'ПРОВАЛ', name, note))
    if not ok:
        problems.append(name)
    return ok


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for c in iter(lambda: f.read(1 << 20), b''):
            h.update(c)
    return h.hexdigest()[:16]


def main():
    try:
        sys.stdout.reconfigure(errors='replace')
    except Exception:
        pass
    ap = argparse.ArgumentParser()
    ap.add_argument('package')
    ap.add_argument('--game', default=r'D:\Dead Air\Dead Air')
    args = ap.parse_args()

    pkg = args.package
    binp = os.path.join(pkg, 'bin')
    print('пакет: %s' % pkg)
    print('эталон данных: %s' % args.game)
    print()

    if not os.path.isdir(binp):
        print('нет каталога bin — это не пакет')
        return 1

    # 1. нулевые файлы
    zeros = [f for f in os.listdir(binp)
             if f.lower().endswith(('.dll', '.exe')) and os.path.getsize(os.path.join(binp, f)) == 0]
    check(not zeros, 'нет библиотек нулевого размера', ', '.join(zeros) if zeros else '')

    # 2. полнота набора
    missing = [m for m in REQUIRED if not os.path.exists(os.path.join(binp, m))]
    check(not missing, 'все нужные модули на месте', ', '.join(missing) if missing else
          '%d шт.' % len(REQUIRED))
    miss_start = [m for m in START_ONLY if not os.path.exists(os.path.join(binp, m))
                  or os.path.getsize(os.path.join(binp, m)) == 0]
    check(not miss_start, 'библиотека, нужная лишь для старта',
          ', '.join(miss_start) if miss_start else 'xrRender_GL.dll на месте')

    exe = [f for f in os.listdir(binp) if f.lower().endswith('.exe')]
    check(bool(exe), 'исполняемый файл есть', ', '.join(exe))

    # 3. данные
    game_data = os.path.join(args.game, 'gamedata')
    pkg_data = os.path.join(pkg, 'gamedata')
    diff, absent = [], []
    if os.path.isdir(game_data) and os.path.isdir(pkg_data):
        for dp, _, fs in os.walk(game_data):
            rel_dir = os.path.relpath(dp, game_data)
            top = rel_dir.split(os.sep)[0]
            if top not in ('shaders', 'scripts', 'configs'):
                continue
            for f in fs:
                if '.bak' in f.lower():
                    continue
                # Файлы, которые мод переписывает САМ во время игры: сверять их бессмысленно, они
                # расходятся после первого же запуска. Поймано 01.08: `atmosfear_options.ltx`
                # разошёлся на одну строку с погодой после проверочного запуска с флэшки.
                if f.lower() in ('axr_options.ltx', 'atmosfear_options.ltx'):
                    continue
                a = os.path.join(dp, f)
                b = os.path.join(pkg_data, rel_dir, f)
                if not os.path.exists(b):
                    absent.append(os.path.join(rel_dir, f))
                elif open(a, 'rb').read() != open(b, 'rb').read():
                    diff.append(os.path.join(rel_dir, f))
        check(not absent, 'все наши файлы данных доехали',
              '%d не доехало: %s' % (len(absent), ', '.join(absent[:3])) if absent else '')
        check(not diff, 'данные пакета совпадают с нашими',
              '%d разошлось: %s' % (len(diff), ', '.join(diff[:3])) if diff else '')
    else:
        notes.append('gamedata не сверена: нет каталога')

    # 4. настройки
    ltx = os.path.join(pkg, 'appdata', 'user.ltx')
    if os.path.exists(ltx):
        raw = open(ltx, 'rb').read().decode('cp1251', 'replace')
        bad = []
        for k, v in EXPECT_SETTINGS.items():
            m = re.search(r'^%s\s+(\S+)\s*$' % re.escape(k), raw, re.M)
            if not m:
                bad.append('%s отсутствует' % k)
            elif m.group(1) != v:
                bad.append('%s = %s (ждали %s)' % (k, m.group(1), v))
        check(not bad, 'настройки выставлены как решали', '; '.join(bad) if bad else
              '%d ключей' % len(EXPECT_SETTINGS))
    else:
        check(False, 'user.ltx на месте', 'нет %s' % ltx)

    # 5. файлы «только для чтения» — мод не сможет сохранить свои настройки
    #
    # Настоящий случай 01.08: `axr_options.ltx` приехал в пакет с атрибутом ReadOnly (один файл из
    # 791), и мод при каждом запуске писал в лог "Can't create file ... Permission denied". Настройки
    # мода у игрока не сохранялись вообще, а выглядело это как проблема с правами на его машине.
    ro = []
    for dp, _, fs in os.walk(os.path.join(pkg, 'gamedata')):
        for f in fs:
            full = os.path.join(dp, f)
            try:
                if not os.access(full, os.W_OK):
                    ro.append(os.path.relpath(full, pkg))
            except OSError:
                pass
    check(not ro, 'нет файлов «только для чтения»',
          '%d шт., первый: %s' % (len(ro), ro[0]) if ro else '')

    # 6. снимок символов под ЭТУ сборку
    key = {}
    for m in REQUIRED[:4]:
        p = os.path.join(binp, m)
        if os.path.exists(p):
            key[m] = sha(p)
    found = None
    if os.path.isdir(SYMROOT):
        for snap in sorted(os.listdir(SYMROOT), reverse=True):
            d = os.path.join(SYMROOT, snap)
            if all(os.path.exists(os.path.join(d, m)) and sha(os.path.join(d, m)) == h
                   for m, h in key.items()):
                found = snap
                break
    check(found is not None, 'снимок символов под эту сборку',
          found if found else 'нет — сделайте keep_symbols.py ДО выдачи')

    print()
    for n in notes:
        print('  примечание: %s' % n)
    if problems:
        print('ПРОВАЛ: %s' % ', '.join(problems))
        return 1
    print('пакет готов к выдаче')
    return 0


if __name__ == '__main__':
    sys.exit(main())
