-- [DA_PORT] Прибор разницы кадров. Разбор - в r4_rendertarget_phase_frame_diff.cpp.

function normal (shader, t_base, t_second, t_detail)
	-- da_fullscreen, а не стоковая заглушка: у тех разметка вершин не совпадает с FVF::F_TL, которым
	-- рисуются эти проходы, и несовпадение заставляет D3D11 молча выбросить вызов.
	shader:begin	("da_fullscreen","da_frame_diff")
			: fog	(false)
			: zb 	(false,false)

	shader:dx10texture	("s_image",   "$user$fsr2_out")
	shader:dx10texture	("s_history", "$user$diff_prev")
	shader:dx10sampler	("smp_nofilter")
end
