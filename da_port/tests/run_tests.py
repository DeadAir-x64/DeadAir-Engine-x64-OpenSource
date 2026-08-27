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
  actor_calls — методы db.actor: нет биндинга или за ним заглушка;
  options   — ключи opt_* без единого читателя;
  gamedata  — da_gamedata в репозитории против развёрнутого в игре;
  release_shipping — всё правленое уехало в отгрузку, и ровно то, что мы проверяли;
  loop_vars — осиротевший счётчик после перевода перебора ALife на индекс;
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
    # Читаем вывод как UTF-8 явно: без этого Python берёт кодовую страницу консоли (cp866/cp1251),
    # и весь русский текст отчёта приезжает кашей. Мелочь на вид, но однажды ровно эта каша
    # спрятала четыре ложных провала в test_symbolicate — сравнение шло по русским подстрокам.
    rc = subprocess.run([LUAJIT, script], env=env, capture_output=True, text=True,
                        encoding='utf-8', errors='replace', cwd=TESTS)
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
# Проверка: методы актёра и заглушки
# ---------------------------------------------------------------------------
def check_actor_calls():
    """`db.actor:X()` должен быть зарегистрирован в порте — и не быть заглушкой.

    Заведена по факту: перк «Твёрдая рука» звал `db.actor:set_actor_recoil_coeff(0.4)`, биндинг
    существовал, но внутри стояла заглушка с сообщением «Called missing function». Перк выглядел
    рабочим на всём пути — выбор в меню, информпорция, читатель в скрипте, — и не делал ничего.
    Проверка level.* этого не видела: она смотрит другое пространство имён.

    Заглушки опаснее отсутствующих биндингов. Отсутствующий вызов роняет скрипт с ошибкой в лог,
    и это заметно; заглушка молча возвращает управление, и механика просто не работает — а по
    журналу игры не отличить от «так задумано».

    Ищем два разных дефекта:
      • имя, которого нет ни в одном .def   — вызов упадёт;
      • имя, за которым стоит DA_PORT_STUB  — вызов пройдёт и ничего не сделает.
    """
    game_dir = os.path.join(REPO, 'src', 'xrGame')
    if not os.path.isdir(game_dir):
        report('actor_calls', True, 'пропущено: нет исходников порта')
        return True

    bound = set()
    stub_cpp = set()
    for dirpath, _, filenames in os.walk(game_dir):
        for fn in filenames:
            if not fn.endswith(('.cpp', '.h')):
                continue
            text = open(os.path.join(dirpath, fn), encoding='utf-8', errors='replace').read()
            bound.update(re.findall(r'\.def\("([a-zA-Z_0-9]+)"', text))
            bound.update(re.findall(r'\.property\("([a-zA-Z_0-9]+)"', text))
            # Функция считается заглушкой, если печатает DA_PORT_STUB. Имя C++ берём по сигнатуре
            # выше по тексту: заглушки живут в одном файле с биндингами и всегда однострочные.
            for m in re.finditer(r'(\w+)::(\w+)\([^)]*\)\s*\{[^}]*DA_PORT_STUB[^}]*\}', text, re.S):
                stub_cpp.add(m.group(2))

    # Имя Lua у заглушки: ищем .def("lua_name", &Class::CppName) для найденных C++-имён.
    stub_lua = set()
    for dirpath, _, filenames in os.walk(game_dir):
        for fn in filenames:
            if not fn.endswith('.cpp'):
                continue
            text = open(os.path.join(dirpath, fn), encoding='utf-8', errors='replace').read()
            for lua_name, cpp_name in re.findall(r'\.def\("([a-zA-Z_0-9]+)",\s*&\w+::(\w+)\)', text):
                if cpp_name in stub_cpp:
                    stub_lua.add(lua_name)

    used = {}
    src_dir = DUMP_SCRIPTS if os.path.isdir(DUMP_SCRIPTS) else LOOSE_SCRIPTS
    for fn in sorted(os.listdir(src_dir)):
        if not fn.endswith('.script'):
            continue
        text = open(os.path.join(src_dir, fn), encoding='utf-8', errors='replace').read()
        text = re.sub(r'--[^\n]*', '', text)
        for name in re.findall(r'\bdb\.actor:([a-zA-Z_][a-zA-Z_0-9]*)\s*\(', text):
            used.setdefault(name, set()).add(fn)

    known_missing = load_baseline('known_missing_actor_calls.txt')
    problems = []
    for name, files in sorted(used.items()):
        where = ', '.join(sorted(files)[:2])
        if name in stub_lua:
            problems.append('db.actor:%s — ЗАГЛУШКА в порте  <- %s' % (name, where))
        elif name not in bound and name not in known_missing:
            problems.append('db.actor:%s — нет биндинга  <- %s' % (name, where))

    for p in problems:
        print('         %s' % p)
    report('actor_calls', not problems, 'вызовов db.actor: %d, заглушек в порте: %d'
           % (len(used), len(stub_lua)))
    return not problems


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


