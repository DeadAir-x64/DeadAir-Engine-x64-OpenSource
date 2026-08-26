#ifndef SLOAD_H
#define SLOAD_H

#include "common.h"
#include "da_hextile.h"

#define GLOSS_MUL 2//.1f

#ifdef	MSAA_ALPHATEST_DX10_1
#if MSAA_SAMPLES == 2
static const float2 MSAAOffsets[2] = { float2(4,4), float2(-4,-4) };
#endif
#if MSAA_SAMPLES == 4
static const float2 MSAAOffsets[4] = { float2(-2,-6), float2(6,-2), float2(-6,2), float2(2,6) };
#endif
#if MSAA_SAMPLES == 8
static const float2 MSAAOffsets[8] = { float2(1,-3), float2(-1,3), float2(5,1), float2(-3,-5), 
								               float2(-5,5), float2(-7,-1), float2(3,7), float2(7,-7) };
#endif
#endif	//	MSAA_ALPHATEST_DX10_1

//////////////////////////////////////////////////////////////////////////////////////////
// Bumped surface loader                //
//////////////////////////////////////////////////////////////////////////////////////////
struct	surface_bumped
{
	float4	base;
	float3	normal;
	float	gloss;
	float	height;

};

// [DA_PORT] ---- Detail-bump stability under a temporal upscaler ---------------------------------
// x = gloss damping, y = normal damping. Both zero by default, and at zero every line below collapses
// to a multiply by one, i.e. this file behaves exactly as the mod's original. Driven from the console
// by r__detail_gloss_fix / r__detail_normal_fix; see xr_ioc_cmd.cpp for the full diagnosis.
uniform float4 da_detail_fix;

// How fast the detail normal changes from one pixel to the next. Where this is large the tilt is finer
// than the sampling grid: the specular answer then depends on exactly where inside the texel the sample
// landed, the jitter moves that point every frame, and the upscaler locks the resulting noise into a
// standing pattern. Measured in SCREEN space on purpose - it needs no knowledge of how the detail map is
// encoded, and it still sees the frequency at grazing angles, where anisotropic filtering keeps alive
// what mip selection alone would have removed.
float da_detail_instability( float3 dN )
{
	return length( ddx(dN) ) + length( ddy(dN) );
}

// [DA_PORT] ---- Фильтрация блика по разбросу нормалей (specular antialiasing) -------------------
// x = сила, y = потолок добавки, z = показатель степени, w = отладка. При x = 0 множитель равен
// единице и файл ведёт себя ровно как прежде. Ручки: r__spec_aa, _max, _power, _debug.
uniform float4 da_spec_aa;

// Kaplanyan 2016 / Tokuyoshi 2017 / Tokuyoshi–Kaplanyan 2019. Замер тот же, что в Filament:
// дисперсия ИТОГОВОЙ нормали по экрану. Итоговой — это важно: так мера видит и базовый рельеф, и
// деталь, и наш отрицательный сдвиг мипов под апскейлером, а не одну лишь детальную карту.
//
// ⛔ Первоисточник переводит дисперсию в шероховатость. У нас ширину блика задаёт таблица
// s_material из четырёх РАЗНЫХ моделей, ось немонотонная (см. xr_ioc_cmd.cpp), и расширить
// лепесток попиксельно нельзя. Остаётся исходная формулировка Toksvig 2005: если блик не
// расширить, его надо ровно настолько же приглушить. Для показателя n лепесток с дисперсией s^2
// равносилен показателю n/(1+n*s^2), а высота пика падает во столько же раз.
//
// Потолок обязателен: без него далёкая мелкая геометрия уводит множитель в ноль и металл
// становится матовым. 0.15 — значение по умолчанию в Filament.
float da_spec_aa_factor( float3 N )
{
	float3 du	= ddx( N );
	float3 dv	= ddy( N );
	float  var	= da_spec_aa.x * ( dot(du,du) + dot(dv,dv) );
	float  kern	= min( 2.0h * var, da_spec_aa.y );
	return 1.0h / ( 1.0h + da_spec_aa.z * kern );
}

float4 tbase( float2 tc )
{
	// [DA_PORT] Разрыв повторов — и здесь тоже. Через эту функцию идут ПЛОСКИЕ поверхности, то есть
	// две трети мира: у 64% текстур карты рельефа нет вовсе, и в sload_i они не заходят.
	//
	// Условие проверяем ДО расчёта решётки, а не после: внутри есть ddx/ddy, и держать их под
	// выключенной ручкой значило бы платить за приём, который не работает. Ветвление здесь
	// равномерное (значение приходит из константного буфера, одно на весь вызов отрисовки), поэтому
	// производные внутри него законны — в отличие от случая, который мы разбирали у параллакса.
#ifdef DA_HEX_ALLOW
	if ( da_hex_enabled() )
	{
		da_hex_setup H = da_hex_prepare( tc );
		float4 c; float3 W;
		da_hex_sample_base( c, W, s_base, smp_base, H );
		return c;
	}
#endif
	return	s_base.Sample( smp_base, tc);
}

