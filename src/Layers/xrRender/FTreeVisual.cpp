#include "stdafx.h"

#include "xrEngine/IGame_Persistent.h"
#include "xrEngine/IGame_Level.h"
#include "xrEngine/Environment.h"
#include "xrCore/FMesh.hpp"
#include "FTreeVisual.h"
#include "Common/OGF_GContainer_Vertices.hpp"

// [DA_PORT] Vegetation sway scale, 0 = frozen. Defined in the engine; declared outside the namespace
// on purpose - written inside it, the extern would look for the symbol in that namespace instead.
extern ENGINE_API float ps_r__wind_scale;
extern ENGINE_API int ps_r__wind_shadow;

namespace xray::render::RENDER_NAMESPACE
{
shared_str m_xform;
shared_str m_xform_v;
shared_str c_consts;
shared_str c_wave;
shared_str c_wind;
shared_str c_c_bias;
shared_str c_c_scale;
shared_str c_c_sun;

FTreeVisual::FTreeVisual(void) {}
FTreeVisual::~FTreeVisual(void) {}
void FTreeVisual::Release() { dxRender_Visual::Release(); }
void FTreeVisual::Load(const char* N, IReader* data, u32 dwFlags)
{
    dxRender_Visual::Load(N, data, dwFlags);

    const VertexElement* vFormat = nullptr;

    // read vertices
    R_ASSERT(data->find_chunk(OGF_GCONTAINER));
    {
        // verts
        u32 ID = data->r_u32();
        vBase = data->r_u32();
        vCount = data->r_u32();
        vFormat = RImplementation.getVB_Format(ID);

        VERIFY(nullptr == p_rm_Vertices);
        p_rm_Vertices = RImplementation.getVB(ID);
        p_rm_Vertices->AddRef();

        // indices
        dwPrimitives = 0;
        ID = data->r_u32();
        iBase = data->r_u32();
        iCount = data->r_u32();
        dwPrimitives = iCount / 3;

        VERIFY(nullptr == p_rm_Indices);
        p_rm_Indices = RImplementation.getIB(ID);
        p_rm_Indices->AddRef();
    }

    // load tree-def
    R_ASSERT(data->find_chunk(OGF_TREEDEF2));
    {
        data->r(&xform, sizeof(xform));
        data->r(&c_scale, sizeof(c_scale));
        c_scale.rgb.mul(.5f);
        c_scale.hemi *= .5f;
        c_scale.sun *= .5f;
        data->r(&c_bias, sizeof(c_bias));
        c_bias.rgb.mul(.5f);
        c_bias.hemi *= .5f;
        c_bias.sun *= .5f;
        // Msg				("hemi[%f / %f], sun[%f / %f]",c_scale.hemi,c_bias.hemi,c_scale.sun,c_bias.sun);
    }

    /*if (RImplementation.o.ffp && dcl_equal(vFormat, mu_model_decl_unpacked))
    {
        const size_t vertices_size = vCount * sizeof(mu_model_vert_unpacked);

        const auto new_buffer = xr_new<VertexStagingBuffer>();
        new_buffer->Create(vertices_size);

        auto vert_new = static_cast<mu_model_vert_unpacked*>(new_buffer->Map());
        const auto vert_orig = static_cast<mu_model_vert_unpacked*>(p_rm_Vertices->Map(vBase, vertices_size, true)); // read-back
        CopyMemory(vert_new, vert_orig, vertices_size);

        for (size_t i = 0; i < vCount; ++i)
        {
            //vert_new->P.mul(xform.j);
            ++vert_new;
        }

        new_buffer->Unmap(true);
        p_rm_Vertices->Unmap(false);
        _RELEASE(p_rm_Vertices);
        p_rm_Vertices = new_buffer;
        vBase = 0;
    }*/

    // Geom
    rm_geom.create(vFormat, *p_rm_Vertices, *p_rm_Indices);

    // Get constants
    m_xform = "m_xform";
    m_xform_v = "m_xform_v";
    c_consts = "consts";
    c_wave = "wave";
    c_wind = "wind";
    c_c_bias = "c_bias";
    c_c_scale = "c_scale";
    c_c_sun = "c_sun";
}

struct FTreeVisual_setup
{
    u32 dwFrame;
    float scale;
    Fvector4 wave;
    Fvector4 wind;

