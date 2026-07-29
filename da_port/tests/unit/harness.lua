-- Песочница для скриптов мода: повторяет то, как их грузит движок, и подставляет моки вместо
-- движковых функций.
--
-- Как грузит движок (xrScriptEngine/script_engine.cpp:39): каждый файл оборачивается в
--   local this = {} setmetatable(this, {__index = _G}) setfenv(1, this)
-- то есть у файла своя таблица имён, чтение проваливается в _G, а запись остаётся у файла.
-- Здесь то же самое, только вместо _G подставляется таблица моков — иначе скрипт при загрузке
-- полезет в движковые функции, которых вне игры нет.

local H = {}

-- Пустая функция: подходит везде, где скрипту важен сам факт вызова, а не результат.
local function noop() end

-- ---------------------------------------------------------------------------
-- Моки движковых функций. Дополняются точечно из конкретного теста.
-- ---------------------------------------------------------------------------
function H.base_env()
    local env = {}

    -- стандартная библиотека
    for _, name in ipairs({ 'assert', 'error', 'ipairs', 'pairs', 'next', 'pcall', 'print',
                            'rawget', 'rawset', 'select', 'setmetatable', 'getmetatable',
                            'tonumber', 'tostring', 'type', 'unpack', 'string', 'table', 'math',
                            'os', 'io' }) do
        env[name] = _G[name]
    end

    -- Битовые операции движка. В игре их даёт LuaJIT-биндинг, у нас — bit.* из самого LuaJIT.
    -- ⚠️ Именно на разрядности этих операций горели маски поломок, поэтому здесь честные 32 бита.
    local bit = require('bit')
    env.bit_and = function(a, b) return bit.band(a, b) end
    env.bit_or = function(a, b) return bit.bor(a, b) end
    env.bit_not = function(a) return bit.bnot(a) end
    env.bit_xor = function(a, b) return bit.bxor(a, b) end

    -- math.pow в Lua 5.1 есть, в LuaJIT 2.1 тоже; на всякий случай подстрахуемся.
    env.math = setmetatable({ pow = function(a, b) return a ^ b end }, { __index = math })

    env.printf = noop
    env.log = noop

    -- Заглушки движковых типов и функций, которые встречаются на верхнем уровне файлов.
    env.sound_object = function() return { play = noop, stop = noop, playing = function() return false end } end
    env.particles_object = function() return { play_at_pos = noop, stop = noop, playing = function() return false end } end
    env.vector = function() return { set = function(self) return self end } end
    env.time_global = function() return 0 end
    env.device = function() return { width = 1920, height = 1080 } end

    return env
end

-- Ищет скрипт по каталогам из DA_SCRIPT_DIRS: сначала наши loose-правки, потом полный набор мода.
-- Порядок важен — loose перекрывает архивную версию, значит проверять надо именно его.
function H.script(name)
    for dir in string.gmatch(os.getenv('DA_SCRIPT_DIRS') or '', '[^;]+') do
        local path = dir .. '/' .. name
        local fh = io.open(path, 'r')
        if fh then
            fh:close()
            return path
        end
    end
    error('не найден скрипт ' .. name .. ' ни в одном из DA_SCRIPT_DIRS')
end

-- ---------------------------------------------------------------------------
-- Загрузка скрипта в песочницу
-- ---------------------------------------------------------------------------
-- path — файл скрипта, env — таблица моков (H.base_env() плюс что нужно тесту).
-- Возвращает таблицу имён скрипта: в ней лежат его глобальные функции и переменные.
function H.load(path, env)
    env = env or H.base_env()

    local ns = setmetatable({}, { __index = env })

    -- `class "CFoo" (CBar)` — это вызов class("CFoo")(CBar). Движок заводит таблицу класса в
    -- пространстве имён файла; нам достаточно таблицы с методом-конструктором.
    env.class = function(name)
        local cls = {}
        cls.__index = cls
        ns[name] = cls
        -- Конструктор: вызов CFoo() создаёт объект и зовёт __init, если он объявлен.
        setmetatable(cls, {
            __call = function(_, ...)
                local obj = setmetatable({}, cls)
                if cls.__init then cls.__init(obj, ...) end
                return obj
            end
        })
        return function() return cls end
    end

    local chunk, err = loadfile(path)
    if not chunk then
        return nil, err
    end
    setfenv(chunk, ns)
    local ok, run_err = pcall(chunk)
    if not ok then
        return nil, run_err
    end
    return ns
end

-- ---------------------------------------------------------------------------
-- Мини-фреймворк проверок
-- ---------------------------------------------------------------------------
local failures, checks = {}, 0

function H.check(condition, message)
    checks = checks + 1
    if not condition then
        failures[#failures + 1] = message
    end
end

function H.eq(actual, expected, message)
    checks = checks + 1
    if actual ~= expected then
        failures[#failures + 1] = string.format('%s: ожидалось %s, получено %s',
            message, tostring(expected), tostring(actual))
    end
end

-- Печатает итог и возвращает код возврата для os.exit.
function H.report(suite_name)
    if #failures == 0 then
        io.write(string.format('  ok   %-28s проверок: %d\n', suite_name, checks))
        return 0
    end
    io.write(string.format('  ПРОВАЛ %-26s проверок: %d, ошибок: %d\n', suite_name, checks, #failures))
    for _, f in ipairs(failures) do
        io.write('         ', f, '\n')
    end
    return 1
end

return H
