-- [DA_PORT] Sky motion vectors: one full-screen pass over the velocity buffer, before the upscaler.
-- See da_sky_velocity.ps for why the sky shader itself cannot do this.

function normal (shader, t_base, t_second, t_detail)
	shader:begin	("da_fullscreen","da_sky_velocity")
			: fog	(false)
			: zb 	(false,false)

	-- Depth only: it is what tells sky from everything else. The pass writes into the velocity buffer
	-- and never reads it, because every pixel that is not sky is discarded.
	shader:dx10texture	("s_position", "$user$position")

	shader:dx10sampler	("smp_nofilter")
end
