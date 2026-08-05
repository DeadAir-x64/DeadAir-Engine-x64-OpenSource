-- [DA_PORT] Студень: стоячая вода. Пиксельный шейдер water_green вместо water_soft — тот же наш
-- водный шейдер, но с зелёным профилем (см. блок DA_WATER_GREEN в water.ps). Проточная вода
-- (effects_water.s) осталась на water_soft.
local tex_base                = "water\\water_water"
local tex_nmap                = "water\\water_normal"
local tex_dist                = "water\\water_dudv"
local tex_env0                = "$user$sky0"         -- "sky\\sky_8_cube"
local tex_env1                = "$user$sky1"         -- "sky\\sky_8_cube"

--local tex_leaves              = "decal\\decal_listja"
--local tex_leaves              = "decal\\decal_listja_vetki"
-- [DA_PORT] Настоящие листья вместо пены. Слот s_leaves читает water.ps и кладёт сор ПОВЕРХ
-- пены. Стояла водная пена, то есть слой рисовал пену поверх пены и не давал ничего.
-- Обе текстуры листьев лежат в архивах игры (levels.xdb0..4, xtra.xdb0).
local tex_leaves              = "decal\\decal_listja_vetki"

function normal                (shader, t_base, t_second, t_detail)
	shader	:begin		("water_soft","water_green")
    		:sorting	(2, false)
			:blend		(true,blend.srcalpha,blend.invsrcalpha)
			:zb			(true,false)
			:distort	(true)
			:fog		(true)
			-- [DA_PORT] Вода помечает себя в трафарете, бит 0x02. По этой отметке
			-- phase_reactive_water пишет реактивность: апскейлер не должен доверять
			-- накопленной истории там, где вода, потому что вода не пишет ни глубины,
			-- ни векторов движения — историю ей восстанавливают по векторам ДНА.
			--
			-- Маска записи 2: общий бит 0x01 не трогается вовсе, поэтому для света и
			-- отражений (они сравнивают трафарет с 0x01, в том числе на равенство)
			-- ничего не меняется. Гасится бит в том же проходе, что и читается.
			:dx10stencil	(true, cmp_func.always, 255, 2,
							 stencil_op.keep, stencil_op.replace, stencil_op.keep)
			:dx10stencil_ref	(3)
--  shader:sampler        ("s_base")       :texture  (tex_base)
--  shader:sampler        ("s_nmap")       :texture  (tex_nmap)
--  shader:sampler        ("s_env0")       :texture  (tex_env0)   : clamp()
--  shader:sampler        ("s_env1")       :texture  (tex_env1)   : clamp()
--  shader:sampler        ("s_position")       :texture  ("$user$position")

	shader:dx10texture	("s_base",		tex_base)
	shader:dx10texture	("s_nmap",		tex_nmap)
	shader:dx10texture	("s_env0",		tex_env0)
	shader:dx10texture	("s_env1",		tex_env1)
	shader:dx10texture	("s_position",	"$user$position")

	shader:dx10texture	("s_leaves",	tex_leaves)
	shader:dx10texture	("s_image",	"$user$ssr")	-- [DA_PORT] rt_SSR: our water.ps samples s_image, so EVERY script using water_soft must bind it

	shader:dx10sampler	("smp_base")
	shader:dx10sampler	("smp_nofilter")
	shader:dx10sampler	("smp_rtlinear")
end

function l_special        (shader, t_base, t_second, t_detail)
	shader	:begin                ("waterd_soft","waterd_soft")
			:sorting        (2, true)
			:blend                (true,blend.srcalpha,blend.invsrcalpha)
			:zb                (true,false)
			:fog                (false)
			:distort        (true)

	shader: dx10color_write_enable( true, true, true, false)

--  shader:sampler        ("s_base")       :texture  (tex_base)
--  shader:sampler        ("s_distort")    :texture  (tex_dist)
--  shader:sampler        ("s_position")       :texture  ("$user$position")

	shader:dx10texture	("s_base",		tex_base)
	shader:dx10texture	("s_distort",	tex_dist)
	shader:dx10texture	("s_position",	"$user$position")

	shader:dx10sampler	("smp_base")
	shader:dx10sampler	("smp_nofilter")	
end