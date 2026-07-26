-- [DA_PORT] Reactive mask widening: one full-screen pass, run just before the upscaler.
-- See da_reactive.ps for what it does and why the raw motion vector was the wrong signal.

function normal (shader, t_base, t_second, t_detail)
	-- da_fullscreen, not one of the stock stubs: those declare vertex layouts that do not match the
	-- FVF::F_TL geometry these passes are drawn with, and a mismatch makes D3D11 drop the draw silently.
	shader:begin	("da_fullscreen","da_reactive")
			: fog	(false)
			: zb 	(false,false)

	-- The motion buffer, the depth the camera's share is subtracted with, and the mask the G-buffer
	-- already wrote - this pass only ever adds to it.
	shader:dx10texture	("s_velocity", "$user$velocity")
	shader:dx10texture	("s_position", "$user$position")
	shader:dx10texture	("s_reactive", "$user$reactive")

	-- Point sampling throughout: these are per-pixel quantities and interpolating them would blend a
	-- moving pixel with a still one, which is exactly the distinction this pass is built on.
	shader:dx10sampler	("smp_nofilter")
end
