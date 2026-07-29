-- Глобалы, которые читаются, но нигде не присваиваются.
--
-- ЗАЧЕМ. Ровно так выглядел баг с предупреждениями о выбросе: переменная `warn` читалась в трёх
-- ветках `surge_manager.script` и не присваивалась ни в одном из 378 скриптов. Ветки были мертвы,
-- игра при этом работала и молчала — Lua на чтение несуществующего глобала не ругается, он просто
-- nil, а сравнение с ним тихо ложно. Такой дефект не ловится ни запуском, ни глазами.
--
-- КАК. Разбирается не текст, а БАЙТ-КОД: LuaJIT честно помечает обращения к глобалам опкодами
-- GGET (чтение) и GSET (запись), включая вложенные функции. Поэтому ни комментарии, ни строки,
-- ни динамика имён результат не искажают.
--
-- Известные движковые символы (level, db, alife, vector…) перечислены в known_globals.txt — иначе
-- отчёт утонул бы в них. Файл — базовая линия: он фиксирует то, что уже есть, чтобы тест ругался
-- только на НОВОЕ.

local bc = require('jit.bc')

local scripts_dirs = {}
for dir in string.gmatch(os.getenv('DA_SCRIPT_DIRS') or '', '[^;]+') do
    scripts_dirs[#scripts_dirs + 1] = dir
end
local known_path = os.getenv('DA_KNOWN_GLOBALS')
local list_mode = os.getenv('DA_LIST_UNKNOWN') == '1'

-- --- сбор файлов ------------------------------------------------------------
-- lua_help.script — дамп справки по API, а не код; движок его не грузит.
local skip = { ['lua_help.script'] = true }

local files, seen = {}, {}
for _, dir in ipairs(scripts_dirs) do
    local pipe = io.popen('dir /b "' .. string.gsub(dir, '/', '\\') .. '\\*.script" 2>nul')
    if pipe then
        for name in pipe:lines() do
            -- loose перекрывает архивную версию, поэтому первый каталог в списке главнее
            if not skip[name] and not seen[name] then
                seen[name] = true
                files[#files + 1] = { dir = dir, name = name, path = dir .. '/' .. name }
            end
        end
        pipe:close()
    end
end

if #files == 0 then
    io.write('  ПРОВАЛ globals: не найдено ни одного скрипта\n')
    os.exit(1)
end

-- --- разбор байт-кода -------------------------------------------------------
-- Накопитель вместо файла: jit.bc.dump пишет через out:write(...).
local sink = {}
function sink:write(...)
    local n = select('#', ...)
    for i = 1, n do
        self[#self + 1] = tostring((select(i, ...)))
    end
    return self
end
function sink:close() end
function sink:flush() end

local reads, writes = {}, {}   -- имя -> список файлов
local parse_errors = {}

for _, f in ipairs(files) do
    local chunk, err = loadfile(f.path)
    if not chunk then
        parse_errors[#parse_errors + 1] = err
    else
        local out = setmetatable({}, { __index = sink })
        bc.dump(chunk, out, true) -- true = вместе со всеми вложенными функциями
        local text = table.concat(out)
        for op, name in string.gmatch(text, '(GGET)%s+%d+%s+%d+%s+;%s+"([^"]+)"') do
            reads[name] = reads[name] or {}
            reads[name][f.name] = true
        end
        for op, name in string.gmatch(text, '(GSET)%s+%d+%s+%d+%s+;%s+"([^"]+)"') do
            writes[name] = writes[name] or {}
            writes[name][f.name] = true
        end
    end
end

-- --- что считается известным ------------------------------------------------
local known = {}

-- 1. всё, что где-либо присваивается
for name in pairs(writes) do known[name] = true end

-- 2. имена самих скриптов: движок кладёт таблицу файла в глобал с его именем
for _, f in ipairs(files) do
    known[(string.gsub(f.name, '%.script$', ''))] = true
end

-- 3. базовые линии: движковые символы и отдельно — разобранные дефекты мода
local mod_bugs = {}
local function read_baseline(path, into)
    if not path then return end
    local fh = io.open(path, 'r')
    if not fh then return end
    for line in fh:lines() do
        line = string.gsub(line, '%-%-.*$', '')
        line = string.match(line, '^%s*(.-)%s*$')
        if line ~= '' then into[line] = true end
    end
    fh:close()
end

read_baseline(known_path, known)
read_baseline(os.getenv('DA_KNOWN_MOD_BUGS'), mod_bugs)
for name in pairs(mod_bugs) do known[name] = true end

-- --- отчёт ------------------------------------------------------------------
local unknown = {}
for name, where in pairs(reads) do
    if not known[name] then
        local files_list = {}
        for fn in pairs(where) do files_list[#files_list + 1] = fn end
        table.sort(files_list)
        unknown[#unknown + 1] = { name = name, files = files_list }
    end
end
table.sort(unknown, function(a, b) return a.name < b.name end)

if list_mode then
    -- режим пополнения базовой линии: просто печатаем имена
    for _, u in ipairs(unknown) do io.write(u.name, '\n') end
    os.exit(0)
end

for _, err in ipairs(parse_errors) do
    io.write('  ПРОВАЛ globals: ', err, '\n')
end

-- Дефекты мода из базовой линии показываем как справку: набор остаётся зелёным, находка не теряется.
local bug_notes = {}
for name in pairs(mod_bugs) do
    if reads[name] then
        local where = {}
        for fn in pairs(reads[name]) do where[#where + 1] = fn end
        table.sort(where)
        bug_notes[#bug_notes + 1] = '         ' .. name .. '  <- ' .. table.concat(where, ', ')
    end
end
table.sort(bug_notes)

if #unknown == 0 and #parse_errors == 0 then
    io.write(string.format('  ok   %-28s файлов: %d, глобалов прочитано: %d\n',
        'globals', #files, (function() local n = 0 for _ in pairs(reads) do n = n + 1 end return n end)()))
    if #bug_notes > 0 then
        io.write('         (известные дефекты мода, разбор в known_mod_bugs.txt:)\n')
        for _, note in ipairs(bug_notes) do io.write(note, '\n') end
    end
    os.exit(0)
end

io.write(string.format('  ПРОВАЛ %-26s неизвестных глобалов: %d\n', 'globals', #unknown))
for _, u in ipairs(unknown) do
    io.write('         ', u.name, '  <- ', table.concat(u.files, ', '), '\n')
end
io.write('\n  Читается, но нигде не присваивается. Либо это движковый символ — тогда допишите его\n')
io.write('  в tests/lint/known_globals.txt, либо оборванная проводка, как было с warn.\n')
os.exit(1)
