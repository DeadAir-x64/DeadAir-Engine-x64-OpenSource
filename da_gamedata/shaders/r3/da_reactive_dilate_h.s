-- [DA_PORT] Reactive widening, first axis. Reads the motion the first pass wrote into scratch2 and
-- writes the half-finished result into scratch - see da_reactive_dilate.ps for why it takes two.
--
-- Two blenders rather than one with a switch: a texture cannot be read while it is the target being
-- drawn into, so each axis needs its source named separately. They share the same pixel shader.

function normal (shader, t_base, t_second, t_detail)
	shader:begin	("da_fullscreen","da_reactive_dilate")
			: fog	(false)
			: zb 	(false,false)

	shader:dx10texture	("s_motion",   "$user$reactive_scratch2")
	shader:dx10texture	("s_reactive", "$user$reactive")

	shader:dx10sampler	("smp_nofilter")
end