# ---------------------------------------------------------------------------
# Проверка: диалоги ссылаются на существующие функции
# ---------------------------------------------------------------------------
def check_dialog_functions():
    """Каждая функция, названная в диалоге, обязана существовать в скриптах.

    Движок искал такую функцию под THROW3. XRAY_EXCEPTIONS задан сборкой (-DXRAY_EXCEPTIONS=1),
    а не исходниками, поэтому THROW в релизе не исчезает — он БРОСАЕТ исключение, а на пути
    MinGW его никто не ловит (WinMain там без try). Дальше std::terminate и общее окно
    «Unexpected application termination»; имя недостающей функции THROW3 собрал в буфер,
    который никто не печатает. Движок теперь такие места переживает и называет
    (см. da_script_functor.h), но сама ссылка от этого не чинится: предусловие с
    несуществующей функцией навсегда закрывает фразу.

    Найденные при написании проверки 20 ссылок перечислены в known_dialog_funcs.txt: все они
    ведут в dialogs_pripyat, а этот файл в моде ПУСТОЙ (2 байта из 378 скриптов — единственный).
    """
    gameplay_dirs = [d for d in (
        os.path.join(GAME, 'gamedata', 'configs', 'gameplay'),
        os.path.join(os.path.dirname(REPO), 'extracted', 'configs', 'gameplay'),
    ) if os.path.isdir(d)]
    if not gameplay_dirs:
        report('dialog_funcs', True, 'пропущено: нет configs/gameplay')
        return True

    def read(path):
        for enc in ('utf-8', 'cp1251'):
            try:
                return open(path, encoding=enc).read()
            except UnicodeDecodeError:
                continue
        return open(path, encoding='cp1251', errors='replace').read()

    # что вообще определено в скриптах
    defined, modules = set(), set()
    for src_dir in (DUMP_SCRIPTS, LOOSE_SCRIPTS):
        if not os.path.isdir(src_dir):
            continue
        for fn in os.listdir(src_dir):
            if not fn.endswith('.script'):
                continue
            mod = fn[:-len('.script')]
            modules.add(mod)
            text = read(os.path.join(src_dir, fn))
            for m in re.finditer(r'^\s*function\s+([A-Za-z_][\w.:]*)', text, re.M):
                name = m.group(1).replace(':', '.')
                defined.add(name if '.' in name else '%s.%s' % (mod, name))
            # присваивание функции в поле таблицы: foo.bar = function(...)
            for m in re.finditer(r'^\s*([A-Za-z_][\w.]*)\s*=\s*function\s*\(', text, re.M):
                name = m.group(1)
                defined.add(name if '.' in name else '%s.%s' % (mod, name))

    # на что ссылаются диалоги; loose-файл перекрывает архивный
    refs = {}
    for gp in reversed(gameplay_dirs):
        for fn in sorted(os.listdir(gp)):
            if not fn.endswith('.xml'):
                continue
            text = read(os.path.join(gp, fn))
            for tag in ('precondition', 'action', 'script_text', 'init_func'):
                for m in re.finditer(r'<%s[^>]*>([^<]+)</%s>' % (tag, tag), text):
                    name = m.group(1).strip()
                    # ссылка на функцию всегда вида модуль.функция
                    if '.' not in name or ' ' in name:
                        continue
                    line = text[:m.start()].count('\n') + 1
                    refs.setdefault(name, set()).add('%s:%d' % (fn, line))

    known = load_baseline('known_dialog_funcs.txt')
    missing = sorted(n for n in refs if n not in defined and n not in known)
    for name in missing:
        where = sorted(refs[name])
        print('         %s — функции нет (%s%s)' % (
            name, ', '.join(where[:2]), ' …' if len(where) > 2 else ''))
    report('dialog_funcs', not missing, 'ссылок: %d' % len(refs))
    return not missing