// [DA_PORT] Крутой параллакс: настройки снаружи вместо зашитых чисел. См. xr_ioc_cmd.cpp.
// x = начало затухания (м), y = конец (м), z = глубина продавливания, w = сила собственной тени
// [DA_PORT] Собственный сдвиг мипов детальной текстуры (.x), в мипах. Разбор - у ручки
// r__detail_mipbias в движке. Ноль = как было.

// [DA_PORT] ХЕШИРОВАННЫЙ АЛЬФА-ТЕСТ (Wyman & McGuire, 2017). Ручка r__alpha_hash, ноль - как было.
//
// Зачем. Листва и трава рисуются жёстким clip() по одному порогу: пиксель либо целиком есть, либо
// целиком нет, полутонов не бывает. Ветка тоньше пикселя проходит или не проходит проверку в
// зависимости от того, куда субпиксельное дрожание поставило выборку, - и решение перещёлкивается
// каждый кадр. Временному фильтру усреднять нечего: ему приходит не "полупрозрачно", а "да/нет".
//
// Приём: порог берётся не постоянный, а СЛУЧАЙНЫЙ на каждый пиксель и каждый кадр. Тогда доля
// прошедших проверку кадров равна альфе - то есть покрытие в среднем ВЕРНОЕ, - а временное
// накопление превращает эту случайность в правильную полупрозрачность. Осмысленно только в паре с
// накоплением, поэтому по умолчанию выключено и включается вместе с апскейлером или нашей TAA.
//
// da_alpha_hash: x = сила (0 - прежний постоянный порог, 1 - полностью случайный), y = поворот
// последовательности от кадра к кадру.
uniform float4 da_alpha_hash;

float da_hashed_aref( float2 pix, float aref )
{
	[branch] if ( da_alpha_hash.x <= 0.001f )
		return aref;

	float h = frac( sin( dot( pix + da_alpha_hash.y, float2( 12.9898f, 78.233f ) ) ) * 43758.5453f );
	return lerp( aref, h, da_alpha_hash.x );
}

// [DA_PORT] Крупный слой вариации дальнего плана: x = сила, y = 1/шаг повторения,
// z = метры начала, w = метры полной силы. Разбор — у ручки r__macro_var в движке.
uniform float4 da_macro_var;
// [DA_PORT] x = сила подмены оттенка дали («фальшивая трава»). Разбор — у r__macro_tint.
uniform float4 da_macro_var2;
// [DA_PORT] Щиты дальней растительности: x = доля рассеянного света, y = насыщенность,
// z = яркость. Разбор — у ручки r__lod_hemi. Единицы = прежнее поведение.
uniform float4 da_lod_tune;

uniform float4 da_detail_bias;

uniform float4 da_parallax;
// x = шагов поиска max, y = шагов поиска min, z = шагов луча к солнцу, w = режим отладки
uniform float4 da_parallax2;

// [DA_PORT] Собственная тень рельефа. Единица = «ничего не затенено» — ровно это и видят шейдеры,
// собранные БЕЗ крутого параллакса, поэтому домножать на неё можно безусловно.
static float da_parallax_shadow = 1.0h;
// Высота в найденной точке и признак «ветка параллакса здесь отработала» — только для отладки.
// Второй нужен потому, что без него «поправка мала» и «этот код тут не выполняется вовсе» на экране
// выглядят одинаково, а перепутать их мы уже успели на детальных текстурах.
static float da_parallax_height = 0.0h;
static float da_parallax_hit = 0.0h;

#if defined(ALLOW_STEEPPARALLAX) && defined(USE_STEEPPARALLAX)

