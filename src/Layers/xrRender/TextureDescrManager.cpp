#include "stdafx.h"

#include "TextureDescrManager.h"
#include "ETextureParams.h"

#include "xrCore/Threading/ParallelForEach.hpp"

// [DA_PORT] Принудительный параллакс всем, у кого есть карта высот. См. xr_ioc_cmd.cpp.
//
// 🪤 Объявление обязано стоять ЗДЕСЬ, до открытия пространства имён, и обращаться к нему надо через
// `::`. Внутри xray::render::RENDER_NAMESPACE тот же extern даёт имя в ЭТОМ пространстве, и
// линковка падает на `undefined reference to __imp__ZN4xray6render9render_r420ps_r__parallax_forceE`
// — то есть ищет переменную, которой нигде нет. Компиляция при этом проходит молча.
extern ENGINE_API int ps_r__parallax_force;

namespace xray::render::RENDER_NAMESPACE
{
// eye-params
float r__dtex_range = 50;
class cl_dt_scaler : public R_constant_setup
{
public:
    float scale;

    cl_dt_scaler(float s) : scale(s) {}
    void setup(CBackend& cmd_list, R_constant* C) override { cmd_list.set_c(C, scale, scale, scale, 1 / r__dtex_range); }
};

void fix_texture_thm_name(pstr fn)
{
    pstr _ext = strext(fn);
    if (_ext && (!xr_stricmp(_ext, ".tga") || !xr_stricmp(_ext, ".thm") || !xr_stricmp(_ext, ".dds") ||
       !xr_stricmp(_ext, ".bmp") || !xr_stricmp(_ext, ".ogm")))
    {
        *_ext = 0;
    }
}

void CTextureDescrMngr::LoadLTX(pcstr initial, bool listTHM)
{
    ZoneScoped;

    string_path fname;
    FS.update_path(fname, initial, "textures.ltx");

    if (!FS.exist(fname))
        return;

#ifndef MASTER_GOLD
    Msg("Processing textures.ltx in [%s]", initial);
#endif

    CInifile ini(fname);

    Lock lock;
    if (ini.section_exist("association"))
    {
        CInifile::Sect& data = ini.r_section("association");
#ifndef MASTER_GOLD
        Msg("\tsection [%s] has %d lines", data.Name.c_str(), data.Data.size());
#endif
        m_texture_details.reserve(m_texture_details.size() + data.Data.size());
        m_detail_scalers.reserve(m_detail_scalers.size() + data.Data.size());

        const auto processAssociation = [&](const CInifile::Item& item)
        {
            ZoneScopedN("Process association");
            if (listTHM)
                Msg("\t\t%s = %s", item.first.c_str(), item.second.c_str());

            lock.Enter();
            texture_desc& desc = m_texture_details[item.first];
            cl_dt_scaler*& dts = m_detail_scalers[item.first];
            lock.Leave();

            if (desc.m_assoc)
                xr_delete(desc.m_assoc);

            desc.m_assoc = xr_new<texture_assoc>();

            string_path T;
            float s;

            const int res = sscanf(item.second.c_str(), "%[^,],%f", T, &s);
            R_ASSERT4(res == 2, "Bad texture association", item.first.c_str(), fname);
            desc.m_assoc->detail_name = T;
            if (dts)
                dts->scale = s;
            else
                dts = xr_new<cl_dt_scaler>(s);

            if (strstr(item.second.c_str(), "usage[diffuse_or_bump]"))
                desc.m_assoc->usage.set(texture_assoc::flDiffuseDetail | texture_assoc::flBumpDetail);
            else if (strstr(item.second.c_str(), "usage[bump]"))
                desc.m_assoc->usage.set(texture_assoc::flBumpDetail);
            else if (strstr(item.second.c_str(), "usage[diffuse]"))
                desc.m_assoc->usage.set(texture_assoc::flDiffuseDetail);
        };
        xr_parallel_for_each(data.Data, processAssociation);
    } // "association"

    if (ini.section_exist("specification"))
    {
        CInifile::Sect& data = ini.r_section("specification");
#ifndef MASTER_GOLD
        Msg("\tsection [%s] has %d lines", data.Name.c_str(), data.Data.size());
#endif
        m_texture_details.reserve(m_texture_details.size() + data.Data.size());

        const auto processSpecification = [&](const CInifile::Item& item)
        {
            if (listTHM)
                Msg("\t\t%s = %s", item.first.c_str(), item.second.c_str());

            lock.Enter();
            texture_desc& desc = m_texture_details[item.first];
            lock.Leave();

            if (desc.m_spec)
                xr_delete(desc.m_spec);

            desc.m_spec = xr_new<texture_spec>();

            string_path bmode;
            const int res =
                    sscanf(item.second.c_str(), "bump_mode[%[^]]], material[%f]", bmode, &desc.m_spec->m_material);
            R_ASSERT4(res == 2, "Bad texture specification", item.first.c_str(), fname);
            if ((bmode[0] == 'u') && (bmode[1] == 's') && (bmode[2] == 'e') && (bmode[3] == ':'))
            {
                // bump-map specified
                desc.m_spec->m_bump_name = bmode + 4;
            }
        };
        xr_parallel_for_each(data.Data, processSpecification);
    } // "specification"
}

void CTextureDescrMngr::LoadTHM(LPCSTR initial, bool listTHM)
{
    ZoneScoped;

    FS_FileSet flist;
    FS.file_list(flist, initial, FS_ListFiles, "*.thm");

    if (flist.empty())
        return;

#ifndef MASTER_GOLD
    Msg("Processing %d .thm files in [%s]", flist.size(), initial);
#endif


    m_texture_details.reserve(m_texture_details.size() + flist.size());
    m_detail_scalers.reserve(m_detail_scalers.size() + flist.size());

    Lock lock;
    const auto processFile = [&](const FS_File& it)
    {
        ZoneScopedN("Process file");
        // Alundaio: Print list of *.thm to find bad .thms!
        if (listTHM)
            Log("\t", it.name.c_str());

        string_path fn;
        FS.update_path(fn, initial, it.name.c_str());
        IReader* F = FS.r_open(fn);
        R_ASSERT3(F, "Failed to open THM (case-sensitivity problem?)", it.name.c_str());

        xr_strcpy(fn, it.name.c_str());
        fix_texture_thm_name(fn);

        R_ASSERT3(F->find_chunk(THM_CHUNK_TYPE), "Cannot find THM chunk in file", fn);
        F->r_u32();
        STextureParams tp;
        // [DA_PORT] Битый/усечённый .thm пропускаем поимённо, а не читаем за его концом. Load теперь
        // возвращает false, если чанк параметров короче фиксированной части (см. ETextureParams.cpp).
        // Раньше при таком файле чтение уходило за границу: в релизе — молча мусорные width/height,
        // в Mixed — падение на VERIFY(Pos<=Size). Имя файла в логе — чтобы негодный .thm было видно.
        if (!tp.Load(*F))
        {
            Msg("! [DA] битый .thm (усечён чанк параметров), пропущен: %s", it.name.c_str());
            FS.r_close(F);
            return;
        }
        FS.r_close(F);
        if (STextureParams::ttImage == tp.type || STextureParams::ttTerrain == tp.type ||
            STextureParams::ttNormalMap == tp.type)
        {
            lock.Enter();
            texture_desc& desc = m_texture_details[fn];
            cl_dt_scaler*& dts = m_detail_scalers[fn];
            lock.Leave();

            if (tp.detail_name.size() &&
                tp.flags.is_any(STextureParams::flDiffuseDetail | STextureParams::flBumpDetail))
            {
                if (desc.m_assoc)
                    xr_delete(desc.m_assoc);

                desc.m_assoc = xr_new<texture_assoc>();
                desc.m_assoc->detail_name = tp.detail_name;
                if (dts)
                    dts->scale = tp.detail_scale;
                else
                    dts = xr_new<cl_dt_scaler>(tp.detail_scale);

                if (tp.flags.is(STextureParams::flDiffuseDetail))
                    desc.m_assoc->usage.set(texture_assoc::flDiffuseDetail);

                if (tp.flags.is(STextureParams::flBumpDetail))
                    desc.m_assoc->usage.set(texture_assoc::flBumpDetail);
            }
            if (desc.m_spec)
                xr_delete(desc.m_spec);

            desc.m_spec = xr_new<texture_spec>();
            desc.m_spec->m_material = float(tp.material) + tp.material_weight;
            desc.m_spec->m_use_steep_parallax = false;

            if (tp.bump_mode == STextureParams::tbmUse)
            {
                desc.m_spec->m_bump_name = tp.bump_name;
            }
            else if (tp.bump_mode == STextureParams::tbmUseParallax)
            {
                desc.m_spec->m_bump_name = tp.bump_name;
                desc.m_spec->m_use_steep_parallax = true;
            }
        }
    };
    if (!listTHM)
        xr_parallel_for_each(flist, processFile);
    else
    {
        // We need precise info in the log when listTHM is specified.
        // Load in single thread, file by file.
        for (const FS_File& file : flist)
            processFile(file);
    }
}

// [DA_PORT] Список материалов, которым разрешён шестиугольный разрыв повторов.
//
// Зачем список вообще. Приём годится ТОЛЬКО там, где развёртка тайловая, а признака этого в данных
// нет: ни в .thm, ни где-либо ещё. Первый заход включил его глобально — и предметы с уникальной
// развёрткой (бочка) превратились в кашу, потому что случайный сдвиг взял кусок из другого места
// картинки.
//
// Откуда список. Отобран прибором da_port/tools/hex/classify.py прямо по картинкам: разрыв на стыке
// краёв, доля прозрачных пикселей, наличие рисунка на краях и периодичность узора. Из 2528 текстур
// прошли 268. Нейросеть для этого не нужна — все четыре величины меряются напрямую.
//
// Формат простой намеренно: имя текстуры на строку, «;» начинает примечание. Точка с запятой и
// пустые строки пропускаются, так что прибор может писать рядом свои числа.
void CTextureDescrMngr::LoadHexList()
{
    ZoneScoped;

    string_path fname;
    FS.update_path(fname, "$game_config$", "da_hex_tiling.ltx");
    if (!FS.exist(fname))
        return;

    IReader* F = FS.r_open(fname);
    if (!F)
        return;

    string_path line;
    u32 count = 0;
    while (!F->eof())
    {
        F->r_string(line, sizeof(line));

        // Отрезаем примечание и пробелы по краям.
        if (pstr sep = strchr(line, ';'))
            *sep = 0;
        if (pstr sep = strchr(line, '\t'))
            *sep = 0;
        pstr p = line;
        while (*p == ' ')
            ++p;
        for (int i = (int)xr_strlen(p) - 1; i >= 0 && (p[i] == ' ' || p[i] == '\r'); --i)
            p[i] = 0;
        if (!p[0])
            continue;

        // Ключи описаний текстур хранятся с разделителем платформы, а прибор пишет через «/».
        // Приводим к одному виду, иначе поиск не найдёт ни одной записи и список окажется
        // «рабочим», но пустым по существу — ровно тот отказ, который никак себя не проявляет.
        for (pstr c = p; *c; ++c)
            if (*c == '/')
                *c = '\\';
        xr_strlwr(p);

        m_hex_tiling[shared_str(p)] = 1;
        ++count;
    }
    FS.r_close(F);

#ifndef MASTER_GOLD
    Msg("* [DA] разрыв повторов: разрешён %u материалам", count);
#endif
}

BOOL CTextureDescrMngr::UseHexTiling(const shared_str& tex_name) const
{
    if (m_hex_tiling.empty())
        return FALSE;

    string_path key;
    xr_strcpy(key, tex_name.c_str());
    for (pstr c = key; *c; ++c)
        if (*c == '/')
            *c = '\\';
    xr_strlwr(key);

    return m_hex_tiling.find(shared_str(key)) != m_hex_tiling.end() ? TRUE : FALSE;
}

void CTextureDescrMngr::Load()
{
    ZoneScoped;
#ifndef MASTER_GOLD
    CTimer timer;
    timer.Start();
#endif // #ifdef DEBUG

    const bool listTHM = strstr(Core.Params, "-list_thm");

    LoadLTX("$game_textures$", listTHM);
    LoadLTX("$level$", listTHM);

    LoadTHM("$game_textures$", listTHM);
    LoadTHM("$level$", listTHM);

    LoadHexList(); // [DA_PORT] см. выше

#ifndef MASTER_GOLD
    Msg("%s, texture descriptions loaded for %d ms", __FUNCTION__, timer.GetElapsed_ms());
#endif
}

void CTextureDescrMngr::UnLoad()
{
    for (auto& it : m_texture_details)
    {
        xr_delete(it.second.m_assoc);
        xr_delete(it.second.m_spec);
    }
    m_texture_details.clear();
    m_hex_tiling.clear(); // [DA_PORT] иначе список пережил бы выгрузку уровня и оброс дублями
}

CTextureDescrMngr::~CTextureDescrMngr()
{
    ZoneScoped;

    for (auto& it : m_detail_scalers)
        xr_delete(it.second);

    m_detail_scalers.clear();
}

shared_str CTextureDescrMngr::GetBumpName(const shared_str& tex_name) const
{
    map_TD::const_iterator I = m_texture_details.find(tex_name);
    if (I != m_texture_details.end())
    {
        if (I->second.m_spec)
        {
            return I->second.m_spec->m_bump_name;
        }
    }
    return "";
}

BOOL CTextureDescrMngr::UseSteepParallax(const shared_str& tex_name) const
{
    map_TD::const_iterator I = m_texture_details.find(tex_name);
    if (I != m_texture_details.end())
    {
        if (I->second.m_spec)
        {
            if (I->second.m_spec->m_use_steep_parallax)
                return TRUE;

            // [DA_PORT] Замер 20.08 по всем 5985 .thm: параллакс просят 112 текстур, а карту высот
            // несут 2020 — готовых поверхностей в восемнадцать раз больше, чем помеченных. Пометка
            // ставилась вручную в редакторе GSC, и до большей части рук просто не дошло.
            //
            // Непустое имя рельефа значит, что у текстуры есть пара _bump/_bump#, а высота лежит в
            // альфе второй — ровно то, что читает UpdateTC. Так что признак здесь тот же самый, что
            // и у отмеченных вручную, разница только в том, кто его проставил.
            //
            // ⚠️ Требует перезагрузки уровня: имя шейдера собирается один раз, при постройке
            // блендера (uber_deffer.cpp), и на лету не меняется.
            if (::ps_r__parallax_force && I->second.m_spec->m_bump_name.size() > 2)
                return TRUE;
        }
    }
    return FALSE;
}

float CTextureDescrMngr::GetMaterial(const shared_str& tex_name) const
{
    map_TD::const_iterator I = m_texture_details.find(tex_name);
    if (I != m_texture_details.end())
    {
        if (I->second.m_spec)
        {
            return I->second.m_spec->m_material;
        }
    }
    return 1.0f;
}

void CTextureDescrMngr::GetTextureUsage(const shared_str& tex_name, bool& bDiffuse, bool& bBump) const
{
    map_TD::const_iterator I = m_texture_details.find(tex_name);
    if (I != m_texture_details.end())
    {
        if (I->second.m_assoc)
        {
            auto& usage = I->second.m_assoc->usage;
            bDiffuse = usage.test(texture_assoc::flDiffuseDetail);
            bBump = usage.test(texture_assoc::flBumpDetail);
        }
    }
}

BOOL CTextureDescrMngr::GetDetailTexture(const shared_str& tex_name, LPCSTR& res, R_constant_setup*& CS) const
{
    map_TD::const_iterator I = m_texture_details.find(tex_name);
    if (I != m_texture_details.end())
    {
        if (I->second.m_assoc)
        {
            texture_assoc* TA = I->second.m_assoc;
            res = TA->detail_name.c_str();
            map_CS::const_iterator It2 = m_detail_scalers.find(tex_name);
            CS = It2 == m_detail_scalers.end() ? 0 : It2->second;
            return TRUE;
        }
    }
    return FALSE;
}
} // namespace xray::render::RENDER_NAMESPACE
