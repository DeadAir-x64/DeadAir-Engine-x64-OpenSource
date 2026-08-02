-- [DA_PORT] Метка самосветящейся геометрии в маске реактивности, см. da_reactive_emissive.ps.
-- Проход идёт сразу после отрисовки свечения, пока трафарет ещё помнит, где оно легло.

function normal (shader, t_base, t_second, t_detail)
	-- da_fullscreen, а не штатная заглушка: заглушки объявляют раскладку вершины, которая не
	-- совпадает с FVF::F_TL у g_combine, и DirectX молча отбрасывает такой draw.
	shader:begin	("da_fullscreen","da_reactive_emissive")
			: fog	(false)
			: zb 	(false,false)

	-- Ни одной текстуры и ни одного сэмплера: проход пишет константу там, где разрешил трафарет.
	-- Состояние трафарета ставится из движка ПОСЛЕ set_Element - StateManager применяется в самом
	-- Render() и перекрывает блок состояний шейдера.
end