// [DA_PORT] Собственная тень рельефа: луч от найденной точки к солнцу по той же карте высот.
//
// Зачем. Без неё параллакс читается как «текстура плывёт»: глубина есть, а тени в канавке нет, и
// глаз отказывается считать это рельефом — смещение он засчитывает как дефект. С тенью та же самая
// карта высот начинает читаться как геометрия. Это самая крупная прибавка к виду из всего, что
// вообще можно сделать в этом шейдере, и стоит она один цикл выборок.
//
// Откуда направление. L_sun_dir_e объявлена в shared/common.h, в блоке static_globals — она есть у
// КАЖДОГО шейдера, включая проход G-буфера, так что заводить новую константу не потребовалось.
// Смотрит она ВДОЛЬ хода лучей (от солнца к миру), как и Ldynamic_dir в lmodel.h, поэтому вектор
// «на солнце» — это минус она.
//
// ⚠️ Куда пишется результат. Свободного места в G-буфере НЕТ: при GBUFFER_OPTIMIZATION вся
// четвёртая составляющая rt_Position — один упакованный float, где 8 бит заняты hemi, 5 битов
// mtl, а младшие 13 непригодны (цель хранит FP16 — см. комментарий у USABLE_BIT_12 в
// common_functions.h). Поэтому результат уходит в АЛЬБЕДО, разбор — в deffer_base_bump.ps.
//
// ⛔ Первая версия множила hemi и не дала видимого НИЧЕГО: hemi кормит только рассеянный свет,
// а он в combine_1.ps складывается с солнечным, и на освещённой стене его доля мала.
float da_parallax_selfshadow( float2 tc, float h0, float3 Lts, float scale, float2 dTcDx, float2 dTcDy )
{
	// Солнце ниже касательной плоскости: поверхность к нему и так не повёрнута, освещение само даст
	// ноль. Возвращаем ЕДИНИЦУ, а не ноль — иначе мы бы срезали ещё и рассеянный свет, и все
	// отвёрнутые от солнца стены разом потемнели бы без всякой причины.
	if ( Lts.z <= 0.001h )
		return 1.0h;

	int    steps = (int)da_parallax2.z;
	float  inv   = 1.0h / steps;
	// Подъём за шаг и отвечающий ему боковой сдвиг. Связка та же, что у основного поиска ниже: там
	// спуск на всю высоту стоит vDelta, здесь подъём на остаток высоты стоит столько же.
	//
	// Делить на Lts.z, как пишут в учебниках, НЕЛЬЗЯ: при низком солнце длина луча уходит в
	// бесконечность и тень растягивается через всю текстуру. Ограничение сдвига здесь ровно такое
	// же, как у вектора на камеру ниже, и стоит по той же причине.
	float  dh   = ( 1.0h - h0 ) * inv;
	float2 dtc  = Lts.xy * scale * dh;

	float occ = 0.0h;
	[loop]
	for ( int i = 1; i <= steps; ++i )
	{
		float hr = h0 + dh * i;                                        // высота луча
		float hs = s_bumpX.SampleGrad( smp_base, tc + dtc * i, dTcDx, dTcDy ).a; // высота поверхности
		// Взвешено расстоянием: ближняя стенка канавки затеняет резче дальней. Так тень выходит
		// плотной у препятствия и мягкой на краю, без ступенек от числа шагов.
		occ = max( occ, ( hs - hr ) * ( 1.0h - i * inv ) );
	}

	// Ручка И ЕСТЬ плотность: перекрытие живёт в долях высоты рельефа и редко переваливает за 0.15,
	// поэтому осмысленные значения r__parallax_shadow лежат в районе единиц, а не долей единицы.
	return saturate( 1.0h - occ * da_parallax.w );
}

