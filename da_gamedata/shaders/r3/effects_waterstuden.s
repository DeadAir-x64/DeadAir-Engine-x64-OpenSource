-- [DA_PORT] РЎС‚СѓРґРµРЅСЊ: СЃС‚РѕСЏС‡Р°СЏ РІРѕРґР°. РџРёРєСЃРµР»СЊРЅС‹Р№ С€РµР№РґРµСЂ water_green РІРјРµСЃС‚Рѕ water_soft вЂ” С‚РѕС‚ Р¶Рµ РЅР°С€
-- РІРѕРґРЅС‹Р№ С€РµР№РґРµСЂ, РЅРѕ СЃ Р·РµР»С‘РЅС‹Рј РїСЂРѕС„РёР»РµРј (СЃРј. Р±Р»РѕРє DA_WATER_GREEN РІ water.ps). РџСЂРѕС‚РѕС‡РЅР°СЏ РІРѕРґР°
-- (effects_water.s) РѕСЃС‚Р°Р»Р°СЃСЊ РЅР° water_soft.
local tex_base                = "water\\water_water"
local tex_nmap                = "water\\water_normal"
local tex_dist                = "water\\water_dudv"
local tex_env0                = "$user$sky0"         -- "sky\\sky_8_cube"
local tex_env1                = "$user$sky1"         -- "sky\\sky_8_cube"

--local tex_leaves              = "decal\\decal_listja"
--local tex_leaves              = "decal\\decal_listja_vetki"
-- [DA_PORT] РќР°СЃС‚РѕСЏС‰РёРµ Р»РёСЃС‚СЊСЏ РІРјРµСЃС‚Рѕ РїРµРЅС‹. РЎР»РѕС‚ s_leaves С‡РёС‚Р°РµС‚ water.ps Рё РєР»Р°РґС‘С‚ СЃРѕСЂ РџРћР’Р•Р РҐ
-- РїРµРЅС‹. РЎС‚РѕСЏР»Р° РІРѕРґРЅР°СЏ РїРµРЅР°, С‚Рѕ РµСЃС‚СЊ СЃР»РѕР№ СЂРёСЃРѕРІР°Р» РїРµРЅСѓ РїРѕРІРµСЂС… РїРµРЅС‹ Рё РЅРµ РґР°РІР°Р» РЅРёС‡РµРіРѕ.
-- РћР±Рµ С‚РµРєСЃС‚СѓСЂС‹ Р»РёСЃС‚СЊРµРІ Р»РµР¶Р°С‚ РІ Р°СЂС…РёРІР°С… РёРіСЂС‹ (levels.xdb0..4, xtra.xdb0).
local tex_leaves              = "decal\\decal_listja_vetki"

function normal                (shader, t_base, t_second, t_detail)
	shader	:begin		("water_soft","water_green")
    		:sorting	(2, false)
			:blend		(true,blend.srcalpha,blend.invsrcalpha)
			:zb			(true,false)
			:distort	(true)
			:fog		(true)
			-- [DA_PORT] Р’РѕРґР° РїРѕРјРµС‡Р°РµС‚ СЃРµР±СЏ РІ С‚СЂР°С„Р°СЂРµС‚Рµ, Р±РёС‚ 0x02 вЂ” РєР°Рє СЃР°РјРѕСЃРІРµС‚СЏС‰Р°СЏСЃСЏ
			-- РіРµРѕРјРµС‚СЂРёСЏ. РџРѕ СЌС‚РѕР№ РѕС‚РјРµС‚РєРµ phase_reactive_transparent РїРёС€РµС‚ СЂРµР°РєС‚РёРІРЅРѕСЃС‚СЊ:
			-- Р°РїСЃРєРµР№Р»РµСЂ РЅРµ РґРѕР»Р¶РµРЅ РґРѕРІРµСЂСЏС‚СЊ РЅР°РєРѕРїР»РµРЅРЅРѕР№ РёСЃС‚РѕСЂРёРё С‚Р°Рј, РіРґРµ РІРѕРґР°, РїРѕС‚РѕРјСѓ С‡С‚Рѕ
			-- РІРѕРґР° РЅРµ РїРёС€РµС‚ РЅРё РіР»СѓР±РёРЅС‹, РЅРё РІРµРєС‚РѕСЂРѕРІ РґРІРёР¶РµРЅРёСЏ вЂ” РёСЃС‚РѕСЂРёСЋ РµР№ РІРѕСЃСЃС‚Р°РЅР°РІР»РёРІР°СЋС‚
			-- РїРѕ РІРµРєС‚РѕСЂР°Рј Р”РќРђ.
			--
			-- РњР°СЃРєР° Р·Р°РїРёСЃРё 2: РѕР±С‰РёР№ Р±РёС‚ 0x01 РЅРµ С‚СЂРѕРіР°РµС‚СЃСЏ, РїРѕСЌС‚РѕРјСѓ СЃРІРµС‚ Рё РѕС‚СЂР°Р¶РµРЅРёСЏ (РѕРЅРё
			-- СЃСЂР°РІРЅРёРІР°СЋС‚ С‚СЂР°С„Р°СЂРµС‚ СЃ 0x01, РІ С‚РѕРј С‡РёСЃР»Рµ РЅР° СЂР°РІРµРЅСЃС‚РІРѕ) СЂР°Р±РѕС‚Р°СЋС‚ РєР°Рє РїСЂРµР¶РґРµ.
						-- [DA_PORT] Вода рисуется ОДИН раз на пиксель.
			--
			-- Водоёмы на уровне — это отдельные плоскости, и на болотах они перекрываются.
			-- Вода тестирует глубину, но НЕ пишет её (zb(true,false)), поэтому второй кусок
			-- первым не отсекается: оба проходят тест и оба смешиваются. Там, где куски
			-- налезают друг на друга, слой ложится дважды — на экране это белёсая пелена, а
			-- при движении камеры она ещё и ходит, потому что порядок сортировки прозрачных
			-- объектов меняется.
			--
			-- Лечится тем же трафаретом, которым вода метит себя для маски реактивности:
			-- сравнение notequal по биту 0x02 пропускает только первый слой, второй
			-- отбрасывается ещё до смешивания.
			--
			-- Бит 0x02 занят исключительно водой — проверено по всем шейдерам: самосветящаяся
			-- геометрия пишет 0x01, и её вода по-прежнему перекрывает.
			-- [DA_PORT] Сравнение «всегда», а не «не равно».
			--
			-- «Не равно» пускало на пиксель только ПЕРВУЮ нарисованную поверхность воды. Глубина
			-- у воды только читается (zb(true,false)), строгой сортировки нет — и первой
			-- оказывалась то ближняя, то дальняя, как ляжет порядок по площади на экране.
			-- Досталась дальней — ближнюю уже не пускают, и сквозь воду видно дно.
			--
			-- Так и выглядел дефект: тёмный многоугольник с прямыми краями, который переезжал
			-- вместе со взглядом. Прямые края — это границы самих водных полигонов.
			--
			-- Отметку в трафарете оставляем: она нужна маске реактивности. Убран только запрет.
			:dx10stencil	(true, cmp_func.always, 2, 2,
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