    // [DA_PORT] The same two as they were on the previous frame, for motion vectors. Without them the
    // sway is invisible to the upscaler: the previous position is rebuilt from the vertex as it stands
    // NOW, already displaced, so foliage reports "I did not move" while it visibly does. The history is
    // then fetched by the camera offset alone, lands on a different leaf, and gets accepted or rejected
    // depending on how well it happens to match - which reads as trees and bushes blinking.
    Fvector4 wave_old;
    Fvector4 wind_old;
    bool seeded;

    // [DA_PORT] Sway phase, accumulated rather than derived from the clock. See calculate().
    float phase_pos, phase_rot, time_old;

    FTreeVisual_setup(): dwFrame(0), scale(0), seeded(false), phase_pos(0), phase_rot(0), time_old(0) {}


    void calculate()
    {
        // Both are functions of global time and are recomputed once per frame, so remembering the
        // previous frame's pair costs one copy.
        if (seeded)
        {
            wave_old.set(wave);
            wind_old.set(wind);
        }
        dwFrame = Device.dwFrame;
        CEnvDescriptor& desc = g_pGamePersistent->Environment().CurrentEnv;

        // [DA_PORT] The phase is ACCUMULATED, not computed as time * speed.
        //
        // Both m_fTreeSpeed and m_fTreeRotation come from the weather descriptor and are interpolated
        // between keyframes, so they drift continuously through the day. Multiplying them by
        // fTimeGlobal - which is thousands of seconds by then - turns a drift of a thousandth into a
        // phase jump of many whole turns: the tree is instantly somewhere else in its swing, which
        // looks exactly like it snapping back to its rest position. Visible at native resolution too,
        // so it was never an upscaler problem; a temporal filter merely makes it more obvious.
        //
        // CDetailManager already does it this way for grass (m_time_pos += dt * speed), which is why
        // grass never showed the artefact. Same treatment here.
        float dt = Device.fTimeGlobal - time_old;
        if (dt < 0.f || dt > 1.f) // first frame, or a pause / level load
            dt = 0.03f;
        time_old = Device.fTimeGlobal;

        phase_rot += PI_MUL_2 * dt / desc.m_fTreeRotation;
        phase_pos += dt * desc.m_fTreeSpeed;

        // [DA_PORT] Wrapped to one turn. The phase is fed to periodic functions (calc_cyclic in the
        // shader, sin/cos here), so a full turn is worth nothing to them - but it is worth a great deal
        // to a float. Left to grow, the phase reaches thousands within minutes of play, where float32
        // resolves about a thousandth - the same size as one frame's increment at 300 FPS. The phase
        // then advances in visible steps instead of smoothly, which is the jumping that appears "after
        // a while" and never at the start. Wrapping keeps every value small and exact.
        // Safe for the previous-frame copies too: they feed the same periodic functions.
        phase_rot = fmodf(phase_rot, PI_MUL_2);
        phase_pos = fmodf(phase_pos, PI_MUL_2);

        // Calc wind-vector3, scale
        float tm_rot = phase_rot;

        wind.set(_sin(tm_rot), 0, _cos(tm_rot), 0);
        wind.normalize();
        // [DA_PORT] r__wind_scale: 0 freezes the trees. See xr_ioc_cmd.cpp for why it exists.
        wind.mul(desc.m_fTreeAmplitude * ps_r__wind_scale); // dir1*amplitude

        scale = 1.f / float(FTreeVisual_quant);

        // setup constants
        wave.set(desc.m_fTreeWave.x, desc.m_fTreeWave.y, desc.m_fTreeWave.z, phase_pos); // wave
        wave.div(PI_MUL_2);

        if (!seeded)
        {
            // First frame: no previous sway to speak of. Seeding with the current one reports no motion,
            // which beats reporting the whole displacement as if it happened in a single frame.
            wave_old.set(wave);
            wind_old.set(wind);
            seeded = true;
        }
    }
};

// [DA_PORT] Общие для кадра параметры покачивания: ветер, волна и та же пара за прошлый кадр.
//
// Вынесено из Render, потому что спрашивающих стало двое — обычная отрисовка и пакетная
// (инстансинг). Оба обязаны получить ОДНИ И ТЕ ЖЕ числа: иначе дерево, попавшее в пачку, качается
// не так, как соседнее, не попавшее, и это видно.
//
// ⚠️ Потоко-локальная копия здесь не годится, хотя гонку убирает тоже. Фаза НАКАПЛИВАЕТСЯ, и у
// каждого потока счётчик пошёл бы своим ходом: поток, пропустивший кадр (каскад целиком отсечён),
// отстаёт навсегда, и листва в теневой карте расходится с листвой в сцене.
FTreeVisual_setup& da_tree_setup_for_frame()
{
    static FTreeVisual_setup tvs;
    // [DA_PORT] Exactly once per frame, and provably so.
    //
    // The plain "if (dwFrame != current) calculate()" this replaces was safe only while calculate() was
    // a pure function of the clock: several command lists render trees in parallel (scene plus shadow
    // cascades), two of them can pass that test in the same frame, and recomputing the same numbers
    // twice cost nothing. Once the sway phase became ACCUMULATED and the previous frame's wind started
    // being remembered here, a second entry stopped being harmless: it advances the phase twice in one
    // frame, and it overwrites wave_old/wind_old with the values just computed - so the previous frame's
    // wind equals the current one and every tree reports zero motion for that frame.
    //
    // Which is why the artefact was intermittent (the race is not won every frame), grew more frequent
    // the more trees were on screen (more draws, more chances), and never appeared in the original: the
    // race was always there, it just had nothing to corrupt.
    // [DA_PORT] ⚠️ Мало вычислить один раз — надо ещё дождаться, пока вычислят.
    //
    // Замена ниже гарантирует, что calculate() выполнится ровно однажды за кадр. Она НЕ гарантирует,
    // что остальные потоки увидят результат готовым: победитель ещё пишет tvs.wind / wind_old /
    // scale, а соседние каскады уже читают эти поля несколькими строками ниже. Прочитанный на
    // середине ветер сдвигает листву в теневой карте, и тень куста прыгает на целый кадр.
    //
    // Отсюда и повадки: редко (окно перекрытия узкое), широко (кривой ветер двигает ВСЮ
    // растительность разом) и с мгновенным возвратом на следующем кадре. Замер da_light_watch по
    // строке во всю ширину показал ровно это: десять широких событий на три с половиной тысячи
    // кадров при 84% узких, в один-два пикселя.
    //
    // Поэтому к «посчитано» добавлен флаг «результат виден», а опоздавшие ждут его. Ожидание почти
    // всегда нулевое: calculate() — это несколько матричных операций.
    static std::atomic<u32> s_frame{ 0 };
    static std::atomic<u32> s_ready{ 0 };
    u32 seen = s_frame.load(std::memory_order_relaxed);
    if (seen != Device.dwFrame && s_frame.compare_exchange_strong(seen, Device.dwFrame))
    {
        tvs.calculate();
        s_ready.store(Device.dwFrame, std::memory_order_release);
    }
    else
    {
        while (s_ready.load(std::memory_order_acquire) != Device.dwFrame)
            std::this_thread::yield();
    }

    return tvs;
}

// [DA_PORT] Ветер для текущего прохода. В теневом его гасим — см. разбор ниже, в Render.
static void da_tree_wind_for_phase(
    CBackend& cmd_list, const FTreeVisual_setup& tvs, Fvector4& wind, Fvector4& wind_old)
{
    wind = tvs.wind;
    wind_old = tvs.wind_old;

    if (ps_r__wind_shadow == 0 &&
        RImplementation.get_context(cmd_list.context_id).o.phase == CRender::PHASE_SMAP)
    {
        wind.set(0.f, 0.f, 0.f, 0.f);
        wind_old.set(0.f, 0.f, 0.f, 0.f);
    }
}

void FTreeVisual::Render(CBackend& cmd_list, float /*LOD*/, bool use_fast_geo)
{
    FTreeVisual_setup& tvs = da_tree_setup_for_frame();
// setup constants
#if RENDER != R_R1
    Fmatrix xform_v;
    xform_v.mul_43(cmd_list.get_xform_view(), xform);
    cmd_list.tree.set_m_xform_v(xform_v); // matrix
#endif
    float s = ps_r__Tree_SBC;
    cmd_list.tree.set_m_xform(xform); // matrix
    cmd_list.tree.set_consts(tvs.scale, tvs.scale, 0, 0); // consts/scale
    // [DA_PORT] Foliage stands still in the SHADOW pass, while swaying normally on screen.
    //
    // Measured in game: metal breaks into iridescent mottling under the upscaler, and freezing the
    // vegetation with r__wind_scale 0 leaves the same metal clean with nothing else changed. Marking
    // the metal reactive did NOT help, which rules out the reconstruction - the mottling is already in
    // the frame being rendered. That leaves only one route from the wind to the surface: the shadow the
    // leaves cast on it. A shadow map is a hard, aliased edge, so a leaf moving by a fraction of a
    // texel flips whole pixels of the metal between lit and shadowed every frame, and a narrow specular
    // lobe turns that into the colour noise seen on screen.
    //
    // The cost is that a tree's shadow no longer sways with the tree. Dappled foliage shade is diffuse
    // and moves subtly, so this reads as far less wrong than the metal boiling - and it is a common
    // trade in engines with sharp shadow maps. Costs nothing per frame either way.
    Fvector4 wind, wind_old;
    da_tree_wind_for_phase(cmd_list, tvs, wind, wind_old);

    cmd_list.tree.set_wave(tvs.wave); // wave
    cmd_list.tree.set_wind(wind); // wind
    cmd_list.tree.set_wave_old(tvs.wave_old); // [DA_PORT] motion vectors
    cmd_list.tree.set_wind_old(wind_old); // [DA_PORT] motion vectors
#if RENDER != R_R1
    s *= 1.3333f;
    cmd_list.tree.set_c_scale(s * c_scale.rgb.x, s * c_scale.rgb.y, s * c_scale.rgb.z, s * c_scale.hemi); // scale
    cmd_list.tree.set_c_bias(s * c_bias.rgb.x, s * c_bias.rgb.y, s * c_bias.rgb.z, s * c_bias.hemi); // bias
#else
    const auto& desc = g_pGamePersistent->Environment().CurrentEnv;
    cmd_list.tree.set_c_scale(s * c_scale.rgb.x, s * c_scale.rgb.y, s * c_scale.rgb.z, s * c_scale.hemi); // scale
    cmd_list.tree.set_c_bias(s * c_bias.rgb.x + desc.ambient.x, s * c_bias.rgb.y + desc.ambient.y,
        s * c_bias.rgb.z + desc.ambient.z, s * c_bias.hemi); // bias
#endif
    cmd_list.tree.set_c_sun(s * c_scale.sun, s * c_bias.sun, 0, 0); // sun
}

#ifdef USE_DX11
// [DA_PORT] --- Пакетная отрисовка деревьев (инстансинг) ---------------------------------------
//
// Дерево рисуется одним вызовом на штуку, а их на уровне тысячи: на Юпитере это самая длинная
// череда однотипных вызовов за кадр. Инстансинг позволяет нарисовать до 64 одинаковых деревьев
// одним вызовом, разложив их матрицы и освещение в константный буфер.
//
// Здесь только «что рисовать» и «чем оно отличается от соседа». Сама сборка пачек — в
// r__dsgraph_render.cpp, там же и решение, что пакетировать, а что оставить обычному пути.
//
// База ничего не умеет: FTreeVisual сам по себе не рисуется, рисуются наследники ST и PM.
bool FTreeVisual::GetInstancedDraw(float /*LOD*/, FTreeVisualInstancedDraw& /*draw*/) { return false; }

// [DA_PORT] Общее для всей пачки: то же, что Render ставит для одиночного дерева, кроме матриц и
// освещения — они у каждого экземпляра свои и уезжают в FillInstanceData.
//
// ⚠️ Ветер обязан считаться той же функцией, что и в Render: если пакетные деревья возьмут
// нескошенный ветер в теневом проходе, их тени поедут относительно тех, что рисуются обычным путём.
void FTreeVisual::SetupInstancedGlobals(CBackend& cmd_list)
{
    FTreeVisual_setup& tvs = da_tree_setup_for_frame();

    Fvector4 wind, wind_old;
    da_tree_wind_for_phase(cmd_list, tvs, wind, wind_old);

    cmd_list.tree.set_consts(tvs.scale, tvs.scale, 0, 0);
    cmd_list.tree.set_wave(tvs.wave);
    cmd_list.tree.set_wind(wind);
    cmd_list.tree.set_wave_old(tvs.wave_old); // [DA_PORT] векторы движения
    cmd_list.tree.set_wind_old(wind_old); // [DA_PORT] векторы движения
}

// [DA_PORT] Девять векторов на экземпляр: две матрицы 3x4 (мировая и видовая) плюс масштаб, смещение
// и солнце. Раскладка ОБЯЗАНА совпадать с tree_instance.h, иначе дерево уедет молча — шейдер просто
// прочитает не те числа. Порядок компонент такой же, каким матрицы уходят в константы обычным путём.
void FTreeVisual::FillInstanceData(CBackend& cmd_list, FTreeVisualInstanceData& data) const
{
    // [DA_PORT] xform не меняется никогда после Load — считаем один раз за жизнь дерева, не за кадр.
    if (!m_da_cached_world_valid)
    {
        m_da_cached_world[0].set(xform._11, xform._21, xform._31, xform._41);
        m_da_cached_world[1].set(xform._12, xform._22, xform._32, xform._42);
        m_da_cached_world[2].set(xform._13, xform._23, xform._33, xform._43);
        m_da_cached_world_valid = true;
    }
    data.vectors[0] = m_da_cached_world[0];
    data.vectors[1] = m_da_cached_world[1];
    data.vectors[2] = m_da_cached_world[2];

    // Единственное, что реально меняется каждый кадр, — матрица вида (камера движется).
    Fmatrix xform_v;
    xform_v.mul_43(cmd_list.get_xform_view(), xform);
    data.vectors[3].set(xform_v._11, xform_v._21, xform_v._31, xform_v._41);
    data.vectors[4].set(xform_v._12, xform_v._22, xform_v._32, xform_v._42);
    data.vectors[5].set(xform_v._13, xform_v._23, xform_v._33, xform_v._43);

    // [DA_PORT] Зависит только от ползунка ps_r__Tree_SBC — пересчёт только если он сдвинулся.
    const float s = ps_r__Tree_SBC * 1.3333f;
    if (m_da_cached_sbc != s)
    {
        m_da_cached_light[0].set(s * c_scale.rgb.x, s * c_scale.rgb.y, s * c_scale.rgb.z, s * c_scale.hemi);
        m_da_cached_light[1].set(s * c_bias.rgb.x, s * c_bias.rgb.y, s * c_bias.rgb.z, s * c_bias.hemi);
        m_da_cached_light[2].set(s * c_scale.sun, s * c_bias.sun, 0, 0);
        m_da_cached_sbc = s;
    }
    data.vectors[6] = m_da_cached_light[0];
    data.vectors[7] = m_da_cached_light[1];
    data.vectors[8] = m_da_cached_light[2];
}
#endif

#define PCOPY(a) a = pFrom->a
void FTreeVisual::Copy(dxRender_Visual* pSrc)
{
    dxRender_Visual::Copy(pSrc);

    FTreeVisual* pFrom = dynamic_cast<FTreeVisual*>(pSrc);

    PCOPY(rm_geom);
    PCOPY(p_rm_Vertices);
    if (p_rm_Vertices)
        p_rm_Vertices->AddRef();
    PCOPY(vBase);
    PCOPY(vCount);
    PCOPY(vStride);
    PCOPY(p_rm_Indices);
    if (p_rm_Indices)
        p_rm_Indices->AddRef();
    PCOPY(iBase);
    PCOPY(iCount);
    PCOPY(dwPrimitives);

    PCOPY(xform);
    PCOPY(c_scale);
    PCOPY(c_bias);
}

//-----------------------------------------------------------------------------------
// Stripified Tree
//-----------------------------------------------------------------------------------
FTreeVisual_ST::FTreeVisual_ST(void) {}
FTreeVisual_ST::~FTreeVisual_ST(void) {}
void FTreeVisual_ST::Release() { inherited::Release(); }
void FTreeVisual_ST::Load(const char* N, IReader* data, u32 dwFlags) { inherited::Load(N, data, dwFlags); }
void FTreeVisual_ST::Render(CBackend& cmd_list, float LOD, bool use_fast_geo)
{
    inherited::Render(cmd_list, LOD, use_fast_geo);
    cmd_list.set_Geometry(rm_geom);
    cmd_list.Render(D3DPT_TRIANGLELIST, vBase, 0, vCount, iBase, dwPrimitives);
    cmd_list.stat.r.s_flora.add(vCount);
}
#ifdef USE_DX11
bool FTreeVisual_ST::GetInstancedDraw(float /*LOD*/, FTreeVisualInstancedDraw& draw)
{
    if (!rm_geom)
        return false;

    draw.geometry = &*rm_geom;
    draw.base_vertex = vBase;
    draw.vertex_count = vCount;
    draw.start_index = iBase;
    draw.primitive_count = dwPrimitives;
    return true;
}
#endif
void FTreeVisual_ST::Copy(dxRender_Visual* pSrc) { inherited::Copy(pSrc); }
//-----------------------------------------------------------------------------------
// Progressive Tree
//-----------------------------------------------------------------------------------
FTreeVisual_PM::FTreeVisual_PM(void)
{
    pSWI = nullptr;
    last_lod = 0;
}
FTreeVisual_PM::~FTreeVisual_PM(void) {}
void FTreeVisual_PM::Release() { inherited::Release(); }
void FTreeVisual_PM::Load(const char* N, IReader* data, u32 dwFlags)
{
    inherited::Load(N, data, dwFlags);
    R_ASSERT(data->find_chunk(OGF_SWICONTAINER));
    {
        u32 ID = data->r_u32();
        pSWI = RImplementation.getSWI(ID);
    }
}
// [DA_PORT] Выбор уровня детализации вынесен из Render: пакетной отрисовке он нужен ОТДЕЛЬНО от
// самого рисования — чтобы сложить в одну пачку только деревья с одинаковым куском геометрии.
//
// ⚠️ Побочный эффект сохранён намеренно: last_lod запоминается. На него опирается ветка LOD < 0
// («рисуй тем же, чем в прошлый раз»), и если пакетный путь перестанет его обновлять, дерево при
// возврате на обычный путь возьмёт устаревший уровень.
u32 FTreeVisual_PM::SelectLOD(float LOD)
{
    int lod_id = last_lod;
    if (LOD >= 0.f)
    {
        lod_id = iFloor((1.f - LOD) * float(pSWI->count - 1) + 0.5f);
        last_lod = lod_id;
    }
    VERIFY(lod_id >= 0 && lod_id < int(pSWI->count));
    return u32(lod_id);
}

void FTreeVisual_PM::Render(CBackend& cmd_list, float LOD, bool use_fast_geo)
{
    inherited::Render(cmd_list, LOD, use_fast_geo);
    FSlideWindow& SW = pSWI->sw[SelectLOD(LOD)];
    cmd_list.set_Geometry(rm_geom);
    cmd_list.Render(D3DPT_TRIANGLELIST, vBase, 0, SW.num_verts, iBase + SW.offset, SW.num_tris);
    cmd_list.stat.r.s_flora.add(SW.num_verts);
}

#ifdef USE_DX11
bool FTreeVisual_PM::GetInstancedDraw(float LOD, FTreeVisualInstancedDraw& draw)
{
    if (!rm_geom || !pSWI || !pSWI->count)
        return false;

    const FSlideWindow& window = pSWI->sw[SelectLOD(LOD)];
    draw.geometry = &*rm_geom;
    draw.base_vertex = vBase;
    draw.vertex_count = window.num_verts;
    draw.start_index = iBase + window.offset;
    draw.primitive_count = window.num_tris;
    return true;
}
#endif
void FTreeVisual_PM::Copy(dxRender_Visual* pSrc)
{
    inherited::Copy(pSrc);
    FTreeVisual_PM* pFrom = dynamic_cast<FTreeVisual_PM*>(pSrc);
    PCOPY(pSWI);
}
} // namespace xray::render::RENDER_NAMESPACE
