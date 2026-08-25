#include "stdafx.h"

#include "Layers/xrRender/ResourceManager.h"
#include "Layers/xrRender/FBasicVisual.h"
#include "xrCore/FMesh.hpp"
#include "Common/LevelStructure.hpp"
#include "xrEngine/IGame_Persistent.h"
#include "xrCore/stream_reader.h"

#if defined(USE_DX11)
#include "Layers/xrRender/FHierrarhyVisual.h"
#include "Layers/xrRenderDX11/3DFluid/dx113DFluidVolume.h"
#endif

// [DA_PORT] Объявляется СНАРУЖИ пространства имён: внутри линкер искал бы символ в
// xray::render::RENDER_NAMESPACE и не нашёл бы. Грабля описана в docs/07_TROUBLESHOOTING.md.
extern ENGINE_API void da_upscaler_reset_history(pcstr why);

namespace xray::render::RENDER_NAMESPACE
{
// [DA_PORT] Счётчик ссылок на устройство D3D — без слоя отладки DirectX.
//
// Каждый объект D3D держит ссылку на устройство, поэтому счётчик — это косвенная перепись живых
// объектов. Замер показал 198 ссылок после одной загрузки уровня и 5806 после семи: около девятисот
// объектов теряется на каждый переход, и держат они память драйвера, а не наши кучи (оттого обход
// куч и показывал «живые стоят», пока закоммиченная память росла на 800 МБ за переход).
//
// Слой отладки (ID3D11Debug) перечислил бы их поимённо, но требует пакета Graphics Tools, которого
// может не быть. Здесь достаточно печатать счётчик ПОСЛЕ КАЖДОГО ШАГА выгрузки: шаг, после которого
// он не падает, и есть владелец. Чтение счётчика — это AddRef с немедленным Release.
static ULONG da_device_refs()
{
    if (!HW.pDevice)
        return 0;
    HW.pDevice->AddRef();
    return HW.pDevice->Release();
}



// [DA_PORT] Текстура проекции налобного луча. Имя взято из ветки TorchType 2 в xr_actor.script —
// автор писал её ровно под налобный фонарь.
#define DA_TORCH_SPOT_TEXTURE "internal" DELIMITER "torch1"

void CRender::level_Load(IReader* fs)
{
    ZoneScoped;

    // [DA_PORT] История временных фильтров относится к прошлому уровню и переносить её некуда:
    // прошлый кадр показывает другое место, а векторы движения этого не описывают.
    ::da_upscaler_reset_history("level load");

    // [DA_PORT] Отметки стадий загрузки уровня рендером.
    //
    // Поставлены потому, что падение на переходе не оставляло ни стека, ни следа: последней строкой
    // в логе была отметка сброса истории выше, то есть САМОЕ НАЧАЛО level_Load, а дальше тишина.
    // Штатные «phase time» печатаются по концу фазы, поэтому по ним видно лишь, что упало где-то
    // внутри — но не где. Отметки дешёвые (строка в лог на стадию) и остаются в сборке: следующий
    // такой отказ назовёт стадию сам, без ещё одного круга сборка-запуск-повтор.
    Msg("* [DA_PORT] level_Load: начало | ссылок на устройство: %u", (u32)da_device_refs());

    R_ASSERT(g_pGameLevel);
    R_ASSERT(!b_loaded);

    // Begin
    g_pGamePersistent->LoadBegin();
    Resources->DeferredLoad(TRUE);
    IReader* chunk;

    // Shaders
    Msg("* [DA_PORT] level_Load: шейдеры уровня | ссылок на устройство: %u", (u32)da_device_refs());
    g_pGamePersistent->LoadTitle("st_loading_shaders");
    {
        ZoneScopedN("Load shaders");
        chunk = fs->open_chunk(fsL_SHADERS);
        R_ASSERT2(chunk, "Level doesn't builded correctly.");
        u32 count = chunk->r_u32();
        Shaders.resize(count);
        for (u32 i = 0; i < count; i++) // skip first shader as "reserved" one
        {
            string512 n_sh, n_tlist;
            LPCSTR n = LPCSTR(chunk->pointer());
            chunk->skip_stringZ();
            if (0 == n[0])
                continue;
            xr_strcpy(n_sh, n);
            pstr delim = strchr(n_sh, '/');
            // [DA_PORT] The result was dereferenced unchecked: a level shader entry without a '/' wrote
            // through a null pointer. Skip it and say which one instead of taking the whole game down.
            if (!delim)
            {
                Msg("! [DA_PORT] level shader [%s] has no '/' separator - skipped", n_sh);
                continue;
            }
            *delim = 0;
            xr_strcpy(n_tlist, delim + 1);
            // [DA_PORT] Имя ПЕРЕД созданием, а не после. Создание шейдера уровня умеет уронить игру
            // насмерть — без стека и без единого сообщения, — и тогда единственная зацепка это
            // последняя напечатанная строка. Печатать после было бы бесполезно ровно в том случае,
            // ради которого всё и заведено.
            Msg("* [DA_PORT] level_Load: шейдер %u/%u [%s] / [%s]", i, count, n_sh, n_tlist);
            Shaders[i] = Resources->Create(n_sh, n_tlist);
        }
        chunk->close();
    }

    // Components
    Msg("* [DA_PORT] level_Load: следы и детали | ссылок на устройство: %u", (u32)da_device_refs());
    Wallmarks = xr_new<CWallmarksEngine>();
    Details = xr_new<CDetailManager>();

    if (!GEnv.isDedicatedServer)
    {
        // VB,IB,SWI
        Msg("* [DA_PORT] level_Load: геометрия | ссылок на устройство: %u", (u32)da_device_refs());
        g_pGamePersistent->LoadTitle("st_loading_geometry");
        {
            CStreamReader* geom = FS.rs_open("$level$", "level.geom");
            R_ASSERT2(geom, "level.geom");
            LoadBuffers(geom, false);
            LoadSWIs(geom);
            FS.r_close(geom);
        }

        //...and alternate/fast geometry
        if (CStreamReader* geom = FS.rs_open("$level$", "level.geomX"))
        {
            LoadBuffers(geom, true);
            FS.r_close(geom);
            m_fast_geom_loaded = true;
        }

        // Visuals
        Msg("* [DA_PORT] level_Load: визуалы | ссылок на устройство: %u", (u32)da_device_refs());
        g_pGamePersistent->LoadTitle("st_loading_spatial_db");
        chunk = fs->open_chunk(fsL_VISUALS);
        LoadVisuals(chunk);
        chunk->close();

        // Details
        Msg("* [DA_PORT] level_Load: детальные объекты | ссылок на устройство: %u", (u32)da_device_refs());
        g_pGamePersistent->LoadTitle("st_loading_details");
        Details->Load();
    }

    // Sectors
    Msg("* [DA_PORT] level_Load: секторы и порталы | ссылок на устройство: %u", (u32)da_device_refs());
    g_pGamePersistent->LoadTitle("st_loading_sectors_portals");
    LoadSectors(fs);

#if defined(USE_DX11)
    // 3D Fluid
    Load3DFluid();
#endif

    // HOM
    Msg("* [DA_PORT] level_Load: HOM | ссылок на устройство: %u", (u32)da_device_refs());
    HOM.Load();

    // Lights
    Msg("* [DA_PORT] level_Load: источники света | ссылок на устройство: %u", (u32)da_device_refs());
    g_pGamePersistent->LoadTitle("st_loading_lights");
    LoadLights(fs);

    // End
    g_pGamePersistent->LoadEnd();

    // signal loaded
    Msg("* [DA_PORT] level_Load: готово | ссылок на устройство: %u", (u32)da_device_refs());
    b_loaded = TRUE;

    // [DA_PORT] Прогрев прожектора налобного фонаря — разбор у m_da_torch_spot_warm в r2.h.
    //
    // Имя текстуры взято из ветки TorchType 2 в xr_actor.script, то есть ровно то, которым луч
    // пользуется на самом деле. Если текстуры нет, create просто отдаст пустышку — прогрев тогда
    // не состоится, но и вреда не будет.
    if (Target)
    {
        string256 warm_name;
        strconcat(sizeof(warm_name), warm_name, "r2" DELIMITER "accum_spot_", DA_TORCH_SPOT_TEXTURE);
        m_da_torch_spot_warm.create(Target->b_accum_spot, warm_name, DA_TORCH_SPOT_TEXTURE);
        Msg("* [DA_PORT] прожектор фонаря прогрет: %s", warm_name);
    }

#if RENDER == R_R4
    // [DA_PORT] Манифест прогрева сохраняем и ЗДЕСЬ, а не только при выходе с уровня.
    //
    // Одного крючка на выгрузке оказалось мало: сессия, которая закончилась прямо на локации —
    // вылетом, закрытием окна, снятием процесса стендом, — не выгружает уровень вовсе, и всё
    // собранное за неё пропадает. Проверено на живом заходе: игрок переключился на MSAA, отыграл,
    // манифест не вырос ни на запись.
    //
    // Здесь же момент удачный: набор шейдеров уровня к этой точке уже разрешён. Сохранение
    // сливается с прежним файлом, так что двойной вызов ничего не портит.
    da_shader_manifest_save();
#endif
}


// [DA_PORT] Освобождение объёмов объёмного тумана. Подробности — у m_fluid_volumes в r2.h.
void CRender::Unload3DFluid()
{
#if defined(USE_DX11)
    for (auto& it : m_fluid_volumes)
    {
        // Сперва отцепить от родителя. Корень сектора живёт с bDontDelete = TRUE и детей не трогает,
        // но полагаться на это значит завязываться на порядок разрушения; отцепив, мы делаем правку
        // верной независимо от того, кто кого удалит первым.
        if (it.parent && it.parent->getType() == MT_HIERRARHY)
        {
            auto& ch = ((FHierrarhyVisual*)it.parent)->children;
            ch.erase(std::remove(ch.begin(), ch.end(), it.volume), ch.end());
        }
        if (it.volume)
        {
            it.volume->Release();
            dxRender_Visual* v = it.volume;
            xr_delete(v);
        }
    }
#endif
    m_fluid_volumes.clear();
}

void CRender::level_Unload()
{
    // [DA_PORT] Прогрев держал ссылку на шейдер и текстуру — отпускаем вместе с уровнем.
    m_da_torch_spot_warm.destroy();

    ZoneScoped;

    if (!g_pGameLevel)
        return;

#if RENDER == R_R4
    // [DA_PORT] Манифест прогрева сохраняем при выходе с уровня — то есть сам собой, по ходу игры.
    //
    // Полное покрытие набирается только обходом карты, а требовать от игрока консольную команду
    // после каждой локации бессмысленно: забудется на второй. Сохранение сливается с уже лежащим
    // файлом, поэтому обход можно вести хоть в несколько заходов. Стоит это записи пары сотен
    // строк там, где и так идёт выгрузка уровня.
    da_shader_manifest_save();
#endif
    if (!b_loaded)
        return;

    // [DA_PORT] До всего остального: объёмы тумана висят детьми у корней секторов, и их надо
    // снять раньше, чем начнут удаляться сами визуалы.
    Unload3DFluid();
    Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "объёмы тумана", (u32)da_device_refs());

