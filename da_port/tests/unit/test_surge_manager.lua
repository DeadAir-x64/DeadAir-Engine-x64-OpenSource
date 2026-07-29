-- Выброс: настройки предупреждения и судьбы NPC.
--
-- Зачем тест: обе связи были ОБОРВАНЫ в моде и восстановлены нами. `warn` не присваивалась нигде,
-- поэтому радиофразы не играли никогда; судьба NPC была зашита намертво, поэтому turn_to_zombie и
-- explode не вызывались ни разу. Тест держит обе проводки на месте.

local H = require('harness')

local env = H.base_env()

-- Настройки AtmosFear: подменяемая таблица значений.
local opts = {}
env.atmosfear_options = {
    config = {
        r_value = function(_, section, key, _typ, default)
            local v = opts[section .. '.' .. key]
            if v == nil then return default end
            return v
        end
    }
}
env.ini_file = function() return { r_string_to_condlist = function() return {} end,
                                   line_count = function() return 0 end } end
env.empty_table = function(t) return t end
env.game = { get_game_time = function() return 0 end }
env.level = { get_time_factor = function() return 10 end, name = function() return 'l05_bar' end }

local sm, err = H.load(H.script('surge_manager.script'), env)
if not sm then
    io.write('  ПРОВАЛ surge_manager: не загрузился: ', tostring(err), '\n')
    os.exit(1)
end

-- --- предупреждение ---------------------------------------------------------
-- Главное: значение берётся из настройки, а не остаётся мёртвым литералом.
for _, value in ipairs({ 'nowarning', 'radio', 'siren', 'radio_siren' }) do
    opts['atmosfear_current_parameters.opt_blowout_warning'] = value
    sm.read_warning_option()
    H.eq(sm.warn, value, 'warn читается из настройки (' .. value .. ')')
end

-- Настройки нет — берём безопасное умолчание, а не nil: на nil сравнения ниже по файлу молча
-- становятся ложными, и проводка снова оказывается мёртвой, но уже незаметно.
opts['atmosfear_current_parameters.opt_blowout_warning'] = nil
sm.read_warning_option()
H.eq(sm.warn, 'nowarning', 'без настройки warn = nowarning')

-- --- судьба NPC -------------------------------------------------------------
for _, value in ipairs({ 'killatwave', 'killatend', 'turntozombie', 'explode' }) do
    opts['atmosfear_current_parameters.opt_blowout_fate'] = value
    H.eq(sm.get_fate(), value, 'судьба читается из настройки (' .. value .. ')')
end

opts['atmosfear_current_parameters.opt_blowout_fate'] = nil
H.eq(sm.get_fate(), 'killatwave', 'без настройки судьба = killatwave (как в поставке)')

-- --- apply_fate зовёт то, что обещает --------------------------------------
local called
local fake_self = {
    turn_to_zombie = function(_, obj, squad) called = 'zombie' end,
    explode = function(_, obj, squad) called = 'explode' end,
    delay_kill = function(_, id) called = 'kill' end,
    apply_fate = sm.CSurgeManager.apply_fate,
}
local se_obj = { id = 1 }

local cases = {
    { fate = 'turntozombie', want = 'zombie' },
    { fate = 'explode', want = 'explode' },
    { fate = 'killatwave', want = 'kill' },
    { fate = 'killatend', want = 'kill' },
    { fate = 'что-то незнакомое', want = 'kill' }, -- запасной путь обязан убивать, а не молчать
}
for _, c in ipairs(cases) do
    called = nil
    fake_self:apply_fate(c.fate, se_obj, {})
    H.eq(called, c.want, 'apply_fate при ' .. c.fate)
end

os.exit(H.report('surge_manager'))