# ---------------------------------------------------------------------------
# Проверка: разметка интерфейса для НЕширокоформатных экранов
# ---------------------------------------------------------------------------
def check_gamedata():
    """Всё, что лежит в `da_gamedata`, обязано совпадать с развёрнутым в игре.

    Правки удобно делать прямо в игре — там их видно сразу, — и именно поэтому копия в репозитории
    отстаёт молча. Когда проверку написали, разошлись СЕМЬ файлов из сорока трёх: разметка настроек
    на 543 строки, все три скрипта вкладок видео, обе таблицы строк (вдвое короче!) и два шейдерных
    заголовка. Релиз, собранный из репозитория, увёз бы старое меню и половину подписей, а заметили
    бы это тестеры.

    Обратный случай тоже настоящий: `ui_mm_opt_video_adv.script` в репозитории был длиннее игрового
    — он остался от той поры, когда вкладка ещё строила давно убранные строки. Значит «в репозитории
    больше, значит новее» — неверная догадка, и сверять надо содержимое, а не размер.
    """
    game_root = os.path.join(GAME, 'gamedata')
    repo_root = os.path.join(REPO, 'da_gamedata')
    if not os.path.isdir(game_root) or not os.path.isdir(repo_root):
        report('gamedata', True, 'пропущено: нет da_gamedata')
        return True

    def content(path):
        """Содержимое без оглядки на переводы строк.

        Сравнивать сырые байты нельзя: в `.gitattributes` стоит `* text=auto`, поэтому копия в
        репозитории после выгрузки на Windows окажется с CRLF, а копия в игре останется с LF.
        Побайтовая сверка провалилась бы на свежем клоне, ничего не сообщив о настоящем расхождении.
        """
        return open(path, 'rb').read().replace(b'\r\n', b'\n')

    # README.md описывает сам каталог и в игру не разворачивается.
    #
    # axr_options.ltx игра ПЕРЕПИСЫВАЕТ САМА: axr_main держит его как ini_file_ex с правом записи
    # (`config = ini_file_ex("axr_options.ltx", true)`), и моды пишут туда состояние — например,
    # FDDA сохраняет отметку о смене поля зрения, чтобы вернуть прежнее, если игра оборвётся
    # посреди анимации. Копия в игре расходится с репозиторной после КАЖДОГО запуска, и сверять их
    # бессмысленно: это не исходник, а хранилище состояния.
    skip = {'README.md', 'axr_options.ltx'}

    problems = []
    checked = 0
    for dirpath, _, filenames in os.walk(repo_root):
        for name in sorted(filenames):
            if name in skip:
                continue
            rel = os.path.relpath(os.path.join(dirpath, name), repo_root)
            in_game = os.path.join(game_root, rel)
            if not os.path.exists(in_game):
                problems.append('%s есть в da_gamedata, но не развёрнут в игре' % rel)
                continue
            checked += 1
            if content(os.path.join(repo_root, rel)) != content(in_game):
                problems.append('%s: копия в игре и в da_gamedata разошлись' % rel)

    for p in problems:
        print('         %s' % p)
    report('gamedata', not problems, 'файлов: %d' % checked)
    return not problems


