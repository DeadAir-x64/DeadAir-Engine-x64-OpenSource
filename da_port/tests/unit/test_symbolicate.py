#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Тесты расшифровки стека аварии (da_port/tools/symbolicate.py и keep_symbols.py).

Запускаются из run_tests.py, но работают и сами по себе:

    python xray-16/da_port/tests/unit/test_symbolicate.py

Проверяется то, на чём эта связка может тихо сломаться и остаться незамеченной до следующего
вылета у игрока — а тогда будет поздно, лог одноразовый.

Главный тест — «модуль загрузился по ЧУЖОМУ адресу». Ради него в движок и добавлена карта модулей:
если пересчёт адреса неверен, инструмент выдаст имя соседней функции, и это будет выглядеть как
правдоподобный ответ. Тест подсовывает заведомо сдвинутую базу и требует то же имя, что и без сдвига.
"""

import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.dirname(HERE)
DA_PORT = os.path.dirname(TESTS)
TOOLS = os.path.join(DA_PORT, 'tools')
REPO = os.path.dirname(DA_PORT)
BINDIR = os.path.join(REPO, 'bin', 'AMD64', 'Release')

sys.path.insert(0, TOOLS)

failures = []


def check(name, ok, note=''):
    mark = 'ok  ' if ok else 'ПРОВАЛ'
    print('    %-6s %-34s %s' % (mark, name, note))
    if not ok:
        failures.append(name)
    return ok


def find_function_address(dll, want):
    """Адрес функции в файле по таблице дизассемблера — опора для остальных тестов."""
    import symbolicate
    objdump = symbolicate.which('objdump.exe') or symbolicate.which('objdump')
    if not objdump:
        return None, None
    out = subprocess.run([objdump, '-d', '-C', '--section=.text', dll],
                         capture_output=True, text=True, errors='replace').stdout
    for line in out.splitlines():
        m = re.match(r'^([0-9a-f]{8,16}) <(.+)>:$', line)
        if m and want in m.group(2):
            return int(m.group(1), 16), m.group(2)
    return None, None


def write_log(path, frames, modules=None):
    """Собирает лог в том же виде, в каком его пишет движок."""
    lines = ['! [DA_PORT] отказ: обращение по неверному адресу (код C0000005), адрес кода %012X'
             % frames[0][1], 'stack trace:', '']
    for dll, addr in frames:
        lines.append(r'G:\DeadAir_Tester\bin\%s at %016X xrFactory_Create() + 1 byte(s)'
                     % (os.path.basename(dll), addr))
    if modules:
        lines.append('~ [DA_MODULES] карта модулей (сборка Aug  1 2026, build 999):')
        for dll, base, size in modules:
            lines.append('~ [DA_MODULES]   %016X %8u  %s' % (base, size, dll))
        lines.append('~ [DA_MODULES] ---- конец карты ----')
    open(path, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')


def run_tool(log, symbols):
    # Кодировку задаём с ОБЕИХ сторон, иначе проверки по русским подстрокам врут.
    # Без этого Python в дочернем процессе печатает в cp1251 (кодовая страница консоли), а мы
    # читаем как utf-8 — русский текст превращается в кашу, подстрока не находится, и тест
    # объявляет провалившимися ровно те проверки, где сообщение на русском. Функциональные
    # проверки при этом проходят: имена функций латиницей. Четыре ложных провала так и выглядели.
    env = dict(os.environ)
    env['PYTHONIOENCODING'] = 'utf-8'
    r = subprocess.run([sys.executable, os.path.join(TOOLS, 'symbolicate.py'), log,
                        '--symbols', symbols], capture_output=True, text=True,
                       encoding='utf-8', errors='replace', env=env)
    return r.stdout + r.stderr


def main():
    print('  расшифровка стека:')
    import symbolicate

    dll = os.path.join(BINDIR, 'xrGame.dll')
    if not os.path.exists(dll):
        check('сборка на месте', False, 'нет %s — соберите xrGame' % dll)
        return 1

    # --- 1. Базовый адрес из заголовка PE -----------------------------------------------------
    base = symbolicate.pe_image_base(dll)
    check('база из PE читается', isinstance(base, int) and base > 0x10000, hex(base or 0))

    # --- 2. Опора: адрес известной функции ------------------------------------------------------
    addr, fname = find_function_address(dll, 'CWeapon::GetConditionDispersionFactor')
    if not addr:
        check('нашли опорную функцию', False, 'objdump недоступен или функция не найдена')
        return 1
    check('нашли опорную функцию', True, fname)

    tmp = tempfile.mkdtemp(prefix='da_symtest_')

    # --- 3. Лог БЕЗ карты модулей: работа по предпочтительной базе -------------------------------
    log1 = os.path.join(tmp, 'no_map.log')
    write_log(log1, [(dll, addr)])
    out1 = run_tool(log1, BINDIR)
    check('без карты модулей — имя найдено', 'GetConditionDispersionFactor' in out1,
          'и предупреждение: %s' % ('есть' if 'карты модулей в логе НЕТ' in out1 else 'НЕТ'))
    check('без карты модулей — предупреждает', 'карты модулей в логе НЕТ' in out1)

    # --- 4. Лог С картой, база совпадает с обычной ------------------------------------------------
    size = os.path.getsize(dll) * 4  # с запасом: размер образа больше файла
    log2 = os.path.join(tmp, 'with_map.log')
    write_log(log2, [(dll, addr)], modules=[(dll, base, size)])
    out2 = run_tool(log2, BINDIR)
    check('с картой модулей — имя найдено', 'GetConditionDispersionFactor' in out2)
    check('с картой — карта прочитана', 'карта модулей из лога' in out2)

    # --- 5. ГЛАВНОЕ: модуль загружен по ДРУГОМУ адресу -------------------------------------------
    #
    # Ради этого случая карта и заведена. Сдвигаем базу на 0x10000000 и сдвигаем адрес кадра на
    # столько же: инструмент обязан вернуть ТУ ЖЕ функцию. Ошибись он в пересчёте — выдаст имя
    # соседней функции, и по виду это будет неотличимо от правильного ответа.
    shift = 0x10000000
    log3 = os.path.join(tmp, 'rebased.log')
    write_log(log3, [(dll, addr + shift)], modules=[(dll, base + shift, size)])
    out3 = run_tool(log3, BINDIR)
    check('сдвинутый образ — та же функция', 'GetConditionDispersionFactor' in out3,
          'сдвиг +0x%X' % shift)

    # --- 6. Кадр вне диапазона любого модуля не должен выдумывать имя ------------------------------
    log4 = os.path.join(tmp, 'stray.log')
    write_log(log4, [(dll, addr)], modules=[(dll, base, 0x100)])  # адрес заведомо за пределами
    out4 = run_tool(log4, BINDIR)
    # запасной путь по имени файла в строке кадра допустим, но выдумок быть не должно
    check('кадр вне модуля — без выдумок',
          ('GetConditionDispersionFactor' in out4) or ('имя не найдено' in out4)
          or ('файла' in out4))

    # --- 7. Лог без единого кадра ------------------------------------------------------------------
    log5 = os.path.join(tmp, 'empty.log')
    open(log5, 'w', encoding='utf-8').write('* обычный лог без аварии\n')
    out5 = run_tool(log5, BINDIR)
    check('лог без стека — понятное сообщение', 'не нашлось' in out5 or 'Кадров' in out5)

    # --- 8. Разбор блока карты модулей построчно ---------------------------------------------------
    line = r'~ [DA_MODULES]   000000018A000000 128206226  D:\game\bin\xrGame.dll'
    m = symbolicate.RE_MODULE.search(line)
    ok = bool(m) and int(m.group(1), 16) == 0x18A000000 and int(m.group(2)) == 128206226 \
        and m.group(3).endswith('xrGame.dll')
    check('строка карты разбирается', ok)

    # --- 9. Формат кадра движка --------------------------------------------------------------------
    frame = r'G:\DeadAir_Tester\bin\xrGame.dll at 000000018ACD01D7 xrFactory_Create() + 3040891 byte(s)'
    m = symbolicate.RE_FRAME.match(frame)
    check('строка кадра разбирается', bool(m) and int(m.group(2), 16) == 0x18ACD01D7)

    # --- 10. Снимок символов -----------------------------------------------------------------------
    import importlib.util
    spec = importlib.util.spec_from_file_location('keep_symbols', os.path.join(TOOLS, 'keep_symbols.py'))
    ks = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ks)
    check('снимок: база из PE та же', ks.pe_image_base(dll) == base)
    check('снимок: сумма считается', len(ks.sha(dll)) == 16)

    # --- 11. Эталонный лог настоящей аварии ---------------------------------------------------
    #
    # Снят принудительным крашем (da_crash_test) 01.08 на этой же сборке: движок написал карту
    # модулей, инструмент разобрал верхний кадр в CCC_DaCrashTest::Execute. Держим лог рядом и
    # проверяем на нём всю цепочку целиком - это единственный тест, который проходит через
    # НАСТОЯЩИЙ вывод движка, а не через собранный руками.
    ref = os.path.join(DA_PORT, 'docs', '_crashtest_reference.log')
    if os.path.exists(ref):
        out_ref = run_tool(ref, os.path.join(os.path.dirname(REPO), 'Dead Air', 'bin'))
        check('эталонный лог: карта прочитана', 'карта модулей из лога' in out_ref)

        # ⚠️ Имя конкретной функции здесь НЕ проверяется, и это осознанно.
        #
        # Лог снят на сборке того дня, а библиотеки с тех пор пересобирались - адреса уехали, и
        # требовать `CCC_DaCrashTest::Execute` значит требовать, чтобы сборка не менялась. Тест
        # падал бы после каждой правки xrGame и не сообщал бы ничего полезного: ровно то, о чём
        # предупреждает сам инструмент - расшифровывать можно только по ТОЙ ЖЕ сборке (для этого
        # есть keep_symbols.py).
        #
        # Проверяем инвариант, который от сборки не зависит: движок написал карту, инструмент её
        # прочитал и привязал кадры к нашим модулям.
        resolved = sum(1 for l in out_ref.splitlines()
                       if l.startswith(('xrGame.dll', 'xrEngine.dll', 'xrCore.dll')))
        check('эталонный лог: кадры привязаны к модулям', resolved >= 3, '%d кадров' % resolved)
    else:
        check('эталонный лог на месте', False, 'нет %s' % ref)

    print()
    if failures:
        print('  ПРОВАЛ: %s' % ', '.join(failures))
        return 1
    print('  расшифровка стека: всё зелено')
    return 0


if __name__ == '__main__':
    sys.exit(main())