    // HOM
    HOM.Unload();
    Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "HOM", (u32)da_device_refs());

    //*** Details
    // [DA_PORT] Ноль здесь законен: xr_delete ниже возвращает указатель в ноль, а выгрузка
    // может прийти повторно (см. разбор движка следов в r2.cpp).
    if (Details)
        Details->Unload();
    Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "детали", (u32)da_device_refs());

    //*** Sectors
    // 1.
    xr_delete(rmPortals);
    Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "порталы", (u32)da_device_refs());
    last_sector_id = IRender_Sector::INVALID_SECTOR_ID;
    Device.vCameraPositionSaved.set(0, 0, 0);

    // 2.
    cleanup_contexts();
    Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "контексты", (u32)da_device_refs());

    //*** Lights
    // Glows.Unload			();
    Lights.Unload();
    Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "свет", (u32)da_device_refs());

    //*** Visuals
    for (dxRender_Visual* visual : Visuals)
    {
        visual->Release();
        xr_delete(visual);
    }
    Visuals.clear();
    Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "визуалы", (u32)da_device_refs());

    //*** SWI
    for (auto& swi : SWIs)
        xr_free(swi.sw);
    SWIs.clear();
    Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "SWI", (u32)da_device_refs());

    //*** VB/IB
    for (auto& indexBuffer : nVB)
    {
        indexBuffer.Release();
    }
    nVB.clear();

    for (auto& vertexBuffer : xVB)
    {
        vertexBuffer.Release();
    }
    xVB.clear();

    for (auto& indexBuffer : nIB)
    {
        indexBuffer.Release();
    }
    nIB.clear();

    for (auto& vertexBuffer : xIB)
    {
        vertexBuffer.Release();
    }
    xIB.clear();
    Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "буферы VB/IB", (u32)da_device_refs());

    nDC.clear();
    xDC.clear();

    m_fast_geom_loaded = false;

    //*** Components
    xr_delete(Details);
    xr_delete(Wallmarks);
    Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "детали и следы", (u32)da_device_refs());

    //*** Shaders
    Shaders.clear();
    Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "шейдеры", (u32)da_device_refs());
    Msg("* [DA_PORT] выгрузка завершена, ссылок на устройство: %u", (u32)da_device_refs());
    b_loaded = FALSE;
    if (ps_r__clear_models_on_unload)
    {
        Models->ClearPool(true);
        Visuals.clear();
        Resources->Dump(false);
        //static int unload_counter = 0;
        //Msg("The Level Unloaded.======================== %d", ++unload_counter);
    }
}