void UpdateTC( inout p_bumped I)
{
	if (I.position.z < da_parallax.y)
	{
		float maxSamples = da_parallax2.x;
		float minSamples = da_parallax2.y;
		float fParallaxOffset = -da_parallax.z;

		float3	 eye = mul (float3x3(I.M1.x, I.M2.x, I.M3.x,
									 I.M1.y, I.M2.y, I.M3.y,
									 I.M1.z, I.M2.z, I.M3.z), -I.position.xyz);

		eye = normalize(eye);

		// [DA_PORT] Затухание считаем ЗДЕСЬ, а не в конце: оно нужно дважды — сначала чтобы срезать
		// число шагов по дальности, потом чтобы погасить сам сдвиг. Раньше стояло только второе.
		float	fParallaxFade 	= smoothstep(da_parallax.y, da_parallax.x, I.position.z);

		//	Calculate number of steps
		// [DA_PORT] Шагов меньше не только на скользящем угле, но и ПО ДАЛЬНОСТИ.
		//
		// Исходный код считал одинаково подробно хоть в упор, хоть у самой границы. При прежних 12
		// метрах разница терялась, но мы отодвинули границу до 45 — а площадь экрана, занятая
		// рельефом, растёт с дальностью быстрее, чем линейно. Без этой поправки расширение дальности
		// оплачивалось бы ровно во столько же раз возросшей ценой прохода.
		//
		// Далёкая стена занимает единицы пикселей: подробность там всё равно не видна, и минимума
		// шагов ей достаточно. Ноль экономии у камеры, весь выигрыш — на дальнем плане.
		float nNumSteps = lerp( minSamples, lerp( maxSamples, minSamples, eye.z ), fParallaxFade );

		float	fStepSize			= 1.0 / nNumSteps;
		float2	vDelta				= eye.xy * fParallaxOffset*1.2;
		float2	vTexOffsetPerStep	= fStepSize * vDelta;

		// [DA_PORT] Производные берём ЗДЕСЬ, пока ход управления РАВНОМЕРЕН по квадру.
		//
		// Ниже начнётся цикл, где соседние пиксели делают разное число шагов. Всё, что внутри
		// него зовёт Sample без явного мипа, получает производные от расходящегося управления —
		// по правилам HLSL это неопределённое поведение, а на деле каждый пиксель берёт СВОЙ
		// случайный уровень. Ровно это давало кашу и лучевые смазы на кладке.
		//
		// Мип 0 вместо этого — тоже неверно, но иначе: на наклонной стене вдали карта высот
		// начинает мерцать от пикселя к пикселю, и найденное пересечение скачет.
		float2 dTcDx = ddx( I.tcdh.xy );
		float2 dTcDy = ddy( I.tcdh.xy );

		//	Prepare start data for cycle
		float2	vTexCurrentOffset	= I.tcdh;
		float	fCurrHeight			= 0.0;
		float	fCurrentBound		= 1.0;

		// [DA_PORT] [loop] обязателен. Раньше границей был литерал 25 и компилятор мог развернуть
		// цикл; теперь число шагов приходит из константного буфера, развернуть его нельзя. Без
		// явного указания fxc либо отказывается собирать шейдер, либо падает сам, не назвав ни
		// файла, ни строки — ровно так вёл себя цикл SSR в мягкой воде.
		[loop]
		for( int i=0; i<nNumSteps; ++i )
		{
			if (fCurrHeight < fCurrentBound)
			{	
				vTexCurrentOffset += vTexOffsetPerStep;		
				fCurrHeight = s_bumpX.SampleGrad( smp_base, vTexCurrentOffset.xy, dTcDx, dTcDy ).a; 
				fCurrentBound -= fStepSize;
			}
		}

/*
		[unroll(25)]	//	Doesn't work with [loop]
		for( ;fCurrHeight < fCurrentBound; fCurrentBound -= fStepSize )
		{
			vTexCurrentOffset += vTexOffsetPerStep;		
			fCurrHeight = s_bumpX.SampleLevel( smp_base, vTexCurrentOffset.xy, 0 ).a; 
		}
*/
		// [DA_PORT] Двоичное уточнение пересечения вместо одной линейной прикидки.
		//
		// Это и есть лекарство от «ступенек» на кромках кирпича. Линейный поиск выше даёт лишь
		// интервал: последняя точка НАД поверхностью и первая ПОД ней. Прежний код проводил
		// через них прямую и брал её пересечение — прикидка первого порядка, которой хватает,
		// только если высота между двумя шагами меняется линейно. У кладки она меняется
		// обрывом: кирпич кончается стенкой. На обрыве прикидка промахивается, и промах
		// квантован длиной шага — отсюда лесенка, которую не лечит увеличение числа шагов.
		// Так это и описано у Татарчук (ATI, 2006): линейный поиск без двоичного оставляет
		// ступеньки даже при высокой частоте выборок.
		//
		// Пять делений сокращают интервал в 32 раза — этого достаточно, чтобы кромка легла
		// внутрь пикселя. Цена — пять выборок, против 8..32 у линейного поиска.
		float2	tcAbove = vTexCurrentOffset - vTexOffsetPerStep; // ещё НАД поверхностью
		float2	tcBelow = vTexCurrentOffset;                     // уже ПОД ней
		float	bAbove = fCurrentBound + fStepSize;
		float	bBelow = fCurrentBound;
		[unroll]
		for ( int b = 0; b < 5; ++b )
		{
			float2 tcMid = ( tcAbove + tcBelow ) * 0.5h;
			float  bMid  = ( bAbove  + bBelow  ) * 0.5h;
			float  hMid  = s_bumpX.SampleGrad( smp_base, tcMid, dTcDx, dTcDy ).a;
			// Луч выше поверхности — сдвигаем ближнюю границу, иначе дальнюю.
			if ( hMid < bMid ) { tcAbove = tcMid; bAbove = bMid; }
			else               { tcBelow = tcMid; bBelow = bMid; }
		}
		float2	vTexCoord = ( tcAbove + tcBelow ) * 0.5h;
		// Затухание по дальности — тем же смыслом, что и раньше: гасим САМ СДВИГ к нулю.
		vTexCoord = lerp( I.tcdh.xy, vTexCoord, fParallaxFade );

		//	Output the result
		I.tcdh = vTexCoord;

		// [DA_PORT] Собственная тень — по тем же координатам, что и цвет, и с ТЕМ ЖЕ затуханием.
		// На дальней границе рельеф уже плоский, и тень обязана исчезнуть вместе с ним: иначе на
		// стыке повиснет тёмная полоса, у которой на экране нет видимой причины.
		if ( da_parallax.w > 0.001h )
		{
			float3 Lts = normalize( mul( float3x3(I.M1.x, I.M2.x, I.M3.x,
												  I.M1.y, I.M2.y, I.M3.y,
												  I.M1.z, I.M2.z, I.M3.z), -L_sun_dir_e ) );
			float  h0  = s_bumpX.SampleGrad( smp_base, vTexCoord, dTcDx, dTcDy ).a;
			float  sh  = da_parallax_selfshadow( vTexCoord, h0, Lts, da_parallax.z * 1.2, dTcDx, dTcDy );
			da_parallax_shadow = lerp( 1.0h, sh, fParallaxFade );
			da_parallax_height = h0;
		}
		da_parallax_hit = 1.0h;

#if defined(USE_TDETAIL) && defined(USE_STEEPPARALLAX)
		I.tcdbump = vTexCoord * dt_params;
#endif
	}

}

