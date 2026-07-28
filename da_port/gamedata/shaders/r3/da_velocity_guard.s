-- [DA_PORT] Velocity guard: one full-screen pass over the motion buffer, run just before the upscaler.
-- See da_velocity_guard.ps for what it does and why nothing else could do it.

function normal (shader, t_base, t_second, t_detail)
	-- da_fullscreen, not one of the stock stubs: those declare vertex layouts that do not match the
	-- FVF::F_TL geometry these passes are drawn with, and a mismatch makes D3D11 drop the draw silently.
	shader:begin	("da_fullscreen","da_velocity_guard")
			: fog	(false)
			: zb 	(false,false)

	-- The motion buffer being filtered, and the colour target whose alpha carries gloss - that is how
	-- a glossy surface is recognised without any extra data.
	shader:dx10texture	("s_velocity", "$user$velocity")
	shader:dx10texture	("s_diffuse",  "$user$albedo")

	-- Point sampling throughout: these are per-pixel quantities and interpolating them would blend a
	-- moving pixel with a still one, which is exactly the confusion this pass exists to undo.
	shader:dx10sampler	("smp_nofilter")
end
