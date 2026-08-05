-- normal pp
t_rt 		= "$user$albedo"
t_noise		= "fx\\fx_noise2"

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("stub_notransform_postpr","postprocess")
			: fog	(false)
			: zb 	(false,false)
--	shader:sampler	("s_base0")	:texture("$user$albedo")	: clamp() : f_linear ()
--	shader:sampler	("s_base1")    	:texture("$user$albedo")	: clamp() : f_linear ()
--	shader:sampler	("s_noise")    	:texture("fx\\fx_noise2")	: f_linear ()

	shader:dx10texture	("s_base0", "$user$albedo")
	shader:dx10texture	("s_base1", "$user$albedo")
	shader:dx10texture	("s_noise", "fx\\fx_noise2")
	-- [DA_PORT] motion vectors, for the "r__motion_vectors 2" debug view
	shader:dx10texture	("s_velocity", "$user$velocity")
	-- [DA_PORT] reactive mask, for the "r__motion_vectors 4" debug view
	shader:dx10texture	("s_reactive", "$user$reactive")
	-- [DA_PORT] eye-space depth, for the "r__motion_vectors 5" view
	shader:dx10texture	("s_position", "$user$position")
	-- [DA_PORT] FSR 2 result, already at output resolution
	shader:dx10texture	("s_fsr2", "$user$fsr2_out")

	shader:dx10sampler	("smp_rtlinear")
	shader:dx10sampler	("smp_linear")
end

function l_special        (shader, t_base, t_second, t_detail)
	shader:begin	("stub_notransform_postpr","postprocess_CM")
			: fog	(false)
			: zb 	(false,false)
--	shader:sampler	("s_base0")	:texture("$user$albedo")	: clamp() : f_linear ()
--	shader:sampler	("s_base1")    	:texture("$user$albedo")	: clamp() : f_linear ()
--	shader:sampler	("s_noise")    	:texture("fx\\fx_noise2")	: f_linear ()

	shader:dx10texture	("s_base0", "$user$albedo")
	shader:dx10texture	("s_base1", "$user$albedo")
	shader:dx10texture	("s_noise", "fx\\fx_noise2")

	-- [DA_PORT] Те же привязки, что у normal выше, и это не украшение.
	--
	-- Постобработка выбирает между этими двумя вариантами по одному признаку: включена ли
	-- цветокоррекция. Наш порт добавил цели только в normal, поэтому стоило зоне включить свою
	-- цветокоррекцию, кадр уходил сюда - а здесь выход апскейлера не был привязан вовсе. DLSS
	-- собирал кадр, его выбрасывали, и на экран растягивалась сырая сцена вместе с субпиксельным
	-- сдвигом. Выглядело как дрожание мира внутри аномальной зоны.
	--
	-- Признак, по которому это нашлось: r__motion_vectors 2 внутри зоны не работал, снаружи
	-- работал. Отладочный показ читает s_velocity - непривязанную здесь.
	shader:dx10texture	("s_velocity", "$user$velocity")
	shader:dx10texture	("s_reactive", "$user$reactive")
	shader:dx10texture	("s_position", "$user$position")
	shader:dx10texture	("s_fsr2", "$user$fsr2_out")

	shader:dx10sampler	("smp_rtlinear")
	shader:dx10sampler	("smp_linear")

--	shader:sampler	("s_grad1")    	:texture("grad\\grad_red_yellow")	: clamp() : f_linear ()
--	shader:sampler	("s_grad1")    	:texture("grad\\grad_test1")	: clamp() : f_linear ()
	shader:dx10texture	("s_grad0", "$user$cmap0")
	shader:dx10texture	("s_grad1", "$user$cmap1")
end