void CRender::LoadBuffers(CStreamReader* base_fs, bool alternative)
{
    ZoneScoped;

    R_ASSERT2(base_fs, "Could not load geometry. File not found.");
    Resources->Evict();
    // Vertex buffers
    {
        ZoneScopedN("Load VBs");
        xr_vector<VertexDeclarator>& decls = alternative ? xDC : nDC;
        xr_vector<VertexStagingBuffer>& vbuffers = alternative ? xVB : nVB;

        // Use DX9-style declarators
        CStreamReader* fs = base_fs->open_chunk(fsL_VB);
        R_ASSERT2(fs, "Could not load geometry. File 'level.geom?' corrupted.");

        const u32 count = fs->r_u32();
        decls.resize(count);
        vbuffers.resize(count);

        constexpr size_t buffer_size = (MAXD3DDECLLENGTH + 1) * sizeof(VertexElement);
        for (u32 i = 0; i < count; i++)
        {
            // decl
            VertexElement* dcl = (VertexElement*)xr_alloca(buffer_size);
            fs->r(dcl, buffer_size);
            fs->advance(-(int)buffer_size);

            const u32 dcl_len = GetDeclLength(dcl) + 1;
            decls[i].resize(dcl_len);
            fs->r(decls[i].begin(), dcl_len * sizeof(VertexElement));

            // count, size
            const u32 vCount = fs->r_u32();
            const u32 vSize = GetDeclVertexSize(dcl, 0);
#ifndef MASTER_GOLD
            Msg("* [Loading VB] %d verts, %d Kb", vCount, (vCount * vSize) / 1024);
#endif

            // Create and fill
            //  TODO: DX11: Check fragmentation.
            //  Check if buffer is less then 2048 kb
            // [DA_PORT] true = дать буферу сырое представление для выборки вершин из шейдера.
            // Просим только у геометрии УРОВНЯ: она статична и живёт до выгрузки, а флаги ставятся
            // на создании. Отказ не фатален -- путь pulling для такого буфера просто не включится.
            vbuffers[i].Create(vCount * vSize, false, true);
            u8* pData = static_cast<u8*>(vbuffers[i].Map());
            fs->r(pData, vCount * vSize);
            vbuffers[i].Unmap(true); // upload vertex data

            //			fs->advance			(vCount*vSize);
        }

        // [DA_PORT] Сколько среди буферов РАЗНЫХ форматов вершин.
        //
        // Решает архитектуру выборки вершин из шейдера (vertex pulling): там весь уровень читается
        // одним StructuredBuffer по SV_VertexID, и на каждый отдельный формат нужен свой путь и
        // свой шейдер. Три формата — работа обозримая, тридцать — другой разговор. Замер разовый,
        // на загрузке, стоит один проход по объявлениям.
        {
            xr_vector<const VertexElement*> unique_decls;
            for (u32 i = 0; i < count; ++i)
            {
                const VertexElement* d = decls[i].begin();
                bool seen = false;
                for (const VertexElement* u : unique_decls)
                    if (dcl_equal(u, d))
                    {
                        seen = true;
                        break;
                    }
                if (!seen)
                    unique_decls.push_back(d);
            }
            // [DA_PORT] Раскладка полей — построчно. Гадать по размеру нельзя: шейдер читает
            // Nh/T/B как float4, а лежат они упакованными в D3DCOLOR по 4 байта, и без точных
            // смещений выборка из структурного буфера прочитает не те байты МОЛЧА.
            static const auto type_name = [](u32 t) -> pcstr
            {
                switch (t)
                {
                case D3DDECLTYPE_FLOAT1: return "float1";
                case D3DDECLTYPE_FLOAT2: return "float2";
                case D3DDECLTYPE_FLOAT3: return "float3";
                case D3DDECLTYPE_FLOAT4: return "float4";
                case D3DDECLTYPE_D3DCOLOR: return "D3DCOLOR";
                case D3DDECLTYPE_UBYTE4: return "ubyte4";
                case D3DDECLTYPE_SHORT2: return "short2";
                case D3DDECLTYPE_SHORT4: return "short4";
                case D3DDECLTYPE_UBYTE4N: return "ubyte4n";
                case D3DDECLTYPE_SHORT2N: return "short2n";
                case D3DDECLTYPE_SHORT4N: return "short4n";
                case D3DDECLTYPE_FLOAT16_2: return "half2";
                case D3DDECLTYPE_FLOAT16_4: return "half4";
                default: return "?";
                }
            };
            static const auto usage_name = [](u32 u) -> pcstr
            {
                switch (u)
                {
                case D3DDECLUSAGE_POSITION: return "POSITION";
                case D3DDECLUSAGE_NORMAL: return "NORMAL";
                case D3DDECLUSAGE_TANGENT: return "TANGENT";
                case D3DDECLUSAGE_BINORMAL: return "BINORMAL";
                case D3DDECLUSAGE_TEXCOORD: return "TEXCOORD";
                case D3DDECLUSAGE_COLOR: return "COLOR";
                default: return "?";
                }
            };
            for (size_t u = 0; u < unique_decls.size(); ++u)
                for (const VertexElement* e = unique_decls[u]; e && e->Type != D3DDECLTYPE_UNUSED; ++e)
                    Msg("* [DA_PORT]   формат %u поле: смещение %2u  %-9s %s%u", u32(u), e->Offset,
                        type_name(e->Type), usage_name(e->Usage), e->UsageIndex);

            // [DA_PORT] Сколько буферов РЕАЛЬНО получили сырое представление. Проверка обязательна:
            // отказ здесь молчаливый по построению (см. CreateRawSRV), а «просили» и «дали» — разные
            // вещи. Путь выборки из шейдера включается только там, где представление есть.
            u32 with_srv = 0;
            for (u32 i = 0; i < count; ++i)
                if (vbuffers[i].GetSRV())
                    ++with_srv;

            Msg("* [DA_PORT] геометрия уровня: буферов вершин %u, разных форматов %u, с сырым "
                "представлением %u%s", count, u32(unique_decls.size()), with_srv,
                alternative ? " (урезанный поток)" : "");
            for (size_t u = 0; u < unique_decls.size(); ++u)
                Msg("* [DA_PORT]   формат %u: вершина %u байт, полей %u", u32(u),
                    GetDeclVertexSize(unique_decls[u], 0), GetDeclLength(unique_decls[u]));
        }
        fs->close();
    }

    // Index buffers
    {
        ZoneScopedN("Load IBs");
        xr_vector<IndexStagingBuffer>& ibuffers = alternative ? xIB : nIB;

        CStreamReader* fs = base_fs->open_chunk(fsL_IB);
        const u32 count = fs->r_u32();
        ibuffers.resize(count);
        for (u32 i = 0; i < count; i++)
        {
            const u32 iCount = fs->r_u32();
#ifndef MASTER_GOLD
            Msg("* [Loading IB] %d indices, %d Kb", iCount, (iCount * 2) / 1024);
#endif

            // Create and fill
            //  TODO: DX11: Check fragmentation.
            //  Check if buffer is less then 2048 kb
            // [DA_PORT] Размер округляется ВВЕРХ до кратности четырём ради сырого представления.
            //
            // Индексов в треугольниках кратно трём, и при нечётном числе треугольников iCount*2 не
            // делится на четыре — сырое представление такого буфера создать нельзя, и замер показал,
            // что так отваливались 10 буферов из 12. Лишние два байта никогда не читаются как
            // индексы: отрисовка идёт по настоящему числу, а не по размеру буфера. Хвост обнуляем,
            // чтобы в видеопамять не уезжал мусор из кучи.
            const u32 ib_bytes = iCount * 2;
            const u32 ib_bytes_padded = (ib_bytes + 3) & ~3u;
            ibuffers[i].Create(ib_bytes_padded, false, true, true);
            u8* pData = static_cast<u8*>(ibuffers[i].Map());
            fs->r(pData, ib_bytes);
            if (ib_bytes_padded > ib_bytes)
                ZeroMemory(pData + ib_bytes, ib_bytes_padded - ib_bytes);
            ibuffers[i].Unmap(true); // upload index data

            //			fs().advance		(iCount*2);
        }

        // [DA_PORT] Сколько индексных буферов получили сырое представление -- см. вершинные выше.
        // Отказ здесь ожидаем и нормален: сырое представление требует размер, кратный четырём, а у
        // буфера с нечётным числом индексов его нет. Такой буфер просто не пойдёт по пути pulling.
        {
            u32 with_srv = 0;
            for (u32 i = 0; i < count; ++i)
                if (ibuffers[i].GetSRV())
                    ++with_srv;
            Msg("* [DA_PORT] геометрия уровня: буферов индексов %u, с сырым представлением %u%s", count,
                with_srv, alternative ? " (урезанный поток)" : "");
        }
        fs->close();
    }
}