#elif	defined(USE_PARALLAX) || defined(USE_STEEPPARALLAX)

void UpdateTC( inout p_bumped I)
{
	float3	 eye = mul (float3x3(I.M1.x, I.M2.x, I.M3.x,
								 I.M1.y, I.M2.y, I.M3.y,
								 I.M1.z, I.M2.z, I.M3.z), -I.position.xyz);
								 
	float	height	= s_bumpX.Sample( smp_base, I.tcdh).w;	//
			//height  /= 2;
			//height  *= 0.8;
			height	= height*(parallax.x) + (parallax.y);	//
	float2	new_tc  = I.tcdh + height * normalize(eye);	//

	//	Output the result
	I.tcdh	= new_tc;
}

#else	//	USE_PARALLAX

void UpdateTC( inout p_bumped I)
{
	;
}

#endif	//	USE_PARALLAX

surface_bumped sload_i( p_bumped I)
{
	surface_bumped	S;

	// [DA_PORT] Разрыв повторов шестиугольной решёткой. Разбор — в da_hextile.h.
	//
	// Решётку считаем ТОЛЬКО при включённой ручке: внутри есть ddx/ddy, и держать их под выключенным
	// приёмом значило бы платить за него всегда. Ветвление равномерное — значение приходит из
	// константного буфера, одно на весь вызов отрисовки, — поэтому производные тут законны.
	da_hex_setup	H		= (da_hex_setup)0;
	#ifdef DA_HEX_ALLOW
	bool			hexOn	= da_hex_enabled();
#else
	bool			hexOn	= false;
#endif

	// Параллакс обязан идти по ГЛАВНОЙ плитке — по той карте высот, которую мы потом и покажем.
	// Иначе смещение считалось бы по одному кирпичу, а нарисован оказался бы другой, и объём
	// перестал бы совпадать с картинкой.
	if ( hexOn )
	{
		H = da_hex_prepare( I.tcdh.xy );
		I.tcdh.xy = H.stDom;
	}

	UpdateTC(I);	//	All kinds of parallax are applied here.

	float4	Nu, NuE;
	if ( hexOn )
	{
		// Поправку, найденную параллаксом, разносим на все три выборки.
		da_hex_shift( H, I.tcdh.xy - H.stDom );

		// Веса считаются ПО ЦВЕТУ и переиспользуются рельефом и высотой — иначе те разъехались бы
		// с ним по разным плиткам, и на стыке нормаль отвечала бы не тому кирпичу, что нарисован.
		float3 W;
		da_hex_sample_base( S.base, W, s_base, smp_base, H );
		Nu  = da_hex_sample_w( s_bump,  smp_base, H, W );
		NuE = da_hex_sample_w( s_bumpX, smp_base, H, W );
	}
	else
	{
	Nu	= s_bump.Sample( smp_base, I.tcdh );		// IN:	normal.gloss
	NuE	= s_bumpX.Sample( smp_base, I.tcdh);	// IN:	normal_error.height

	S.base		= tbase(I.tcdh);				//	IN:  rgb.a
	}
	S.normal	= Nu.wzy + (NuE.xyz - 1.0h);	//	(Nu.wzyx - .5h) + (E-.5)
	S.gloss		= Nu.x*Nu.x;					//	S.gloss = Nu.x*Nu.x;
	S.height	= NuE.z;
	//S.height	= 0;

#ifdef        USE_TDETAIL
#ifdef        USE_TDETAIL_BUMP
	float4 NDetail		= s_detailBump.SampleBias( smp_base, I.tcdbump, da_detail_bias.x);
	float4 NDetailX		= s_detailBumpX.SampleBias( smp_base, I.tcdbump, da_detail_bias.x);
	// [DA_PORT] Damped where the detail bump has gone finer than a pixel - see the top of this file.
	float3 da_dN		= NDetail.wzy + NDetailX.xyz - 1.0h; //	(Nu.wzyx - .5h) + (E-.5)
	// [DA_PORT] Weighted by the material's OWN gloss, and that is the whole point of the measure.
	// Detail textures are high-frequency everywhere by design, so frequency alone marks the entire
	// world and damping it is just a global loss of detail - measured 26.07, every surface went to
	// full weight at once. What separates the surfaces that break from the ones that do not is the
	// specular lobe: on metal it is narrow enough that a sub-pixel shift in the sample lands on a
	// different part of it and the pixel changes brightness every frame; on wood or brick the same
	// wobble is spread across a wide lobe and disappears. Verified by capture: with the camera held
	// still the barrel changed 5.4/255 per frame - as much as trees moving in the wind - while the
	// fence beside it, same detail texture, same distance, sat at 0.17 and had fully converged.
	float  da_inst		= da_detail_instability( da_dN ) * saturate(S.gloss);
	float  da_wn		= 1.0h - saturate(da_inst * da_detail_fix.y);	// weight on the normal
	float  da_wg		= 1.0h - saturate(da_inst * da_detail_fix.x);	// weight on the gloss
	// Hard overrides, so which half is guilty can be settled without first calibrating a scale.
	if (da_detail_fix.z > 3.5h && da_detail_fix.z < 4.5h)	da_wn = 0.0h;	// 4: no detail in the normal
	if (da_detail_fix.z > 4.5h && da_detail_fix.z < 5.5h)	da_wg = 0.0h;	// 5: no detail in the gloss
	// Neutral for the gloss is ONE, not zero: NDetail.x sits around .5 and GLOSS_MUL is 2, so the pair
	// multiplies to unity on average. Scaling the product by the weight would have driven gloss to zero
	// and flattened the metal instead of merely steadying it.
	S.gloss				= S.gloss * lerp( 1.0h, NDetail.x * GLOSS_MUL, da_wg );
	S.normal			+= da_dN * da_wn;
	float4 detail		= s_detail.SampleBias( smp_base, I.tcdbump, da_detail_bias.x);
	// [DA_PORT] Цвет подавляется СВОЕЙ мерой, не привязанной к блеску.
	//
	// Выше мера домножена на saturate(S.gloss), и для нормали с блеском это верно: там рассыпается
	// узкий зеркальный блик, а дерево и кирпич сходятся сами - замерено 26.07. Но цвет ломается по
	// другой причине: цветная детальная текстура идёт в альбедо с множителем два, и как только она
	// становится мельче пикселя, оттенок меняется от кадра к кадру вслед за дрожанием. На матовой
	// поверхности это видно ничуть не меньше, чем на металле.
	//
	// А через блеск оно было заглушено: у дороги S.gloss около нуля, вся мера обнулялась, и ручка
	// r__detail_albedo_fix на ней не делала НИЧЕГО. Симптом - «земля впереди моргает»: детальная
	// текстура колеи то накладывается, то нет.
	//
	// Меряем так же, как это уже сделано в ветке БЕЗ детального бампа - по самому цвету детали.
	// Разница между двумя ветками была недоделкой, а не замыслом.
	float  da_inst_a	= da_detail_instability( detail.rgb );
	float  da_wa1		= 1.0h - saturate(da_inst_a * da_detail_fix.w);
	if (da_detail_fix.z > 7.5h && da_detail_fix.z < 8.5h)	da_wa1 = 0.0h;	// 8: без детали в цвете
	// Нейтраль - ЕДИНИЦА: средний тексел детали около .5 и множитель два, так что полное подавление
	// значит «как если бы у поверхности не было детальной карты», а не «чёрный» и не «матовый».
	S.base.rgb			= S.base.rgb * lerp( 1.0h, detail.rgb * 2, da_wa1 );

	// [DA_PORT] r__detail_debug 1 and 2 paint the weight itself rather than the surface, so its magnitude
	// can be read off the screen instead of guessed; 3 paints every pixel this branch touches at all,
	// which is the one reading that separates "the number is too small" from "this code never runs here".
	// Written last, so the detail tint above cannot colour it.
	if (da_detail_fix.z > 2.5h && da_detail_fix.z < 3.5h)
		S.base.rgb		= float3( 1.0h, 0.0h, 0.0h );
	else if (da_detail_fix.z > 1.5h && da_detail_fix.z < 2.5h)
		S.base.rgb		= saturate( da_inst * da_detail_fix.x );
	// Bounded on BOTH sides. It was not, and modes 4/5/8/9 - the ones that answer which half is guilty -
	// fell into this branch and painted the surface with a weight of zero, i.e. black. Their whole point
	// is to change behaviour and paint NOTHING, so the artefact can be judged on the real surface.
	else if (da_detail_fix.z > 0.5h && da_detail_fix.z < 1.5h)
		S.base.rgb		= saturate( da_inst * da_detail_fix.y );

//	S.base.rgb			= float3(1,0,0);
#else        //	USE_TDETAIL_BUMP
	float4 detail		= s_detail.SampleBias( smp_base, I.tcdbump, da_detail_bias.x);
	// [DA_PORT] This is the branch metal props actually take (verified in game with the debug modes
	// below). Note what it does: a COLOURED detail texture goes straight into the albedo with a factor of
	// two. Once that texture is finer than a pixel the tint differs from frame to frame as the jitter
	// moves the sample, and the upscaler settles the difference into standing coloured mottling - which
	// is why the artefact reads as iridescence rather than the whitish sparkle a specular problem gives.
	float  da_inst		= da_detail_instability( detail.rgb );
	float  da_wa		= 1.0h - saturate(da_inst * da_detail_fix.w);	// weight on the albedo tint
	float  da_wg2		= 1.0h - saturate(da_inst * da_detail_fix.x);	// weight on the gloss
	// Hard overrides, to settle which half is guilty without first calibrating a scale.
	if (da_detail_fix.z > 7.5h && da_detail_fix.z < 8.5h)	da_wa  = 0.0h;	// 8: no detail in the albedo
	if (da_detail_fix.z > 8.5h)								da_wg2 = 0.0h;	// 9: no detail in the gloss
	// Neutral is ONE for both: an average detail texel sits near .5 and both are scaled by 2, so full
	// damping means "as if this surface had no detail map", not "black" and not "matte".
	S.base.rgb			= S.base.rgb * lerp( 1.0h, detail.rgb * 2, da_wa );
	S.gloss				= S.gloss * lerp( 1.0h, detail.w * GLOSS_MUL, da_wg2 );
	// [DA_PORT] r__detail_debug 7: detail WITHOUT a bump map. Blue here and red under mode 3 are mutually
	// exclusive, so the pair says which of the two detail paths a surface actually takes.
	if (da_detail_fix.z > 6.5h && da_detail_fix.z < 7.5h)
		S.base.rgb		= float3( 0.0h, 0.0h, 1.0h );
#endif        //	USE_TDETAIL_BUMP
#endif

	return S;
}

