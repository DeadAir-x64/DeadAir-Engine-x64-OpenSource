#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Ремонтные наборы: каждый набор обязан быть в списке, который открывает окно ремонта.

Запускается из run_tests.py, но работает и сам по себе:

    python xray-16/da_port/tests/unit/test_repair_kits.py

ЗАЧЕМ ЭТОТ ТЕСТ СУЩЕСТВУЕТ. Окно ремонта открывает itms_manager по белому списку
`[repair_mod_tools]` в `configs/plugins/itms_manager.ltx`. Секция вне списка означает не «ремонт
не сработал», а гораздо хуже: движок уже съел предмет (`max_uses = 1`, `remove_after_use = true`),
окно не открылось, и набор исчезает со звуком применения. Ни ошибки, ни строки в логе.

Так и вышло: мы завели 17 секций-ступеней (`toolkit_p_2`, `helmet_repair_kit_2` и прочие — у
автора наборы помечены `;xN`, но самих секций он не создал), а в список их вписать забыли.
Дорогие наборы молча пропадали при использовании.

Признак набора — `repair_add_condition` (сколько износа лечит). Именно он, а НЕ `repair_type`:
`repair_type` стоит и на самих стволах, там он объявляет класс оружия (`pistol`, `rifle`), и по
нему в универсум попадает половина оружейных конфигов.

Пустая тара `_0` сюда не относится намеренно: это не набор, а запчасть (`class = S_PDA`), у неё
есть только `repair_part_bonus`.
"""

import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.dirname(HERE)
# Соглашение о путях — то же, что в run_tests.py: REPO = …/xray-16, GAME = …/Dead Air/Dead Air.
REPO = os.path.dirname(os.path.dirname(TESTS))
GAME = os.path.join(os.path.dirname(REPO), 'Dead Air')

GAME_CONFIGS = os.path.join(GAME, 'gamedata', 'configs')
# Распакованные движком конфиги Test-копии — полная поверхность мода, включая то, чего нет в loose.
PACK_CONFIGS = os.path.join(os.path.dirname(os.path.dirname(REPO)), 'Dead Air Test',
                            'Dead Air', 'appdata', 'logs', 'vfs_configs')

SECTION_RE = re.compile(r'^\[([^\]]+)\]\s*(?::\s*(.+))?$')

failures = []


def check(name, ok, note=''):
    print('  [%s] %s%s' % ('ok' if ok else 'ПРОВАЛ', name, (' — ' + note) if note else ''))
    if not ok:
        failures.append(name)


def load_configs(root, db):
    """Секция -> {родители, ключи}. Вызывается дважды: пакет, потом loose (loose перекрывает)."""
    if not os.path.isdir(root):
        return 0
    files = 0
    for dirpath, _, filenames in os.walk(root):
        for fn in filenames:
            if not fn.lower().endswith('.ltx'):
                continue
            files += 1
            cur = None
            try:
                txt = io.open(os.path.join(dirpath, fn), encoding='cp1251', errors='replace').read()
            except OSError:
                continue
            for line in txt.split('\n'):
                s = line.split(';')[0].strip()
                if not s:
                    continue
                m = SECTION_RE.match(s)
                if m:
                    cur = m.group(1).strip()
                    parents = [x.strip() for x in (m.group(2) or '').split(',') if x.strip()]
                    db[cur] = {'parents': parents, 'keys': {}}
                    continue
                if cur and '=' in s:
                    k, v = s.split('=', 1)
                    db[cur]['keys'][k.strip()] = v.strip()
    return files


def effective(db, sec, key, depth=0):
    """Значение ключа с учётом наследования `[секция]:родитель` — ступени наследуют всё от базы."""
    if depth > 12 or sec not in db:
        return None
    node = db[sec]
    if key in node['keys']:
        return node['keys'][key]
    for parent in node['parents']:
        got = effective(db, parent, key, depth + 1)
        if got is not None:
            return got
    return None


def whitelist(db_path, block):
    """Содержимое [block] как список имён. Берём loose, если он есть: он перекрывает архив."""
    for root in (GAME_CONFIGS, PACK_CONFIGS):
        path = os.path.join(root, db_path)
        if not os.path.isfile(path):
            continue
        out, inside = [], False
        txt = io.open(path, encoding='cp1251', errors='replace').read()
        for line in txt.split('\n'):
            # ⚠️ Комментарий снимаем ДО разбора заголовка. У этого списка он хвостовой:
            # `[repair_mod_tools]   ;Add sections that will open repair ui` — со строгим якорем `$`
            # такая строка секцией не признаётся, блок «не начинается», и тест объявляет пропавшими
            # ВСЕ наборы разом. Ровно на это он и попался при первом запуске.
            s = line.split(';')[0].strip()
            if not s:
                continue
            m = SECTION_RE.match(s)
            if m:
                inside = (m.group(1).strip() == block)
                continue
            if inside:
                out.append(s.split('=')[0].strip())
        return out, path
    return None, None


def main():
    print('Ремонтные наборы: белый список окна ремонта')

    db = {}
    n_pack = load_configs(PACK_CONFIGS, db)
    n_loose = load_configs(GAME_CONFIGS, db)  # вторым — перекрывает архивные значения

    if not db:
        # Без конфигов проверять нечего. Это не провал: у тестов может не быть установленной игры.
        print('  пропуск: конфиги не найдены (%s)' % GAME_CONFIGS)
        return 0

    check('конфиги прочитаны', n_pack + n_loose > 0, '%d файлов, %d секций' % (n_pack + n_loose, len(db)))

    allowed, src = whitelist(os.path.join('plugins', 'itms_manager.ltx'), 'repair_mod_tools')
    if allowed is None:
        check('найден itms_manager.ltx', False, 'ни в loose, ни в архивном дампе')
        print()
        print('  ПРОВАЛ: %s' % ', '.join(failures))
        return 1

    allowed_set = set(allowed)
    kits = sorted(s for s in db if effective(db, s, 'repair_add_condition'))

    check('набор ремонтных наборов не пуст', len(kits) > 0, '%d наборов' % len(kits))

    # ГЛАВНАЯ ПРОВЕРКА: набор без места в списке = предмет, исчезающий без окна.
    missing = [s for s in kits if s not in allowed_set]
    check('каждый набор открывает окно ремонта', not missing,
          ('вне списка: ' + ', '.join(missing[:8]) + ('…' if len(missing) > 8 else '')) if missing else '')

    # Обратная сторона: строка списка, которой не соответствует ни одна секция. Не роняет игру,
    # но означает либо опечатку, либо удалённый предмет — и то и другое стоит увидеть.
    ghosts = [s for s in allowed if s not in db]
    check('в списке нет ссылок на несуществующие секции', not ghosts,
          ('нет секций: ' + ', '.join(ghosts)) if ghosts else '')

    print()
    if failures:
        print('  ПРОВАЛ: %s' % ', '.join(failures))
        print('  список читался из: %s' % src)
        return 1
    print('  ремонтные наборы: всё зелено (%d наборов, %d строк в списке)' % (len(kits), len(allowed)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
