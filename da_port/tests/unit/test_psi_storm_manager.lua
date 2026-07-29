-- Психошторм: судьба NPC.
--
-- Зачем тест: в kill_obj_at_pos стоял литерал `local fate = "turntozombie"`, из-за чего четыре
-- ветки с убийством не исполнялись никогда, а настройка opt_psi_storm_fate не читалась ни одним
-- скриптом. Тест держит связь настройки с кодом.

local H = require('harness')

local env = H.base_env()

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
env.surge_manager = { SurgeManager = { ini = {} } }
env.game = { get_game_time = function() return 0 end }
env.level = { get_time_factor = function() return 10 end }

local psi, err = H.load(H.script('psi_storm_manager.script'), env)
if not psi then
    io.write('  ПРОВАЛ psi_storm_manager: не загрузился: ', tostring(err), '\n')
    os.exit(1)
end

for _, value in ipairs({ 'turntozombie', 'kill', 'none' }) do
    opts['atmosfear_current_parameters.opt_psi_storm_fate'] = value
    H.eq(psi.get_fate(), value, 'судьба читается из настройки (' .. value .. ')')
end

opts['atmosfear_current_parameters.opt_psi_storm_fate'] = nil
H.eq(psi.get_fate(), 'turntozombie', 'без настройки судьба = turntozombie (как в поставке)')

os.exit(H.report('psi_storm_manager'))
