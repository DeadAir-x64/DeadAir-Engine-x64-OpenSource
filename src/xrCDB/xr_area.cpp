#include "stdafx.h"

#include "xr_area.h"
#include "xrEngine/xr_object.h"
#include "Common/LevelStructure.hpp"
#include "xrEngine/xr_collide_form.h"

#include <filesystem>


//----------------------------------------------------------------------
// Class	: CObjectSpaceData
// Purpose	: stores thread sensitive data
//----------------------------------------------------------------------
thread_local xrXRC CObjectSpaceData::xrc("object space");
thread_local collide::rq_results CObjectSpaceData::r_temp;
thread_local xr_vector<ISpatial*> CObjectSpaceData::r_spatial;

using namespace collide;

namespace
{
// [DA_PORT] Кэш столкновений ограничен по объёму, а не вычищается целиком.
//
// Движок кладёт в appdata\cdb_cache каталог на каждый посещённый уровень и не убирает их никогда:
// на машине разработки накопилось 2.6 ГБ в 28 каталогах, причём одно только Кладбище техники — 491
// МБ. У игрока будет столько же, и он не поймёт, откуда.
//
// В Dead Air Refined это лечится удалением ВСЕХ каталогов, кроме текущего. Для линейного
// прохождения годится, для нас нет: здесь ходят между уровнями свободно, и такая уборка означала бы
// перестроение кэша при каждом возвращении — а он затем и нужен, чтобы повторная загрузка была
// быстрой.
//
// Поэтому держим предел по сумме: пока она превышена, удаляем каталоги, к которым дольше всего не
// обращались. Текущий не трогаем никогда. Так и диск не растёт без края, и уровни, между которыми
// игрок ходит чаще всего, остаются тёплыми.
//
// Предел меняется ключом -cdb_cache_limit_mb <число>; 0 отключает уборку совсем.
constexpr u64 DA_CDB_CACHE_LIMIT_MB_DEFAULT = 1536;

u64 da_directory_size(const std::filesystem::path& directory, std::error_code& error)
{
    u64 total = 0;
    std::filesystem::directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    if (error)
        return 0;

    for (; it != end; it.increment(error))
    {
        if (error)
            break;

        // ⚠️ Ошибку file_size проверять ОБЯЗАТЕЛЬНО: при неудаче он возвращает (uintmax_t)-1, то есть
        // около восемнадцати экзабайт. Одна такая величина переполнила бы сумму, предел оказался бы
        // «превышен» — и уборка снесла бы весь кэш до последнего уровня.
        std::error_code local;
        if (!it->is_regular_file(local) || local)
            continue;

        const std::uintmax_t size = it->file_size(local);
        if (local)
            continue;

        total += u64(size);
    }
    error.clear();
    return total;
}

// [DA_PORT] Отметка «этим кэшем только что пользовались».
//
// Без неё выбор жертвы шёл бы по времени СОЗДАНИЯ каталога: файл кэша пишется один раз, а при чтении
// его время не меняется. Тогда уровень, куда игрок ходит каждый день, удалялся бы наравне с
// забытым — лишь потому, что создан раньше. Отмечаем каталог при каждой удачной загрузке, и выбор
// становится честным: уходит то, что дольше всего не грузили.
void da_touch_cache_dir(const std::filesystem::path& cacheFile)
{
    const std::filesystem::path directory = cacheFile.parent_path();
    if (directory.empty())
        return;

    std::error_code error;
    std::filesystem::last_write_time(directory, std::filesystem::file_time_type::clock::now(), error);
}

void prune_inactive_level_caches(const std::filesystem::path& activeCacheFile)
{
    const std::filesystem::path activeDirectory = activeCacheFile.parent_path();
    const std::filesystem::path cacheRoot = activeDirectory.parent_path();
    if (activeDirectory.empty() || cacheRoot.empty() || activeCacheFile.filename() != "objspace.bin" ||
        cacheRoot.filename() != "cdb_cache")
        return;

    u64 limit_mb = DA_CDB_CACHE_LIMIT_MB_DEFAULT;
    if (const char* const param = strstr(Core.Params, "-cdb_cache_limit_mb"))
    {
        u64 parsed = 0;
        if (sscanf(param, "-cdb_cache_limit_mb %llu", &parsed) == 1)
            limit_mb = parsed;
    }
    if (!limit_mb)
        return;

    struct da_cache_entry
    {
        std::filesystem::path path;
        std::filesystem::file_time_type touched;
        u64 bytes;
    };

    std::error_code error;
    std::filesystem::directory_iterator iterator(
        cacheRoot, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    if (error)
        return;

    xr_vector<da_cache_entry> entries;
    u64 total = 0;

    for (; iterator != end; iterator.increment(error))
    {
        if (error)
            break;

        const std::filesystem::directory_entry& entry = *iterator;

        const std::filesystem::file_status status = entry.symlink_status(error);
        if (error)
        {
            error.clear();
            continue;
        }
        if (status.type() != std::filesystem::file_type::directory)
            continue;

        // Каталог кэша узнаём по содержимому: мало ли что ещё окажется рядом.
        bool containsCacheFile = false;
        for (const char* cacheFile : { "objspace.bin", "hom.bin", "portals.bin" })
        {
            containsCacheFile = std::filesystem::is_regular_file(entry.path() / cacheFile, error);
            if (containsCacheFile)
                break;
            error.clear();
        }
        if (!containsCacheFile)
            continue;

#ifdef _WIN32
        // Точку повторного разбора не трогаем: рекурсивное удаление по ней ушло бы за пределы каталога.
        const DWORD attributes = GetFileAttributesW(entry.path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
#endif

        std::error_code local;
        const u64 bytes = da_directory_size(entry.path(), local);
        total += bytes;

        if (entry.path() == activeDirectory)
            continue; // текущий уровень в счёт идёт, а под нож — никогда

        const std::filesystem::file_time_type touched = std::filesystem::last_write_time(entry.path(), local);
        entries.push_back({ entry.path(), local ? std::filesystem::file_time_type::min() : touched, bytes });
    }

    const u64 limit_bytes = limit_mb * 1024ull * 1024ull;
    if (total <= limit_bytes || entries.empty())
        return;

    // Первыми уходят те, к которым дольше всего не обращались.
    std::sort(entries.begin(), entries.end(),
        [](const da_cache_entry& a, const da_cache_entry& b) { return a.touched < b.touched; });

    u32 removedDirectories = 0;
    u64 removedBytes = 0;
    for (const da_cache_entry& victim : entries)
    {
        if (total <= limit_bytes)
            break;

        // [DA_PORT] Удалять только через файловую систему движка.
        //
        // CLocatorAPI ведёт СВОЙ каталог файлов, и удаление мимо него оставляет в индексе записи о
        // том, чего на диске уже нет. Дальше движок «находит» несуществующий файл и получает отказ
        // на пустом месте. Ту же грабля мы прошли на сейвах: там пришлось звать FS.file_rename
        // вместо системного переименования, иначе сейв исчезал из списка загрузки.
        //
        // Замечено в Dead Air Refined («Keep level cache pruning synchronized with VFS») — их первая
        // версия чистила через std::filesystem и наступила ровно на это.
        const std::string directory = victim.path.string() + DELIMITER;
        FS.dir_delete(directory.c_str(), true);

        std::error_code local;
        if (std::filesystem::exists(victim.path, local) || local)
        {
            local.clear();
            continue; // не ушёл — в счёт не берём, попробуем в следующий раз
        }

        total -= victim.bytes;
        removedBytes += victim.bytes;
        ++removedDirectories;
    }

    if (removedDirectories)
        Msg("* [DA_PORT] кэш столкновений: удалено уровней %u (%llu МБ), осталось %llu МБ из %llu",
            removedDirectories, removedBytes / (1024ull * 1024ull), total / (1024ull * 1024ull), limit_mb);
}
} // namespace

//----------------------------------------------------------------------
// Class	: CObjectSpace
// Purpose	: stores space slots
//----------------------------------------------------------------------
CObjectSpace::CObjectSpace(ISpatial_DB* spatialSpace)
    : SpatialSpace(spatialSpace)
{
#ifdef DEBUG
    if (GEnv.RenderFactory)
        m_pRender = xr_new<FactoryPtr<IObjectSpaceRender>>();
#endif
    m_BoundingVolume.invalidate();
}
//----------------------------------------------------------------------
CObjectSpace::~CObjectSpace()
{
#ifdef DEBUG
    xr_delete(m_pRender);
#endif
}
//----------------------------------------------------------------------

//----------------------------------------------------------------------
int CObjectSpace::GetNearest(xr_vector<ISpatial*>& q_spatial, xr_vector<IGameObject*>& q_nearest, const Fvector& point,
    float range, IGameObject* ignore_object)
{
    ZoneScoped;

    q_spatial.clear();
    // Query objects
    q_nearest.clear();
    Fsphere Q;
    Q.set(point, range);
    Fvector B;
    B.set(range, range, range);
    SpatialSpace->q_box(q_spatial, 0, STYPE_COLLIDEABLE, point, B);

    // Iterate
    for (auto& it : q_spatial)
    {
        IGameObject* O = it->dcast_GameObject();
        if (0 == O)
            continue;
        if (O == ignore_object)
            continue;
        Fsphere mS = {O->GetSpatialData().sphere.P, O->GetSpatialData().sphere.R};
        if (Q.intersect(mS))
            q_nearest.push_back(O);
    }

    return q_nearest.size();
}

//----------------------------------------------------------------------
int CObjectSpace::GetNearest(
    xr_vector<IGameObject*>& q_nearest, const Fvector& point, float range, IGameObject* ignore_object)
{
    return (GetNearest(r_spatial, q_nearest, point, range, ignore_object));
}

//----------------------------------------------------------------------
int CObjectSpace::GetNearest(xr_vector<IGameObject*>& q_nearest, ICollisionForm* obj, float range)
{
    IGameObject* O = obj->Owner();
    return GetNearest(q_nearest, O->GetSpatialData().sphere.P, range + O->GetSpatialData().sphere.R, O);
}

//----------------------------------------------------------------------

void CObjectSpace::Load(CDB::build_callback build_callback,
    CDB::serialize_callback serialize_callback,
    CDB::deserialize_callback deserialize_callback,
    CDB::remapping_materials_callback remapping_materials_callback)
{
    Load("$level$", "level.cform", build_callback, serialize_callback, deserialize_callback, remapping_materials_callback);
}

void CObjectSpace::Load(LPCSTR path, LPCSTR fname,
    CDB::build_callback build_callback,
    CDB::serialize_callback serialize_callback,
    CDB::deserialize_callback deserialize_callback,
    CDB::remapping_materials_callback remapping_materials_callback)
{
    IReader* F = FS.r_open(path, fname);
    R_ASSERT(F);
    Load(F, build_callback, serialize_callback, deserialize_callback, remapping_materials_callback);
}

void CObjectSpace::Load(IReader* F,
    CDB::build_callback build_callback,
    CDB::serialize_callback serialize_callback,
    CDB::deserialize_callback deserialize_callback,
    CDB::remapping_materials_callback remapping_materials_callback)
{
    ZoneScoped;

    static const bool use_cache = !strstr(Core.Params, "-no_cdb_cache");
    if (use_cache)
        Static.set_model_crc32(crc32(F->pointer(), F->length()));

    hdrCFORM H;
    F->r(&H, sizeof(hdrCFORM));

    Fvector* verts = (Fvector*)F->pointer();
    CDB::TRI* tris = (CDB::TRI*)(verts + H.vertcount);

    // SkyLoader: Check for the new format
    IReader* cacheStream = nullptr;
    size_t totalGeomSize = (static_cast<size_t>(H.vertcount) * sizeof(Fvector)) + (H.facecount * sizeof(CDB::TRI));
    F->advance(totalGeomSize);
    if (F->elapsed() > sizeof(u32))
    {
        u32 version = F->r_u32();
        if (version == CFORM_CACHE_CURRENT_VERSION)
            cacheStream = F;
    }

    Create(verts, tris, H, build_callback, serialize_callback, deserialize_callback, remapping_materials_callback, cacheStream);
    FS.r_close(F);
}

void CObjectSpace::Create(Fvector* verts, CDB::TRI* tris, const hdrCFORM& H,
    CDB::build_callback build_callback,
    CDB::serialize_callback serialize_callback,
    CDB::deserialize_callback deserialize_callback,
    CDB::remapping_materials_callback remapping_materials_callback,
    IReader* cacheStream /*= nullptr*/)
{
    ZoneScoped;

    R_ASSERT(CFORM_CURRENT_VERSION == H.version);

    string_path file_name;
    static const bool use_cache = !strstr(Core.Params, "-no_cdb_cache");
    static const bool skip_crc32_check = strstr(Core.Params, "-skip_cdb_cache_crc32_check");

    strconcat(file_name, "cdb_cache" DELIMITER, FS.get_path("$level$")->m_Add, "objspace.bin");
    FS.update_path(file_name, "$app_data_root$", file_name);

    // [DA_PORT] Чужие уровни чистим здесь: путь к своему уже собран, и он же служит опорой проверок.
    if (use_cache)
        prune_inactive_level_caches(std::filesystem::path(file_name));

    if (use_cache && cacheStream)
    {
#ifndef MASTER_GOLD
        Msg("* Loading ObjectSpace cache from level.cform...");
#endif

        // Load geometry
        Static.load_geom(verts, H.vertcount, tris, H.facecount);

        // Read game material list
        xr_map<u16, shared_str> gameMtls;
        u32 cnt = cacheStream->r_u32();
        for (u32 i = 0; i < cnt; i++)
        {
            u16 idx = cacheStream->r_u16();
            shared_str mtlName;
            cacheStream->r_stringZ(mtlName);
            gameMtls[idx] = mtlName;
        }

        if (remapping_materials_callback)
            remapping_materials_callback(Static.get_tris(), Static.get_tris_count(), gameMtls);

        // Load OPCODE tree
        Static.deserialize_tree(cacheStream);
    }
    else if (use_cache && FS.exist(file_name) && Static.deserialize(file_name, skip_crc32_check, deserialize_callback))
    {
        da_touch_cache_dir(std::filesystem::path(file_name)); // [DA_PORT] см. da_touch_cache_dir
#ifndef MASTER_GOLD
        Msg("* Loaded ObjectSpace cache (%s)...", file_name);
#endif
    }
    else
    {
#ifndef MASTER_GOLD
        Msg("* ObjectSpace cache for '%s' was not loaded. "
            "Building the model from scratch..", file_name);
#endif
        Static.build(verts, H.vertcount, tris, H.facecount, build_callback);

        if (use_cache)
            Static.serialize(file_name, serialize_callback);
    }

    m_BoundingVolume.set(H.aabb);
}

//----------------------------------------------------------------------
#ifdef DEBUG
void CObjectSpace::dbgRender() { (*m_pRender)->dbgRender(); }
/*
void CObjectSpace::dbgRender()
{
    R_ASSERT(bDebug);

    RCache.set_Shader(sh_debug);
    for (u32 i=0; i<q_debug.boxes.size(); i++)
    {
        Fobb&		obb		= q_debug.boxes[i];
        Fmatrix		X,S,R;
        obb.xform_get(X);
        RCache.dbg_DrawOBB(X,obb.m_halfsize,color_xrgb(255,0,0));
        S.scale		(obb.m_halfsize);
        R.mul		(X,S);
        RCache.dbg_DrawEllipse(R,color_xrgb(0,0,255));
    }
    q_debug.boxes.clear();

    for (i=0; i<dbg_S.size(); i++)
    {
        std::pair<Fsphere,u32>& P = dbg_S[i];
        Fsphere&	S = P.first;
        Fmatrix		M;
        M.scale		(S.R,S.R,S.R);
        M.translate_over(S.P);
        RCache.dbg_DrawEllipse(M,P.second);
    }
    dbg_S.clear();
}
*/
#endif
// XXX stats: add to statistics
void CObjectSpace::DumpStatistics(IGameFont& font, IPerformanceAlert* alert) { xrc.DumpStatistics(font, alert); }