def _same_content(a, b):
    """Одинаковы ли файлы по существу: без оглядки на перевод строки и BOM.

    Сравнивать сырые байты нельзя. Отгрузка приезжает к игроку архивом с GitHub, где хранятся
    блобы репозитория (LF), а рабочая копия на Windows лежит с CRLF — побайтовая сверка объявила
    бы разошедшимися все 742 текстовых файла и утопила настоящее расхождение в шуме.
    """
    def norm(p):
        data = open(p, 'rb').read()
        if data.startswith(b'\xef\xbb\xbf'):
            data = data[3:]
        while b'\r\r\n' in data:
            data = data.replace(b'\r\r\n', b'\r\n')
        return data.replace(b'\r\n', b'\n').replace(b'\r', b'\n').rstrip()
    return norm(a) == norm(b)


# Разделы, которые уезжают игроку. Скомпилированный кэш шейдеров сюда НЕ входит: он порождаемый,
# его расхождение стоит перекомпиляции при первом запуске, а не ошибки.
SHIPPED_DIRS = ('scripts', 'configs', 'shaders', 'meshes', 'sounds', 'textures')


def check_release_shipping():
    """Всё, что мы правили, обязано уехать игроку — и ровно в том виде, в каком мы это проверяли.

    ⛔ Проверка написана по следам 11.08.2026, когда выпуск ушёл тестерам НЕПОЛНЫМ. Движок уехал
    целиком, а из правок по Lua — ничего: последний коммит игровых файлов был 09.08 в 04:46, а
    восемнадцать скриптов правились после, с 09.08 13:48 по 11.08 01:14. Люди уже качали сборку,
    где не было ни ускорения КПК (18.5 -> 2.1 мс), ни кэша condlist, ни семи защит от пустых
    значений. Соседняя проверка `gamedata` этого не видела: она сторожит связку игра <-> xray-16,
    а до каталога ОТГРУЗКИ цепочка не доходила.

    Три дерева и два правила:
      оригинал мода (`extracted`) -> игра (loose gamedata) -> отгрузка (репозиторий обновления)
      1. файл в игре отличается от оригинала мода (или его там нет вовсе) => обязан быть в отгрузке
         с тем же содержимым. Иначе правка до игрока не доедет;
      2. файл в отгрузке обязан быть и в игре, с тем же содержимым. Иначе игроки получают то, чего
         мы не проверяли, — так и вышло с тремя скриптами (bind_monster, sim_board, xr_motivator).

    ⚠️ Направление решается СОДЕРЖИМЫМ, а не датой. `sim_squad_scripted.script` в игре имел дату
    СВЕЖЕЕ репозиторной, но при этом потерял защиту от освобождённого отряда — дата соврала.

    Каталог отгрузки задаётся через DA_UPDATE_REPO; без него проверка пропускается, чтобы тесты
    оставались рабочими на машине без репозитория обновления.
    """
    ship_root = os.environ.get('DA_UPDATE_REPO', os.path.join(os.path.dirname(REPO), '..',
                                                              'DeadAir_Update'))
    ship_root = os.path.normpath(os.path.join(ship_root, 'gamedata'))
    orig_root = os.path.join(os.path.dirname(REPO), 'extracted')
    game_root = os.path.join(GAME, 'gamedata')

    if not os.path.isdir(ship_root):
        report('release_shipping', True, 'пропущено: нет каталога отгрузки (DA_UPDATE_REPO)')
        return True

    # Файлы, которые сознательно НЕ отгружаются. Каждая строка — путь от gamedata и причина.
    #
    # Строка, оканчивающаяся на `/`, закрывает КАТАЛОГ целиком. Это не послабление, а защита от
    # обратного: рабочий каталог с резервными копиями шейдеров (`shaders/_off_visual_21.08/`)
    # пришлось бы вносить двадцатью строками, и любой новый файл в нём заслон уронил бы заново —
    # то есть список чинили бы правкой без разбора, ровно тем способом, против которого он написан.
    # Отдельные ФАЙЛЫ по-прежнему заносятся поимённо: каталог исключается осознанно, файл — нет.
    skip, skip_dirs = set(), []
    excl = os.path.join(TESTS, 'lint', 'not_shipped.txt')
    if os.path.exists(excl):
        for line in open(excl, encoding='utf-8'):
            line = line.split('--')[0].strip()
            if not line:
                continue
            if line.endswith('/') or line.endswith('\\'):
                skip_dirs.append(line.rstrip('/\\').replace('/', os.sep).lower() + os.sep)
            else:
                skip.add(line.replace('/', os.sep).lower())

    # [DA_PORT] Состав модуля анимаций читается ИЗ САМОГО АРХИВА, а не из списка.
    #
    # Модуль уезжает игроку отдельно (da_animations.xdb0 со своим установщиком), поэтому через
    # DeadAir_Update его файлы не идут. Их больше тысячи, и держать их перечнем в not_shipped.txt
    # было бы двумя способами ошибиться: список устареет при первой пересборке модуля, а каталогом
    # его не заменить — модуль делит textures/, meshes/ и sounds/ с самой игрой, и целый каталог в
    # исключениях спрятал бы заодно наши настоящие правки в тех же папках.
    #
    # Читая архив, заслон всегда знает ровно то, что в модуле есть СЕЙЧАС. Нет архива — исключений
    # нет, и заслон честно ругается на каждый файл: это правильно, значит модуль не собран.
    mod_files = set()
    arc_path = os.path.join(GAME, 'database', 'da_animations.xdb0')
    if os.path.exists(arc_path):
        try:
            import importlib.util
            # ⚠️ Каталог инструментов ОБЯЗАН попасть в путь поиска: unpack_xdb тянет соседний
            # lzo1x обычным import, и загрузка по файлу его не находит.
            _tools = os.path.join(REPO, 'da_port', 'tools')
            if _tools not in sys.path:
                sys.path.insert(0, _tools)
            _spec = importlib.util.spec_from_file_location(
                '_xdb', os.path.join(_tools, 'unpack_xdb.py'))
            _xdb = importlib.util.module_from_spec(_spec)
            _spec.loader.exec_module(_xdb)
            for e in _xdb.load_fat(arc_path):
                if e['size_real']:
                    mod_files.add(e['name'].replace(chr(92), os.sep).replace('/', os.sep).lower())
        except Exception as ex:
            print('  ! модуль анимаций не прочитан (%s) — его файлы попадут в расхождения' % ex)

    def skipped(rel):
        low = rel.lower()
        return low in skip or low in mod_files or any(low.startswith(d) for d in skip_dirs)

    missing, stale, untested = [], [], []
    ours = 0
    for sub in SHIPPED_DIRS:
        gdir = os.path.join(game_root, sub)
        if not os.path.isdir(gdir):
            continue
        for dirpath, _, filenames in os.walk(gdir):
            for name in filenames:
                full = os.path.join(dirpath, name)
                rel = os.path.relpath(full, game_root)
                if skipped(rel):
                    continue
                orig = os.path.join(orig_root, rel)
                # не наше: копия оригинала мода лежит рядом просто для чтения
                if os.path.exists(orig) and _same_content(orig, full):
                    continue
                ours += 1
                shipped = os.path.join(ship_root, rel)
                if not os.path.exists(shipped):
                    missing.append(rel)
                elif not _same_content(shipped, full):
                    stale.append(rel)

    for dirpath, _, filenames in os.walk(ship_root):
        for name in filenames:
            rel = os.path.relpath(os.path.join(dirpath, name), ship_root)
            if skipped(rel) or rel.split(os.sep)[0] not in SHIPPED_DIRS:
                continue
            in_game = os.path.join(game_root, rel)
            if not os.path.exists(in_game):
                untested.append(rel)

    for rel in missing:
        print('         %s: правлено у нас, в отгрузке НЕТ' % rel)
    for rel in stale:
        print('         %s: в отгрузке СТАРАЯ версия' % rel)
    for rel in untested:
        print('         %s: отгружается, а в игре нет — значит не проверено' % rel)

    problems = len(missing) + len(stale) + len(untested)
    report('release_shipping', not problems, 'наших файлов: %d, расхождений: %d' % (ours, problems))
    return not problems