void CRender::LoadVisuals(IReader* fs)
{
    u32 index = 0;
    IReader* chunk = nullptr;

    ZoneScoped;

    while ((chunk = fs->open_chunk(index)) != 0)
    {
        ogf_header H;
        chunk->r_chunk_safe(OGF_HEADER, &H, sizeof(H));

        dxRender_Visual* visual = Models->Instance_Create(H.type);
        visual->Load(nullptr, chunk, 0);
        Visuals.push_back(visual);

        chunk->close();
        index++;
    }
}

void CRender::LoadLights(IReader* fs)
{
    ZoneScoped;
    // lights
    Lights.Load(fs);
    Lights.LoadHemi();
}

void CRender::LoadSectors(IReader* fs)
{
    ZoneScoped;

    // allocate memory for portals
    const u32 size = fs->find_chunk(fsL_PORTALS);
    R_ASSERT(0 == size % sizeof(CPortal::level_portal_data_t));

    const u32 portals_count = size / sizeof(CPortal::level_portal_data_t);
    xr_vector<CPortal::level_portal_data_t> portals_data{portals_count};

    // load sectors
    xr_vector<CSector::level_sector_data_t> sectors_data;

    float largest_sector_vol = 0.0f;
    IReader* S = fs->open_chunk(fsL_SECTORS);
    for (u32 i = 0;; i++)
    {
        IReader* P = S->open_chunk(i);
        if (!P)
            break;

        ZoneScopedN("Load sector");
        auto& sector_data = sectors_data.emplace_back();
        {
            u32 size = P->find_chunk(fsP_Portals);
            R_ASSERT(0 == (size & 1));
            u32 portals_in_sector = size / sizeof(u16);

            sector_data.portals_id.reserve(portals_in_sector);
            while (portals_in_sector)
            {
                const u16 ID = P->r_u16();
                sector_data.portals_id.emplace_back(ID);
                --portals_in_sector;
            }

            size = P->find_chunk(fsP_Root);
            R_ASSERT(size == 4);
            sector_data.root_id = P->r_u32();

            // Search for default sector - assume "default" or "outdoor" sector is the largest one
            // XXX: hack: need to know real outdoor sector
            auto* V = static_cast<dxRender_Visual*>(RImplementation.getVisual(sector_data.root_id));
            float vol = V->vis.box.getvolume();
            if (vol > largest_sector_vol)
            {
                largest_sector_vol = vol;
                largest_sector_id = static_cast<IRender_Sector::sector_id_t>(i);
            }
        }
        P->close();
    }
    S->close();

    // load portals
    if (portals_count)
    {
        static const bool use_cache = !strstr(Core.Params, "-no_cdb_cache");
        static const bool skip_crc32_check = strstr(Core.Params, "-skip_cdb_cache_crc32_check");

        ZoneScopedN("Load portals");

        // build portal model
        bool do_rebuild = true;
        const auto chunk_size = fs->find_chunk(fsL_PORTALS);

        rmPortals = xr_new<CDB::MODEL>();
        if (use_cache)
            rmPortals->set_model_crc32(crc32(fs->pointer(), chunk_size));

        string_path file_name;
        strconcat(file_name, "cdb_cache" DELIMITER, FS.get_path("$level$")->m_Add, "portals.bin");
        FS.update_path(file_name, "$app_data_root$", file_name);

        if (use_cache && FS.exist(file_name) && rmPortals->deserialize(file_name, skip_crc32_check))
        {
#ifndef MASTER_GOLD
            Msg("* Loaded portals cache (%s)...", file_name);
#endif
            do_rebuild = false;
        }
        else
        {
#ifndef MASTER_GOLD
            Msg("* Portals cache for '%s' was not loaded. "
                "Building the model from scratch..", file_name);
#endif
        }

        CDB::Collector CL;
        for (u32 i = 0; i < portals_count; i++)
        {
            ZoneScopedN("Build portal from chunk");
            auto &P = portals_data[i];
            fs->r(&P, sizeof(P));

            if (do_rebuild)
            {
                for (u32 j = 2; j < P.vertices.size(); j++)
                    CL.add_face_packed_D(P.vertices[0], P.vertices[j - 1], P.vertices[j], u32(i));
            }
        }

        if (do_rebuild)
        {
            if (CL.getTS() < 2)
            {
                Fvector v1, v2, v3;
                v1.set(-20000.f, -20000.f, -20000.f);
                v2.set(-20001.f, -20001.f, -20001.f);
                v3.set(-20002.f, -20002.f, -20002.f);
                CL.add_face_packed_D(v1, v2, v3, 0);
            }
            rmPortals->build(CL.getV(), CL.getVS(), CL.getT(), CL.getTS());
            if (use_cache)
                rmPortals->serialize(file_name);
        }
    }
    else
    {
        rmPortals = nullptr;
    }

    for (u32 id = 0; id < R__NUM_PARALLEL_CONTEXTS; ++id)
    {
        auto& dsgraph = contexts_pool[id];
        dsgraph.reset();
        dsgraph.load(sectors_data, portals_data);
        contexts_used.set(id, false);
    }

    auto& dsgraph = get_imm_context();
    dsgraph.reset();
    dsgraph.load(sectors_data, portals_data);

    last_sector_id = IRender_Sector::INVALID_SECTOR_ID;
}

