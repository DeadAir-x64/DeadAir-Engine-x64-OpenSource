-- Поломки оружия: битовая маска.
--
-- Зачем тест: на разрядности этой маски проект горел ТРИЖДЫ — u8 в движке резал биты 8..31,
-- float в ремонтных наборах округлял младшие разряды. Здесь проверяется, что бит 30 остаётся
-- битом 30 на всём пути, и что get_break соблюдает разрешения из condition_avail.

local H = require('harness')

local env = H.base_env()

-- system_ini() подменяется под каждый набор проверок.
local ini_values = {}
env.system_ini = function()
    return {
        r_string_ex = function(_, section, key) return ini_values[section .. '.' .. key] end,
        r_float_ex = function(_, section, key) return tonumber(ini_values[section .. '.' .. key]) end,
    }
end
env.alun_utils = {
    parse_list = function(_, section, key)
        local raw = ini_values[section .. '.' .. key]
        local out = {}
        if raw then
            for item in string.gmatch(raw, '[^,]+') do out[#out + 1] = item end
        end
        return out
    end
}
env.IsWeapon = function() return true end

local ic, err = H.load(H.script('items_condition.script'), env)
if not ic then
    io.write('  ПРОВАЛ items_condition: не загрузился: ', tostring(err), '\n')
    os.exit(1)
end

-- --- биты живут во всём диапазоне 0..31 ------------------------------------
for _, num in ipairs({ 0, 1, 7, 8, 15, 23, 27, 28, 29, 30 }) do
    local mask = ic.add_condition_type(0, num)
    H.eq(mask, 2 ^ num, 'бит ' .. num .. ' ставится')
    H.check(ic.have_condition_type(mask, num), 'бит ' .. num .. ' читается обратно')
    H.eq(ic.remove_condition_type(mask, num), 0, 'бит ' .. num .. ' снимается')
end

-- Бит 31 отдельно: это знаковый разряд, на нём ломались прошлые реализации.
local top = ic.add_condition_type(0, 31)
H.check(ic.have_condition_type(top, 31), 'бит 31 (знаковый) читается')
H.check(not ic.have_condition_type(top, 30), 'бит 31 не задевает соседний 30')

-- --- повторная установка не портит маску ------------------------------------
local m = ic.add_condition_type(0, 5)
H.eq(ic.add_condition_type(m, 5), m, 'повторная установка бита ничего не меняет')
H.eq(ic.remove_condition_type(m, 6), m, 'снятие несуществующего бита ничего не меняет')

-- --- сумма битов не теряет разряды ------------------------------------------
local sum = 0
for _, num in ipairs({ 2, 9, 17, 28 }) do sum = ic.add_condition_type(sum, num) end
for _, num in ipairs({ 2, 9, 17, 28 }) do
    H.check(ic.have_condition_type(sum, num), 'бит ' .. num .. ' уцелел в общей маске')
end

-- --- get_break соблюдает condition_avail ------------------------------------
-- Ствол убит в ноль (condition = 0), значит бросок кубика проходит для всех типов поломок;
-- разрешён при этом только бит 3. Всё остальное обязано остаться невзведённым.
ini_values = {
    ['wpn_test.condition_avail'] = tostring(2 ^ 3),
    ['wpn_test.scope_status'] = '0',
    ['wpn_test.silencer_status'] = '0',
    ['wpn_test.grenade_launcher_status'] = '0',
    ['wpn_test.fire_modes'] = '1',
}
local broken = ic.get_break('wpn_test', 0, 0)
H.eq(broken, 2 ^ 3, 'get_break ставит только разрешённый condition_avail бит')

-- Аддонные биты 28/29/30 не должны появляться у ствола, который этих аддонов не носит,
-- даже если condition_avail их формально разрешает.
ini_values['wpn_test.condition_avail'] = tostring(2 ^ 28 + 2 ^ 29 + 2 ^ 30)
local addons = ic.get_break('wpn_test', 0, 0)
H.eq(addons, 0, 'без прицела/глушителя/подствольника их биты не ставятся')

-- А с аддонами — ставятся.
ini_values['wpn_test.scope_status'] = '2'
ini_values['wpn_test.silencer_status'] = '2'
ini_values['wpn_test.grenade_launcher_status'] = '2'
local with_addons = ic.get_break('wpn_test', 0, 0)
H.check(ic.have_condition_type(with_addons, 28), 'бит прицела ставится при scope_status=2')
H.check(ic.have_condition_type(with_addons, 29), 'бит глушителя ставится при silencer_status=2')
H.check(ic.have_condition_type(with_addons, 30), 'бит подствольника ставится при grenade_launcher_status=2')

os.exit(H.report('items_condition'))