surface_bumped sload_i( p_bumped I, float2 pixeloffset )
{
	surface_bumped	S;
   
   // apply offset
#ifdef	MSAA_ALPHATEST_DX10_1
   I.tcdh.xy += pixeloffset.x * ddx(I.tcdh.xy) + pixeloffset.y * ddy(I.tcdh.xy);
#endif

	UpdateTC(I);	//	All kinds of parallax are applied here.

	float4 	Nu	= s_bump.Sample( smp_base, I.tcdh );		// IN:	normal.gloss
	float4 	NuE	= s_bumpX.Sample( smp_base, I.tcdh);	// IN:	normal_error.height

	S.base		= tbase(I.tcdh);				//	IN:  rgb.a
	S.normal	= Nu.wzyx + (NuE.xyz - 1.0h);	//	(Nu.wzyx - .5h) + (E-.5)
	S.gloss		= Nu.x*Nu.x;					//	S.gloss = Nu.x*Nu.x;
	S.height	= NuE.z;
	//S.height	= 0;

#ifdef        USE_TDETAIL
#ifdef        USE_TDETAIL_BUMP
#ifdef MSAA_ALPHATEST_DX10_1
#if ( (!defined(ALLOW_STEEPPARALLAX) ) && defined(USE_STEEPPARALLAX) )
   I.tcdbump.xy += pixeloffset.x * ddx(I.tcdbump.xy) + pixeloffset.y * ddy(I.tcdbump.xy);
#endif
#endif

	float4 NDetail		= s_detailBump.SampleBias( smp_base, I.tcdbump, da_detail_bias.x);
	float4 NDetailX		= s_detailBumpX.SampleBias( smp_base, I.tcdbump, da_detail_bias.x);
	// [DA_PORT] Damped where the detail bump has gone finer than a pixel - see the top of this file.
	float3 da_dN		= NDetail.wzy + NDetailX.xyz - 1.0h; //	(Nu.wzyx - .5h) + (E-.5)
	// [DA_PORT] Weighted by the material's OWN gloss, and that is the whole point of the measure.
	// Detail textures are high-frequency everywhere by design, so frequency alone marks the entire
	// world and damping it is just a global loss of detail - measured 26.07, every surface went to
	// full weight at once. What separates the surfaces that break from the ones that do not is the
	// specular lobe: on metal it is narrow enough that a sub-pixel shift in the sample lands on a
	// different part of it and the pixel changes brightness every frame; on wood or brick the same
	// wobble is spread across a wide lobe and disappears. Verified by capture: with the camera held
	// still the barrel changed 5.4/255 per frame - as much as trees moving in the wind - while the
	// fence beside it, same detail texture, same distance, sat at 0.17 and had fully converged.
	float  da_inst		= da_detail_instability( da_dN ) * saturate(S.gloss);
	float  da_wn		= 1.0h - saturate(da_inst * da_detail_fix.y);	// weight on the normal
	float  da_wg		= 1.0h - saturate(da_inst * da_detail_fix.x);	// weight on the gloss
	// Hard overrides, so which half is guilty can be settled without first calibrating a scale.
	if (da_detail_fix.z > 3.5h && da_detail_fix.z < 4.5h)	da_wn = 0.0h;	// 4: no detail in the normal
	if (da_detail_fix.z > 4.5h && da_detail_fix.z < 5.5h)	da_wg = 0.0h;	// 5: no detail in the gloss
	// Neutral for the gloss is ONE, not zero: NDetail.x sits around .5 and GLOSS_MUL is 2, so the pair
	// multiplies to unity on average. Scaling the product by the weight would have driven gloss to zero
	// and flattened the metal instead of merely steadying it.
	S.gloss				= S.gloss * lerp( 1.0h, NDetail.x * GLOSS_MUL, da_wg );
	S.normal			+= da_dN * da_wn;
	float4 detail		= s_detail.SampleBias( smp_base, I.tcdbump, da_detail_bias.x);
	// [DA_PORT] Цвет подавляется СВОЕЙ мерой, не привязанной к блеску.
	//
	// Выше мера домножена на saturate(S.gloss), и для нормали с блеском это верно: там рассыпается
	// узкий зеркальный блик, а дерево и кирпич сходятся сами - замерено 26.07. Но цвет ломается по
	// другой причине: цветная детальная текстура идёт в альбедо с множителем два, и как только она
	// становится мельче пикселя, оттенок меняется от кадра к кадру вслед за дрожанием. На матовой
	// поверхности это видно ничуть не меньше, чем на металле.
	//
	// А через блеск оно было заглушено: у дороги S.gloss около нуля, вся мера обнулялась, и ручка
	// r__detail_albedo_fix на ней не делала НИЧЕГО. Симптом - «земля впереди моргает»: детальная
	// текстура колеи то накладывается, то нет.
	//
	// Меряем так же, как это уже сделано в ветке БЕЗ детального бампа - по самому цвету детали.
	// Разница между двумя ветками была недоделкой, а не замыслом.
	float  da_inst_a	= da_detail_instability( detail.rgb );
	float  da_wa1		= 1.0h - saturate(da_inst_a * da_detail_fix.w);
	if (da_detail_fix.z > 7.5h && da_detail_fix.z < 8.5h)	da_wa1 = 0.0h;	// 8: без детали в цвете
	// Нейтраль - ЕДИНИЦА: средний тексел детали около .5 и множитель два, так что полное подавление
	// значит «как если бы у поверхности не было детальной карты», а не «чёрный» и не «матовый».
	S.base.rgb			= S.base.rgb * lerp( 1.0h, detail.rgb * 2, da_wa1 );

	// [DA_PORT] r__detail_debug 1 and 2 paint the weight itself rather than the surface, so its magnitude
	// can be read off the screen instead of guessed; 3 paints every pixel this branch touches at all,
	// which is the one reading that separates "the number is too small" from "this code never runs here".
	// Written last, so the detail tint above cannot colour it.
	if (da_detail_fix.z > 2.5h && da_detail_fix.z < 3.5h)
		S.base.rgb		= float3( 1.0h, 0.0h, 0.0h );
	else if (da_detail_fix.z > 1.5h && da_detail_fix.z < 2.5h)
		S.base.rgb		= saturate( da_inst * da_detail_fix.x );
	// Bounded on BOTH sides. It was not, and modes 4/5/8/9 - the ones that answer which half is guilty -
	// fell into this branch and painted the surface with a weight of zero, i.e. black. Their whole point
	// is to change behaviour and paint NOTHING, so the artefact can be judged on the real surface.
	else if (da_detail_fix.z > 0.5h && da_detail_fix.z < 1.5h)
		S.base.rgb		= saturate( da_inst * da_detail_fix.y );

//	S.base.rgb			= float3(1,0,0);
#else        //	USE_TDETAIL_BUMP
#ifdef MSAA_ALPHATEST_DX10_1
   I.tcdbump.xy += pixeloffset.x * ddx(I.tcdbump.xy) + pixeloffset.y * ddy(I.tcdbump.xy);
#endif
	float4 detail		= s_detail.SampleBias( smp_base, I.tcdbump, da_detail_bias.x);
	S.base.rgb			= S.base.rgb * detail.rgb * 2;
	S.gloss				= S.gloss * detail.w * GLOSS_MUL;
#endif        //	USE_TDETAIL_BUMP
#endif

	return S;
}

surface_bumped sload ( p_bumped I)
{
      surface_bumped      S   = sload_i	(I);
		S.normal.z			*=	0.3;		//. make bump twice as contrast (fake, remove me if possible)
	// [DA_PORT] r__detail_debug 6: paint EVERY bumped surface, whether or not it has a detail map. This
	// is the control for mode 3 - if a surface turns green here but never red there, this file is live
	// on it and the detail-bump branch simply is not the path it takes.
	if (da_detail_fix.z > 5.5h && da_detail_fix.z < 6.5h)
		S.base.rgb		= float3( 0.0h, 1.0h, 0.0h );

#ifdef	GBUFFER_OPTIMIZATION
	   S.height = 0;
#endif	//	GBUFFER_OPTIMIZATION
      return              S;
}

surface_bumped sload ( p_bumped I, float2 pixeloffset )
{
      surface_bumped      S   = sload_i	(I, pixeloffset );
		S.normal.z			*=	0.3;		//. make bump twice as contrast (fake, remove me if possible)
#ifdef	GBUFFER_OPTIMIZATION
	   S.height = 0;
#endif	//	GBUFFER_OPTIMIZATION
      return              S;
}

#endif