void CRender::LoadSWIs(CStreamReader* base_fs)
{
    ZoneScoped;

    // allocate memory for portals
    if (base_fs->find_chunk(fsL_SWIS))
    {
        CStreamReader* fs = base_fs->open_chunk(fsL_SWIS);
        u32 item_count = fs->r_u32();

        for (auto& SWI : SWIs)
            xr_free(SWI.sw);

        SWIs.clear();

        SWIs.resize(item_count);
        for (u32 c = 0; c < item_count; c++)
        {
            FSlideWindowItem& swi = SWIs[c];
            swi.reserved[0] = fs->r_u32();
            swi.reserved[1] = fs->r_u32();
            swi.reserved[2] = fs->r_u32();
            swi.reserved[3] = fs->r_u32();
            swi.count = fs->r_u32();
            VERIFY(nullptr == swi.sw);
            swi.sw = xr_alloc<FSlideWindow>(swi.count);
            fs->r(swi.sw, sizeof(FSlideWindow) * swi.count);
        }
        fs->close();
    }
}

#if defined(USE_DX11)
void CRender::Load3DFluid()
{
    ZoneScoped;

    // if (strstr(Core.Params,"-no_volumetric_fog"))
    if (!o.volumetricfog)
        return;

    string_path fn_game;
    if (FS.exist(fn_game, "$level$", "level.fog_vol"))
    {
        IReader* F = FS.r_open(fn_game);
        u16 version = F->r_u16();

        if (version == 3)
        {
            u32 cnt = F->r_u32();
            for (u32 i = 0; i < cnt; ++i)
            {
                dx113DFluidVolume* pVolume = xr_new<dx113DFluidVolume>();
                pVolume->Load("", F, 0);

                auto& dsgraph = get_imm_context();

                //	Attach to sector's static geometry
                const auto sector_id = dsgraph.detect_sector(pVolume->getVisData().sphere.P);
                auto* pSector = static_cast<CSector*>(dsgraph.get_sector(sector_id));
                //	3DFluid volume must be in render sector
                VERIFY(pSector);

                dxRender_Visual* pRoot = pSector->root();
                //	Sector must have root
                VERIFY(pRoot);
                VERIFY(pRoot->getType() == MT_HIERRARHY);

                ((FHierrarhyVisual*)pRoot)->children.push_back(pVolume);
                // [DA_PORT] Запомнить владение: см. m_fluid_volumes в r2.h.
                m_fluid_volumes.push_back({ pRoot, pVolume });
            }
        }

        FS.r_close(F);
    }
}
#endif
} // namespace xray::render::RENDER_NAMESPACE