def check_loop_vars():
    """Осиротевшая переменная цикла после перевода перебора ALife на индекс.

    ⛔ Перевод `for i=1,65534 do sim:object(i)` на `for da_id in pairs(db.offline_objects)`
    переименовывает счётчик, и тело обязано переехать вместе с ним. В `xr_effects.script`
    (`on_init_bounty_hunt`) три строки остались на старом `i`: снаружи цикла это ГЛОБАЛ, то есть
    пустое значение. `table.insert(valid_targets, nil)` не падает и ничего не кладёт, поэтому
    список оставался пустым, и задача «охота за головами» молча не получала цели — ни ошибки, ни
    строки в логе. Дефект прожил в дереве до сверки перед выпуском.

    Ищем ровно этот класс: имя счётчика в теле НЕ совпадает с именем цикла и нигде в теле не
    объявлено. Проверка узкая намеренно — широкая давала бы ложные срабатывания на `i` из
    объемлющих циклов, а цена ложного срабатывания здесь выше цены пропуска.
    """
    loop_re = re.compile(r'^(\s*)for\s+(\w+)\s+in\s+pairs\(db\.offline_objects\)')
    suspect = ('i', 'k', 'n', 'index')
    problems = []
    loops = 0

    for fn in sorted(os.listdir(LOOSE_SCRIPTS)):
        if not fn.endswith('.script'):
            continue
        data = open(os.path.join(LOOSE_SCRIPTS, fn), 'rb').read()
        try:
            text = data.decode('utf-8')
        except UnicodeDecodeError:
            text = data.decode('cp1251', errors='replace')
        lines = text.replace('\r\n', '\n').split('\n')

        for num, line in enumerate(lines):
            m = loop_re.match(line)
            if not m:
                continue
            loops += 1
            var, indent = m.group(2), len(m.group(1))
            body = []
            for j in range(num + 1, len(lines)):
                cur = lines[j]
                if cur.strip() and (len(cur) - len(cur.lstrip())) <= indent \
                        and cur.strip().startswith('end'):
                    break
                body.append((j + 1, cur))

            declared = {var}
            for _, cur in body:
                for mm in re.finditer(r'\blocal\s+([\w, ]+)', cur):
                    declared.update(x.strip() for x in mm.group(1).split(','))
                for mm in re.finditer(r'\bfor\s+([\w, ]+?)\s*(?:=|in)\b', cur):
                    declared.update(x.strip() for x in mm.group(1).split(','))

            for ln, cur in body:
                code = cur.split('--')[0]
                for old in suspect:
                    if old in declared:
                        continue
                    if re.search(r'(?<![\w.:])' + old + r'(?![\w])', code):
                        problems.append('%s:%d цикл по «%s», в теле осталось «%s»'
                                        % (fn, ln, var, old))
                        break

    for p in problems:
        print('         %s' % p)
    report('loop_vars', not problems, 'переведённых переборов: %d' % loops)
    return not problems


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
# ---------------------------------------------------------------------------
# Проверки на Python: инструменты порта
# ---------------------------------------------------------------------------
def check_python_units():
    """Тесты инструментов (unit/test_*.py) — расшифровка стека и прочая оснастка.

    Отдельно от Lua-тестов: тем нужен собранный интерпретатор, а этим — только Python и файлы
    сборки. Падение одного не должно прятать результат другого, поэтому каждый файл считается
    своей строкой отчёта.
    """
    unit_dir = os.path.join(TESTS, 'unit')
    for fn in sorted(os.listdir(unit_dir)):
        if not (fn.startswith('test_') and fn.endswith('.py')):
            continue
        proc = subprocess.run([sys.executable, os.path.join(unit_dir, fn)],
                              capture_output=True, text=True, errors='replace')
        ok = proc.returncode == 0
        note = ''
        if not ok:
            tail = [l for l in (proc.stdout + proc.stderr).splitlines() if l.strip()]
            note = tail[-1][:70] if tail else 'без вывода'
        report(fn[:-3], ok, note)


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
    check_actor_calls()
    check_options()
    check_dialog_functions()
    check_gamedata()
    check_release_shipping()
    check_loop_vars()
    check_python_units()

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
