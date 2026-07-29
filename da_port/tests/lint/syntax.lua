-- Синтаксис всех скриптов.
--
-- Тот же LuaJIT 2.1.0-beta3, что и в движке (собирается из Externals/LuaJIT), поэтому «разобралось
-- здесь» означает «разберётся в игре». Ловит опечатки в правках сразу, не дожидаясь запуска: в игре
-- несобравшийся скрипт даёт ошибку только в тот момент, когда до него доходит дело.

local dirs = {}
for dir in string.gmatch(os.getenv('DA_SCRIPT_DIRS') or '', '[^;]+') do
    dirs[#dirs + 1] = dir
end

-- lua_help.script — не код, а дамп справки по API; движок его не грузит.
local skip = { ['lua_help.script'] = true }

local total, bad = 0, 0
for _, dir in ipairs(dirs) do
    local pipe = io.popen('dir /b "' .. string.gsub(dir, '/', '\\') .. '\\*.script" 2>nul')
    if pipe then
        for name in pipe:lines() do
            if not skip[name] then
                total = total + 1
                local chunk, err = loadfile(dir .. '/' .. name)
                if not chunk then
                    bad = bad + 1
                    io.write('         ', err, '\n')
                end
            end
        end
        pipe:close()
    end
end

if total == 0 then
    io.write('  ПРОВАЛ syntax: не найдено ни одного скрипта\n')
    os.exit(1)
end

if bad == 0 then
    io.write(string.format('  ok   %-28s файлов: %d\n', 'syntax', total))
    os.exit(0)
end
io.write(string.format('  ПРОВАЛ %-26s файлов: %d, с ошибками: %d\n', 'syntax', total, bad))
os.exit(1)
