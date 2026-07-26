-- [DA_PORT] Reactive widening, second axis. Reads the half-finished pass out of scratch and writes the
-- finished mask back into scratch2, folding in the mask the G-buffer left as it goes.

function normal (shader, t_base, t_second, t_detail)
	shader:begin	("da_fullscreen","da_reactive_dilate")
			: fog	(false)
			: zb 	(false,false)

	shader:dx10texture	("s_motion",   "$user$reactive_scratch")
	shader:dx10texture	("s_reactive", "$user$reactive")

	shader:dx10sampler	("smp_nofilter")
end
