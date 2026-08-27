#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Заслон перед выкладкой: одна команда, которая проверяет ВСЁ.

    python xray-16/da_port/tools/release_gate.py

Ноль на выходе — можно публиковать. Иначе печатается, что именно не сошлось.

⛔ Написан 11.08.2026, после выпуска, ушедшего тестерам НЕПОЛНЫМ. Движок уехал целиком, а из
двух дней правок по Lua — ничего: последний коммит игровых файлов был раньше самих правок.
Проверка «собралось и запустилось» это пропустила, потому что собралось и запустилось честно.
Отсюда правило: перед публикацией сверяются не действия, а СОДЕРЖИМОЕ — сборка, снимок, архив,
игровые файлы и каталог отгрузки, каждое с каждым.

Каталог обновления берётся из DA_UPDATE_REPO (по умолчанию D:/DeadAir_Update).
"""

import hashlib
import os
import re
import subprocess
import sys
import zipfile

TOOLS = os.path.dirname(os.path.abspath(__file__))
XRAY = os.path.dirname(os.path.dirname(TOOLS))              # …/xray-16
ROOT = os.path.dirname(XRAY)                                 # …/Dead Air
GAME = os.path.join(ROOT, 'Dead Air')
GAME_BIN = os.path.join(GAME, 'bin')
SYMBOLS = os.environ.get('DA_SYMBOLS', r'D:/DA_symbols')
UPDATE = os.environ.get('DA_UPDATE_REPO', r'D:/DeadAir_Update')

# Модули, которые собираем мы. Остальное в bin — чужая среда выполнения (MinGW, OpenAL, LuaJIT).
OUR_MODULES = ('xrCore', 'xrEngine', 'xrGame', 'xrAPI', 'xrCDB', 'xrAICore', 'xrLua',
               'xrRender', 'xrNetServer', 'xrSound', 'xrPhysics', 'xrParticles', 'xrODE',
               'xrOPCODE', 'xrMaterialSystem', 'xrGameSpy', 'xrScriptEngine', 'xrCPU_Pipe')

results = []


def say(name, ok, note=''):
    results.append((name, ok))
    print('  %-6s %-32s %s' % ('ok' if ok else 'ПРОВАЛ', name, note))
    return ok


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def git(repo, *args):
    try:
        r = subprocess.run(['git'] + list(args), cwd=repo, capture_output=True, text=True,
                           encoding='utf-8', errors='replace')
        return r.stdout.strip() if r.returncode == 0 else None
    except OSError:
        return None


def revisions_in(path):
    """Все 12-значные метки ревизии, вшитые в бинарник."""
    data = open(path, 'rb').read()
    out = set()
    for m in re.finditer(rb'[0-9a-f]{12}(?:-dirty)?', data):
        s = m.group(0).decode('ascii')
        # отсекаем куски длинных hex-строк: метка лежит отдельной C-строкой
        i, j = m.start(), m.end()
        before = data[i - 1:i]
        after = data[j:j + 1]
        if before and before.isalnum():
            continue
        if after and after.isalnum() and after != b'-':
            continue
        out.add(s)
    return out


# ---------------------------------------------------------------------------
# 1. Деревья зафиксированы
# ---------------------------------------------------------------------------
def check_trees():
    ok = True
    for name, repo in (('xray-16', XRAY), ('обновление', UPDATE)):
        if not os.path.isdir(os.path.join(repo, '.git')):
            ok = say('дерево %s' % name, False, 'не репозиторий: %s' % repo) and ok
            continue
        dirty = git(repo, 'status', '--porcelain', '--untracked-files=no')
        if dirty is None:
            ok = say('дерево %s' % name, False, 'git недоступен') and ok
            continue

        # Расхождение УКАЗАТЕЛЯ подмодуля выносим отдельной строкой: это не забытая правка, а
        # особое состояние, и лечится оно иначе. Смешивать их в одну строку нельзя — тогда
        # заслон краснеет всегда и его перестают читать.
        subs = git(repo, 'submodule', 'status') or ''
        sub_names = {l.strip().split()[1] for l in subs.splitlines() if l.strip()}
        files, ptrs = [], []
        for line in dirty.splitlines():
            path = line[2:].strip()
            (ptrs if path in sub_names else files).append(path)

        if files:
            ok = say('дерево %s' % name, False,
                     'не зафиксировано файлов: %d (первый: %s)' % (len(files), files[0])) and ok
        else:
            say('дерево %s' % name, True, 'чисто')

        if ptrs:
            # Родитель указывает на upstream-коммиты, а локально в подмодулях лежат НАШИ ветки.
            # Так сделано, чтобы `git clone --recursive` работал у посторонних. Обратная сторона:
            # по публичным исходникам наши бинарники не воспроизводятся — правки в luabind и
            # LuaJIT туда не попадают. Лечится своими форками, куда эти ветки можно выложить.
            #
            # ⚠️ Это ПРЕДУПРЕЖДЕНИЕ, а не запрет. На то, что получает игрок, указатели не влияют
            # вовсе: он ставит пакет через установщик и исходники не клонирует. Держать выпуск
            # из-за того, что игрока не касается, — верный способ научиться обходить заслон.
            for p in ptrs:
                print('         ⚠️ %s: указатель на upstream, локально своя ветка' % p)
            print('         ⚠️ по публичным исходникам сборка не воспроизводится; '
                  'выпуску не мешает')
    return ok


# ---------------------------------------------------------------------------
# 2. Ревизия: вшита, не грязная, совпадает с HEAD
# ---------------------------------------------------------------------------
def check_revision():
    core = os.path.join(GAME_BIN, 'xrCore.dll')
    if not os.path.exists(core):
        return say('ревизия', False, 'нет %s' % core)

    head = git(XRAY, 'rev-parse', '--short=12', 'HEAD')
    stamps = revisions_in(core)
    if not stamps:
        return say('ревизия', False, 'в xrCore.dll метки НЕТ — git не виден скрипту сборки')
    dirty = [s for s in stamps if s.endswith('-dirty')]
    if dirty:
        return say('ревизия', False, 'метка грязная: %s — сборка невоспроизводима' % dirty[0])
    stamp = sorted(stamps)[0]
    if not head:
        return say('ревизия', True, '%s (HEAD не прочитан)' % stamp)
    if stamp == head:
        return say('ревизия', True, stamp)

    # Метка отстала от HEAD. Само по себе это не беда: после сборки я коммичу документацию,
    # тесты и игровые файлы — на бинарник они не влияют. Беда, если между меткой и HEAD менялись
    # ИСХОДНИКИ: тогда в сборке лежит код одной ревизии, а подпись другой, и разбор краша поведёт
    # по чужим строкам. Ровно так и выходит, когда закоммитили, но забыли переобновить конфигурацию
    # (XRAY_GIT_SHA вычисляется на КОНФИГУРАЦИИ, а не на сборке).
    if git(XRAY, 'cat-file', '-e', stamp + '^{commit}') is None:
        return say('ревизия', False, 'метки %s нет в истории — сборку не восстановить' % stamp)

    changed = git(XRAY, 'diff', '--name-only', '%s..%s' % (stamp, head), '--',
                  'src', 'Externals', 'CMakeLists.txt', 'cmake')
    # Сдвиг УКАЗАТЕЛЯ подмодуля отсюда исключаем: он про то, на что ссылается публичный
    # репозиторий, а не про то, из чего собраны наши бинарники. Про него отдельно говорит
    # проверка деревьев — сведений не теряем.
    subs = {l.strip().split()[1] for l in (git(XRAY, 'submodule', 'status') or '').splitlines()
            if l.strip()}
    if changed:
        files = [f for f in changed.splitlines() if f not in subs]
    if changed and files:
        return say('ревизия', False,
                   'после %s менялись исходники (%d файл(ов), напр. %s) — пересоберите'
                   % (stamp, len(files), files[0]))
    return say('ревизия', True, '%s; после неё правились только не-исходники' % stamp)


def current_revision():
    core = os.path.join(GAME_BIN, 'xrCore.dll')
    if not os.path.exists(core):
        return None
    st = [s for s in revisions_in(core) if not s.endswith('-dirty')]
    return sorted(st)[0] if st else None


# ---------------------------------------------------------------------------
# 3. Модули: на месте, не пустые, лаунчерная копия та же
# ---------------------------------------------------------------------------
def check_binaries():
    if not os.path.isdir(GAME_BIN):
        return say('модули', False, 'нет %s' % GAME_BIN)

    small = []
    for fn in sorted(os.listdir(GAME_BIN)):
        p = os.path.join(GAME_BIN, fn)
        if not os.path.isfile(p) or not fn.lower().endswith(('.dll', '.exe')):
            continue
        # порог по размеру даёт ложные срабатывания (xrAPI.dll честно весит 90 КБ),
        # поэтому ловим только явно битое: пустое или огрызок
        if os.path.getsize(p) < 20000:
            small.append('%s (%d б)' % (fn, os.path.getsize(p)))
    if small:
        return say('модули', False, 'подозрительно малы: %s' % ', '.join(small))

    exe, alt = os.path.join(GAME_BIN, 'xrEngine.exe'), os.path.join(GAME_BIN, 'xr_3da.exe')
    if not os.path.exists(exe):
        return say('модули', False, 'нет xrEngine.exe — сборка делает xr_3da.exe, копию надо создать')
    if os.path.exists(alt) and sha(exe) != sha(alt):
        return say('модули', False, 'xrEngine.exe и xr_3da.exe РАЗНЫЕ — лаунчер запустит старый')
    return say('модули', True, 'файлов: %d' % len(os.listdir(GAME_BIN)))


# ---------------------------------------------------------------------------
# 4. Снимок символов
# ---------------------------------------------------------------------------
def check_snapshot():
    rev = current_revision()
    if not rev:
        return say('снимок', False, 'ревизия неизвестна')
    snap = os.path.join(SYMBOLS, rev)
    if not os.path.isdir(snap):
        return say('снимок', False, 'нет каталога %s — лог с этой сборки не расшифровать' % snap)

    for need in ('commit.txt', 'modules.txt', 'maps.txt'):
        if not os.path.exists(os.path.join(snap, need)):
            return say('снимок', False, 'в снимке нет %s' % need)

    # Свежесть карты сверяется с ЕЁ модулем, а не с соседними картами.
    #
    # ⚠️ Пометка «СТАРЕЕ» в maps.txt считается от самой новой карты в снимке, и на неполной
    # пересборке она врёт: `xrAPI.dll` не менялся с 09.08, его карта тоже от 09.08 — они друг
    # другу соответствуют, разбор по ней верен. Заслон, кричащий там, где всё в порядке, приучает
    # себя не читать, поэтому правило здесь одно: карта не должна быть СТАРШЕ своего модуля.
    stale = []
    for fn in sorted(os.listdir(snap)):
        if not fn.endswith('.map.gz'):
            continue
        name = fn[:-len('.map.gz')]
        module = None
        for ext in ('.dll', '.exe'):
            cand = os.path.join(GAME_BIN, name + ext)
            if os.path.exists(cand):
                module = cand
                break
        if not module:
            continue
        # запас в минуту: карта пишется линковщиком чуть раньше, чем файл кладётся в bin
        if os.path.getmtime(os.path.join(snap, fn)) + 60 < os.path.getmtime(module):
            stale.append('%s: карта старше модуля' % name)
    if stale:
        for s in stale:
            print('         %s' % s)
        return say('снимок', False, 'карт от другой сборки: %d' % len(stale))

    shipped = os.path.join(snap, 'shipped')
    if not os.path.isdir(shipped):
        return say('снимок', False, 'нет shipped/ — не с чем сверить отгруженные модули')

    bad, checked = [], 0
    for fn in sorted(os.listdir(shipped)):
        p = os.path.join(shipped, fn)
        if not os.path.isfile(p) or not fn.lower().endswith(('.dll', '.exe')):
            continue
        game = os.path.join(GAME_BIN, fn)
        if not os.path.exists(game):
            bad.append('%s: в снимке есть, в игре нет' % fn)
        elif sha(p) != sha(game):
            bad.append('%s: снимок и игра РАЗОШЛИСЬ' % fn)
        else:
            checked += 1
    if bad:
        for b in bad:
            print('         %s' % b)
        return say('снимок', False, 'расхождений: %d' % len(bad))
    return say('снимок', True, '%s, сверено модулей: %d' % (rev, checked))


# ---------------------------------------------------------------------------
# 5. Архив выкладки против того, что стоит в игре
# ---------------------------------------------------------------------------
# Наше, но игроку не отдаётся: отладочный стенд подсистем (см. docs/17_HEAP_GUARD.md).
# Держать его в списке модулей нельзя — заслон будет вечно требовать положить стенд в
# раздачу, а класть его туда незачем.
DEV_ONLY = {'da_asan_probe.exe'}


def check_archive():
    zip_path = os.path.join(UPDATE, '_dist', 'bin.zip')
    if not os.path.exists(zip_path):
        return say('архив bin.zip', False, 'нет %s' % zip_path)

    z = zipfile.ZipFile(zip_path)
    inside = {n.replace('/', os.sep): z.read(n) for n in z.namelist() if not n.endswith('/')}
    # Сверка идёт по всем файлам верхнего уровня — в архиве есть и не-модули (тексты лицензий).
    on_disk = {}
    for f in sorted(os.listdir(GAME_BIN)):
        p = os.path.join(GAME_BIN, f)
        if os.path.isfile(p):
            on_disk[f] = p

    # А вот «в игре есть, в архив не попал» проверяется ТОЛЬКО по настоящим модулям: рядом лежат
    # сотни моих резервных копий (`.dll.old`, `.release_backup`, `.before_*`), им в архиве делать
    # нечего. Новый модуль, забытый при сборке архива, этот фильтр всё равно поймает.
    modules = {f for f in on_disk if f.lower().endswith(('.dll', '.exe'))
               and f.lower() not in DEV_ONLY}

    miss = sorted(set(inside) - set(on_disk))
    extra = sorted(modules - set(inside))
    diff = [r for r in sorted(set(inside) & set(on_disk))
            if hashlib.sha256(inside[r]).hexdigest() != sha(on_disk[r])]

    for r in miss:
        print('         %s: в архиве есть, в игре нет' % r)
    for r in extra:
        print('         %s: в игре есть, в архив НЕ попал' % r)
    for r in diff:
        print('         %s: содержимое архива и игры РАЗОШЛОСЬ' % r)

    n = len(miss) + len(extra) + len(diff)
    return say('архив bin.zip', not n, 'файлов: %d, расхождений: %d' % (len(inside), n))


# ---------------------------------------------------------------------------
# 6. Пакет обновления: настройки по умолчанию и карта файловой системы
# ---------------------------------------------------------------------------
DEBUG_KEYS = re.compile(
    r'^\s*(da_(?:mem|memory|perf|gpu|vis|alloc|lua_gc|d3d|crash|after_load|cmd)\w*'
    r'|r__d3d_debug|g_god|g_unlimitedammo|ai_ignore_actor|demo_record|snd_device'
    r'|vid_mode|vid_monitor|load_last_save)\b(.*)$', re.I)


def check_package():
    ok = True
    fsgame = os.path.join(UPDATE, 'config', 'fsgame.ltx')
    if not os.path.exists(fsgame):
        ok = say('fsgame.ltx в пакете', False,
                 'НЕТ — установка встанет без единой строки лога') and ok
    else:
        say('fsgame.ltx в пакете', True, '%d б' % os.path.getsize(fsgame))

    user = os.path.join(UPDATE, 'config', 'user.ltx.default')
    if not os.path.exists(user):
        return say('user.ltx.default', False, 'нет %s' % user) and ok

    bad = []
    for num, line in enumerate(open(user, encoding='utf-8', errors='replace'), 1):
        m = DEBUG_KEYS.match(line)
        if not m:
            continue
        key, rest = m.group(1), m.group(2).strip()
        # выключенная ручка вреда не делает, машинно-зависимые ключи вредят всегда
        if key.lower() in ('snd_device', 'vid_mode', 'vid_monitor', 'load_last_save'):
            bad.append('%d: %s — ключ этой машины' % (num, key))
        elif rest not in ('0', 'off', 'false'):
            bad.append('%d: %s %s — отладочная ручка включена' % (num, key, rest))
    for b in bad:
        print('         %s' % b)
    ok = say('user.ltx.default', not bad, 'строк: %d, замечаний: %d'
             % (sum(1 for _ in open(user, encoding='utf-8', errors='replace')), len(bad))) and ok
    return ok


# ---------------------------------------------------------------------------
# 7. Игровые файлы и Lua — общим набором тестов
# ---------------------------------------------------------------------------
def check_tests():
    runner = os.path.join(XRAY, 'da_port', 'tests', 'run_tests.py')
    if not os.path.exists(runner):
        return say('тесты Lua и gamedata', False, 'нет %s' % runner)
    env = dict(os.environ)
    env['PYTHONIOENCODING'] = 'utf-8'
    env.setdefault('DA_UPDATE_REPO', UPDATE)
    r = subprocess.run([sys.executable, runner], capture_output=True, text=True,
                       encoding='utf-8', errors='replace', env=env)
    tail = [l for l in r.stdout.splitlines() if l.strip()]
    if r.returncode != 0:
        for l in tail[-12:]:
            print('         %s' % l)
    return say('тесты Lua и gamedata', r.returncode == 0,
               tail[-1][:70] if tail else 'без вывода')


# ---------------------------------------------------------------------------
# 8. Пункты меню против команд отгружаемого движка
# ---------------------------------------------------------------------------
# ⛔ Добавлено 27.08.2026, после выпуска 20.08, который ушёл СЛОМАННЫМ у всех. Игровые файлы в нём
# уехали свежие, модули остались от прошлой сборки, и разметка меню просила семь консольных команд,
# которых в её же движке не было. Первая из них (da_animations) роняла игру при открытии настроек
# видео: «Option token doesnt exist». Ни сборка, ни запуск, ни сверка деревьев этого не видят —
# каждая половина по отдельности исправна, несовместима только пара.
#
# Проверяем НЕ исходники и НЕ игру, а ровно то, что скачает игрок: XML из каталога обновления
# против двоичных модулей из bin.zip того же каталога.
def check_console_entries():
    ui = os.path.join(UPDATE, 'gamedata', 'configs', 'ui')
    zip_path = os.path.join(UPDATE, '_dist', 'bin.zip')
    if not os.path.isdir(ui):
        return say('пункты меню', False, 'нет %s' % ui)
    if not os.path.isfile(zip_path):
        return say('пункты меню', False, 'нет %s' % zip_path)

    entries = {}
    for fn in sorted(os.listdir(ui)):
        if not fn.lower().startswith('ui_mm_opt') or not fn.lower().endswith('.xml'):
            continue
        data = open(os.path.join(ui, fn), 'rb').read().decode('cp1251', 'replace')
        for name in re.findall(r'<options_item[^>]*entry\s*=\s*"([^"]+)"', data):
            entries.setdefault(name, fn)
    if not entries:
        return say('пункты меню', False, 'в разметке не нашлось ни одного пункта — проверка слепа')

    blob = b''
    with zipfile.ZipFile(zip_path) as z:
        for n in z.namelist():
            if n.lower().endswith(('.dll', '.exe')):
                blob += z.read(n)

    # Имя команды ищется как строка в модулях: она попадает туда буквально, при регистрации.
    missing = sorted(e for e in entries if e.encode('ascii', 'replace') not in blob)
    if missing:
        return say('пункты меню', False,
                   'разметка просит команды, которых нет в bin.zip: %s'
                   % ', '.join('%s (%s)' % (m, entries[m]) for m in missing))
    return say('пункты меню', True, 'проверено пунктов: %d' % len(entries))


def main():
    print('Заслон перед выкладкой')
    print('  игра:       %s' % GAME)
    print('  обновление: %s' % UPDATE)
    print('  символы:    %s' % SYMBOLS)
    print()

    check_trees()
    check_revision()
    check_binaries()
    check_snapshot()
    check_archive()
    check_package()
    check_console_entries()
    check_tests()

    failed = [n for n, ok in results if not ok]
    print()
    if failed:
        print('НЕ ВЫКЛАДЫВАТЬ: %d из %d (%s)' % (len(failed), len(results), ', '.join(failed)))
        return 1
    print('всё сошлось: %d проверок — можно публиковать' % len(results))
    return 0


if __name__ == '__main__':
    sys.exit(main())
