-- [DA_PORT] Loose-override: проход СВЕЧЕНИЯ переведён на da_emissive_model.vs.
--
-- Было shadow_direct_model - вершинный шейдер КАРТЫ ТЕНЕЙ, в котором джиттера нет и быть не должно.
-- В основном кадре из-за этого свечение предмета рисовалось без сдвига, а непрозрачная часть того
-- же предмета - со сдвигом; апскейлер, снимая сдвиг со всего кадра, промахивался ровно по свечению.
-- Отсюда пила по кромке светящейся палочки и мерцание ламп. Подробнее - в da_emissive_model.vs.
--
-- Остальное в файле не тронуто ни на байт.

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("deffer_model_flat","deffer_base_flat")
			: fog		(false)
			: emissive 	(true)
--	shader:sampler	("s_base")      :texture	(t_base)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	shader:dx10stencil	( 	true, cmp_func.always, 
							255 , 127, 
							stencil_op.keep, stencil_op.replace, stencil_op.keep)
	shader:dx10stencil_ref	(1)
	shader: dx10color_write_enable( true, true, true, false)
end

function l_special	(shader, t_base, t_second, t_detail)
	shader:begin	("da_emissive_model",	"accum_emissivel")
			: zb 		(true,false)
			: fog		(false)
			: emissive 	(true)
	shader: dx10color_write_enable( true, true, true, false)
end
