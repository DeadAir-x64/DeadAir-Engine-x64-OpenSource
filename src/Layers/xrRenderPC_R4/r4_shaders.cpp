#include "stdafx.h"
#include "r2.h"
#include "Layers/xrRender/ShaderResourceTraits.h"
#include "xrCore/FileCRC32.h"
#include "xrCore/Threading/ParallelFor.hpp" // [DA_PORT] прогрев кэша шейдеров, см. da_shader_warmup

namespace xray::render::RENDER_NAMESPACE
{
void CRender::addShaderOption(const char* name, const char* value)
{
    D3D_SHADER_MACRO macro = {name, value};
    m_ShaderOptions.push_back(macro);
}

template <typename T>
static HRESULT create_shader(DWORD const* buffer, size_t const buffer_size, LPCSTR const file_name,
    T*& result, bool const dx9compatibility)
{
    HRESULT _hr = ShaderTypeTraits<T>::CreateHWShader(buffer, buffer_size, result->sh);
    if (!SUCCEEDED(_hr))
    {
        Log("! Shader: ", file_name);
        Msg("! CreateHWShader hr == 0x%08x", _hr);
        return E_FAIL;
    }

#ifdef DEBUG
    if (result->sh)
    {
        result->sh->SetPrivateData(WKPDID_D3DDebugObjectName, xr_strlen(file_name), file_name);
    }
#endif

    ID3DShaderReflection* pReflection = 0;
    _hr = D3DReflect(buffer, buffer_size, IID_ID3DShaderReflection, (void**)&pReflection);

    if (SUCCEEDED(_hr) && pReflection)
    {
        result->constants.dx9compatibility = dx9compatibility;
        // Parse constant table data
        result->constants.parse(pReflection, ShaderTypeTraits<T>::GetShaderDest());

        // [DA_PORT] Tell a mod author when their shader will not work with the upscalers.
        //
        // A pixel shader that draws into the G-buffer must also write the motion vector, colour target
        // 2. If it does not, the target keeps the zero it was cleared to - and zero does not mean
        // "unknown" to FSR, it means "this pixel did not move on screen". The object is then rebuilt
        // from history fetched where it used to be, which looks like the model smearing or trailing
        // behind the camera. Nothing fails, nothing is logged by the engine, and the artefact points
        // nowhere near the shader that caused it. This is the message that would have saved that hunt.
        //
        // The test needs no naming convention and no guessing. Deferred scene shaders write colour
        // targets 0 and 1 (position and albedo); post-process and blit shaders write target 0 alone.
        // So "writes 1 but not 2" identifies a scene shader missing its velocity output exactly, and
        // leaves every legitimate single-target shader alone.
        //
        // Only while an upscaler actually needs the vectors: with velocity off nothing consumes them
        // and the message would be noise.
        if constexpr (std::is_same_v<T, SPS>)
        {
            if (RImplementation.o.velocity)
            {
                D3D11_SHADER_DESC shader_desc{};
                if (SUCCEEDED(pReflection->GetDesc(&shader_desc)))
                {
                    bool writes_1 = false, writes_2 = false;
                    for (UINT i = 0; i < shader_desc.OutputParameters; ++i)
                    {
                        D3D11_SIGNATURE_PARAMETER_DESC param{};
                        if (FAILED(pReflection->GetOutputParameterDesc(i, &param)) || !param.SemanticName)
                            continue;
                        if (xr_strcmp(param.SemanticName, "SV_Target") != 0)
                            continue;
                        if (param.SemanticIndex == 1)
                            writes_1 = true;
                        else if (param.SemanticIndex == 2)
                            writes_2 = true;
                    }

                    // [DA_PORT] Name-gated, and the first attempt without it was wrong.
                    //
                    // The idea was that only G-buffer shaders write two colour targets, so "writes 1
                    // but not 2" would identify them without caring what they are called. That is
                    // false: full-screen passes write two targets for their own reasons - the combine
                    // splits the frame into low and high parts, and our temporal resolve writes the
                    // screen and the history separately. The check duly accused da_taa.ps,
                    // combine_1_nomsaa.ps and combine_volumetric.ps, none of which touch the G-buffer.
                    //
                    // Every deferred scene shader in this engine is named deffer_*, and a mod adding
                    // one either follows that convention or has copied a file that does. Narrower than
                    // intended, but a diagnostic that cries wolf on the engine's own shaders is worse
                    // than one that covers slightly less.
                    const bool is_scene_shader = file_name && strstr(file_name, "deffer") != nullptr;

                    if (is_scene_shader && writes_1 && !writes_2)
                    {
                        Msg("! [DA_PORT] shader [%s] draws into the G-buffer but writes no motion vector "
                            "(colour target 2). Anything using it will smear or trail under FSR/XeSS. "
                            "Fix: output the velocity the way gamedata\shaders\r3\deffer_model_bump.vs "
                            "and pack_gbuffer do.", file_name);
                    }
                }
            }
        }

        // [DA_PORT] Второй молчаливый отказ той же природы: ВЕРШИННЫЙ шейдер без джиттера.
        //
        // Апскейлер собирает кадр из подпиксельных сдвигов: каждый кадр сцена рисуется чуть смещённой,
        // и из этих смещений набирается разрешение. Шейдер, который смещение не применяет, каждый кадр
        // кладёт свою геометрию в одну и ту же точку — накапливать ему нечего, и его край остаётся
        // ступенчатым при любом качестве апскейлера. На тусклом предмете это незаметно, на ярком (и на
        // чёрном фоне) читается как пила по кромке.
        //
        // Проверка не по имени файла: любой шейдер, который переводит геометрию в клип-пространство,
        // объявляет `m_WVP`. Полноэкранные и постпроцессные — нет, поэтому ложных срабатываний тут не
        // будет. Отсутствие `m_taa_jitter` у такого шейдера означает ровно одно.
        //
        // Ловушка, из-за которой эта проверка и понадобилась: соседняя, про векторы движения, смотрит
        // на ЦЕЛИ вывода и потому слепа к проходу свечения — он пишет одну цель, как постобработка.
        if constexpr (std::is_same_v<T, SVS>)
        {
            if (RImplementation.o.velocity)
            {
                // Кому джиттер не нужен по существу, а не по недосмотру: тени рисуются проекцией
                // ИСТОЧНИКА СВЕТА (подпиксельный сдвиг экрана там бессмыслен), световые объёмы и
                // маски накопления не попадают на экран сами по себе, заглушки не рисуют ничего.
                // Без этого списка сообщение кричит на два десятка невиновных и его перестают читать.
                const bool exempt = file_name &&
                    (strstr(file_name, "shadow_") || strstr(file_name, "accum_") ||
                     strstr(file_name, "lplanes") || strstr(file_name, "stub_") ||
                     strstr(file_name, "dumb"));

                // `m_WVP` — самый частый способ, но не единственный: часть шейдеров переводит
                // геометрию через `m_W` + `m_VP` по отдельности. Спрашиваем про все три, иначе
                // проверка молча пропустит именно те, что собраны иначе.
                const bool has_wvp = !!result->constants.get("m_WVP") ||
                    !!result->constants.get("m_VP") || !!result->constants.get("m_W");
                const bool has_jitter = !!result->constants.get("m_taa_jitter");

                if (!exempt && has_wvp && !has_jitter)
                {
                    Msg("! [DA_PORT] шейдер [%s] рисует геометрию, но не применяет джиттер "
                        "(m_taa_jitter). Под апскейлером его край останется ступенчатым: сдвига нет — "
                        "накапливать нечего. Как надо: gamedata\\shaders\\r3\\deffer_model_flat.vs, "
                        "строка `O.hpos.xy += m_taa_jitter.xy * O.hpos.w`.", file_name);
                }
            }
        }

        _RELEASE(pReflection);
    }
    else
    {
        Msg("! D3DReflectShader %s hr == 0x%08x", file_name, _hr);
    }

    return _hr;
}

static HRESULT create_shader(LPCSTR const pTarget, DWORD const* buffer, size_t const buffer_size, LPCSTR const file_name,
    void*& result, bool const disasm, bool dx9compatibility)
{
    HRESULT _result = E_FAIL;
    pcstr extension = ".hlsl";
    if (pTarget[0] == 'p')
    {
        extension = ".ps";
        _result = create_shader(buffer, buffer_size, file_name, (SPS*&)result, dx9compatibility);
    }
    else if (pTarget[0] == 'v')
    {
        extension = ".vs";
        SVS* svs_result = (SVS*)result;
        _result = create_shader(buffer, buffer_size, file_name, svs_result, dx9compatibility);
        if (SUCCEEDED(_result))
        {
            //	Store input signature (need only for VS)
            ID3DBlob* pSignatureBlob;
            CHK_DX(D3DGetInputSignatureBlob(buffer, buffer_size, &pSignatureBlob));
            VERIFY(pSignatureBlob);

            svs_result->signature = RImplementation.Resources->_CreateInputSignature(pSignatureBlob);

            _RELEASE(pSignatureBlob);
        }
    }
    else if (pTarget[0] == 'g')
    {
        extension = ".gs";
        _result = create_shader(buffer, buffer_size, file_name, (SGS*&)result, dx9compatibility);
    }
    else if (pTarget[0] == 'c')
    {
        extension = ".cs";
        _result = create_shader(buffer, buffer_size, file_name, (SCS*&)result, dx9compatibility);
    }
    else if (pTarget[0] == 'h')
    {
        extension = ".hs";
        _result = create_shader(buffer, buffer_size, file_name, (SHS*&)result, dx9compatibility);
    }
    else if (pTarget[0] == 'd')
    {
        extension = ".ds";
        _result = create_shader(buffer, buffer_size, file_name, (SDS*&)result, dx9compatibility);
    }
    else
    {
        NODEFAULT;
    }

    if (disasm)
    {
        ID3DBlob* disasm = 0;
        D3DDisassemble(buffer, buffer_size, FALSE, 0, &disasm);
        if (!disasm)
            return _result;

        string_path dname;
        strconcat(sizeof(dname), dname, "disasm" DELIMITER, file_name, extension);
        IWriter* W = FS.w_open("$app_data_root$", dname);
        W->w(disasm->GetBufferPointer(), disasm->GetBufferSize());
        FS.w_close(W);
        _RELEASE(disasm);
    }

    return _result;
}

class includer : public ID3DInclude
{
public:
    HRESULT __stdcall Open(
        D3D10_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID* ppData, UINT* pBytes)
    {
        string_path pname;
        strconcat(pname, RImplementation.getShaderPath(), pFileName);
        IReader* R = FS.r_open("$game_shaders$", pname);
        if (nullptr == R)
        {
            // possibly in shared directory or somewhere else - open directly
            R = FS.r_open("$game_shaders$", pFileName);
            if (nullptr == R)
                return E_FAIL;
        }

        // duplicate and zero-terminate
        const size_t size = R->length();
        u8* data = xr_alloc<u8>(size + 1);
        CopyMemory(data, R->pointer(), size);
        data[size] = 0;
        FS.r_close(R);

        *ppData = data;
        *pBytes = size;
        return D3D_OK;
    }
    HRESULT __stdcall Close(LPCVOID pData)
    {
        auto mutableData = const_cast<LPVOID>(pData);
        xr_free(mutableData);
        return D3D_OK;
    }
};

class shader_name_holder
{
    size_t pos{};
    string_path name;

public:
    void append(cpcstr string)
    {
        const size_t size = xr_strlen(string);
        for (size_t i = 0; i < size; ++i)
        {
            name[pos] = string[i];
            ++pos;
        }
    }

    void append(u32 value)
    {
        name[pos] = '0' + char(value); // NOLINT
        ++pos;
    }

    void finish()
    {
        name[pos] = '\0';
    }

    pcstr c_str() const { return name; }
};

class shader_options_holder
{
    size_t pos{};
    D3D_SHADER_MACRO m_options[128];

public:
    void add(const xr_vector<D3D_SHADER_MACRO>& macros)
    {
        for (auto macro : macros)
        {
            m_options[pos] = std::move(macro);
            ++pos;
        }
    }

    void add(cpcstr name, cpcstr value)
    {
        m_options[pos] = { name, value };
        ++pos;
    }

    void finish()
    {
        m_options[pos] = { nullptr, nullptr };
    }

    D3D_SHADER_MACRO* data() { return m_options; }
};

// [DA_PORT] Режим прогрева: компилировать в кэш, не создавая объект на устройстве.
//
// Своя переменная на поток, а не общий флаг: прогрев идёт в несколько потоков, и обычная ленивая
// компиляция в это же время не должна перепутать режим.
thread_local bool da_shader_cache_only = false;

// Сколько записей прогрев взял готовыми. Без этого счётчика «обработано 194 за 0.35 с» выглядит как
// скорость компиляции, хотя это скорость проверки наличия файла — разница в шестьдесят раз.
std::atomic<u32> da_shader_warm_hits{0};

// [DA_PORT] Путь исходника текущей компиляции. Ставится в ShaderResourceTraits::CreateShader.
//
// Почему так, а не параметром: shader_compile — переопределение виртуального метода с фиксированной
// сигнатурой, общей для всех рендеров. Менять её ради записи манифеста дороже.
//
// ⚠️ Обычная переменная, НЕ thread_local. Первая версия была `thread_local`, а объявление в месте
// использования — `extern thread_local` ВНУТРИ функции. Так объявлять нельзя; компилятор это принял
// молча, обращение пошло мимо настоящей переменной, и запись 260 байт легла в чужую память —
// движок падал на старте прыжком по мусорному адресу, без единого внятного сообщения.
//
// Переменная потока тут и не нужна: манифест пишется только обычным ленивым путём, из главного
// потока. Прогрев его не трогает — он и так знает, что компилирует.
string_path da_shader_src_path{};

// [DA_PORT] Подменённый список дефайнов БЛЕНДЕРА на время воспроизведения манифеста.
//
// Переменная потока — прогрев идёт в несколько потоков, и общий m_ShaderOptions они бы делили.
// Объявление и определение здесь, в области пространства имён: объявлять thread_local внутри
// функции нельзя, на этом уже наступили (движок падал прыжком по мусорному адресу).
thread_local const xr_vector<D3D_SHADER_MACRO>* da_shader_options_override = nullptr;

namespace
{
// [DA_PORT] Манифест прогрева: что и с какими макросами компилировать.
//
// Записывается ТОЛЬКО то, что движок реально попросил, и в том виде, в каком оно ушло в компилятор.
// Ни имя каталога кэша, ни имя исходника не позволяют восстановить набор дефайнов: скиннинг
// приходит суффиксом `_0.._4` из CResourceManager::_CreateVS, дефайны блендера — из общего списка
// m_ShaderOptions, который выставляется прямо перед вызовом и очищается после. Любая попытка
// вывести их обратно из имени означает повторение логики движка в двух разных соглашениях сразу —
// и молчаливое отравление кэша, как только эта логика где-то поменяется.
//
// Запись же по построению верна: мы не выводим ничего, мы повторяем.
struct warm_entry
{
    xr_string src;    // путь исходника
    xr_string name;   // имя шейдера, каким его просил движок (с суффиксом скиннинга и т.п.)
    xr_string entry;  // точка входа
    xr_string target; // профиль (vs_5_0, ps_5_0, ...)
    xr_string macros; // ТОЛЬКО дефайны блендера: "ИМЯ=ЗНАЧЕНИЕ;..."
    u32 flags{};
};

std::mutex g_warm_lock;
xr_vector<warm_entry> g_warm_list;
xr_unordered_map<xr_string, bool> g_warm_seen;

void da_warm_record(pcstr src, pcstr name, pcstr entry, pcstr target, u32 flags,
    const xr_vector<D3D_SHADER_MACRO>& blender_macros)
{
    if (!src || !src[0] || !name || !name[0])
        return;

    // Записываем ТОЛЬКО дефайны блендера (скиннинг, детали, полусфера, тесселяция). Настроечные не
    // пишем сознательно: они выводятся из настроек при воспроизведении, и без этого манифест был бы
    // привязан к настройкам того, кто его снял, — то есть бесполезен ровно для того игрока, ради
    // которого прогрев и делается.
    xr_string m;
    for (const auto& macro : blender_macros)
    {
        if (!macro.Name)
            continue;
        m += macro.Name;
        m += "=";
        m += macro.Definition ? macro.Definition : "";
        m += ";";
    }

    xr_string key(name);
    key += "|";
    key += target;
    key += "|";
    key += m;

    std::lock_guard<std::mutex> lock(g_warm_lock);
    if (!g_warm_seen.emplace(key, true).second)
        return;

    warm_entry e;
    e.src = src;
    e.name = name;
    e.entry = entry;
    e.target = target;
    e.macros = std::move(m);
    e.flags = flags;
    g_warm_list.emplace_back(std::move(e));
}
} // namespace

HRESULT CRender::shader_compile(pcstr name, IReader* fs, pcstr pFunctionName,
    pcstr pTarget, u32 Flags, void*& result)
{
    shader_options_holder options;
    shader_name_holder sh_name;

    // Don't move these variables to lower scope!
    string32 c_smap;
    string32 c_gloss;
    string32 c_sun_shafts;
    string32 c_ssao;
    string32 c_sun_quality;
    string32 c_water_reflection;
    char c_msaa_samples[2];
    char c_msaa_current_sample[2];

    // options:
    const auto appendShaderOption = [&](u32 option, cpcstr macro, cpcstr value)
    {
        if (option)
            options.add(macro, value);

        sh_name.append(option);
    };

    // External defines
    // [DA_PORT] При воспроизведении манифеста берём записанные дефайны блендера, иначе текущие.
    // Настроечные дефайны ниже выводятся из НЫНЕШНИХ настроек в обоих случаях — потому манифест и
    // переносим между машинами.
    options.add(da_shader_options_override ? *da_shader_options_override : m_ShaderOptions);

    // Shadow map size
    {
        xr_itoa(m_SMAPSize, c_smap, 10);
        options.add("SMAP_size", c_smap);
        sh_name.append(c_smap);
    }

    // FP16 Filter
    appendShaderOption(o.fp16_filter, "FP16_FILTER", "1");

    // FP16 Blend
    appendShaderOption(o.fp16_blend, "FP16_BLEND", "1");

    // HW smap
    appendShaderOption(o.HW_smap, "USE_HWSMAP", "1");

    // HW smap PCF
    appendShaderOption(o.HW_smap_PCF, "USE_HWSMAP_PCF", "1");

    // Fetch4
    appendShaderOption(o.HW_smap_FETCH4, "USE_FETCH4", "1");

    // SJitter
    appendShaderOption(o.sjitter, "USE_SJITTER", "1");

    // Branching
    appendShaderOption(HW.Caps.raster_major >= 3, "USE_BRANCHING", "1");

    // Vertex texture fetch
    appendShaderOption(HW.Caps.geometry.bVTF, "USE_VTF", "1");

    // Tshadows
    appendShaderOption(o.Tshadows, "USE_TSHADOWS", "1");

    // Motion blur
    appendShaderOption(o.mblur, "USE_MBLUR", "1");

    // Sun filter
    appendShaderOption(o.sunfilter, "USE_SUNFILTER", "1");

    // Static sun on R2 and higher
    appendShaderOption(o.sunstatic, "USE_R2_STATIC_SUN", "1");

    // Force gloss
    {
        xr_sprintf(c_gloss, "%f", o.forcegloss_v);
        appendShaderOption(o.forcegloss, "FORCE_GLOSS", c_gloss);
    }

    // Force skinw
    appendShaderOption(o.forceskinw, "SKIN_COLOR", "1");

    // SSAO Blur
    appendShaderOption(o.ssao_blur_on, "USE_SSAO_BLUR", "1");

    // SSAO HDAO
    if (o.ssao_hdao)
    {
        options.add("HDAO", "1");
        sh_name.append(static_cast<u32>(1)); // HDAO on
        sh_name.append(static_cast<u32>(0)); // HBAO off
        sh_name.append(static_cast<u32>(0)); // Half data off
    }
    else // SSAO HBAO
    {
        sh_name.append(static_cast<u32>(0)); // HDAO off
        sh_name.append(o.ssao_hbao);         // HBAO on/off
        sh_name.append(o.ssao_half_data);    // Half data on/off

        if (o.ssao_hbao)
        {
            if (o.ssao_half_data)
                options.add("SSAO_OPT_DATA", "2");
            else
                options.add("SSAO_OPT_DATA", "1");

            if (o.hbao_vectorized)
                options.add("VECTORIZED_CODE", "1");

            options.add("USE_HBAO", "1");
        }
    }

    // skinning
    // SKIN_NONE
    appendShaderOption(m_skinning < 0, "SKIN_NONE", "1");

    // SKIN_0
    appendShaderOption(0 == m_skinning, "SKIN_0", "1");

    // SKIN_1
    appendShaderOption(1 == m_skinning, "SKIN_1", "1");

    // SKIN_2
    appendShaderOption(2 == m_skinning, "SKIN_2", "1");

    // SKIN_3
    appendShaderOption(3 == m_skinning, "SKIN_3", "1");

    // SKIN_4
    appendShaderOption(4 == m_skinning, "SKIN_4", "1");

    //	Igor: need restart options
    // Soft water
    {
        const bool softWater = RImplementation.o.advancedpp && ps_r2_ls_flags.test(R2FLAG_SOFT_WATER);
        appendShaderOption(softWater, "USE_SOFT_WATER", "1");
    }

    // Soft particles
    {
        const bool useSoftParticles = RImplementation.o.advancedpp && ps_r2_ls_flags.test(R2FLAG_SOFT_PARTICLES);
        appendShaderOption(useSoftParticles, "USE_SOFT_PARTICLES", "1");
    }

    // Depth of field
    {
        const bool dof = RImplementation.o.advancedpp && ps_r2_ls_flags.test(R2FLAG_DOF);
        appendShaderOption(dof, "USE_DOF", "1");
    }

    // Sun shafts
    if (RImplementation.o.advancedpp && ps_r_sun_shafts)
    {
        xr_sprintf(c_sun_shafts, "%d", ps_r_sun_shafts);
        options.add("SUN_SHAFTS_QUALITY", c_sun_shafts);
        sh_name.append(ps_r_sun_shafts);
    }
    else
        sh_name.append(static_cast<u32>(0));

    if (RImplementation.o.advancedpp && ps_r_ssao)
    {
        xr_sprintf(c_ssao, "%d", ps_r_ssao);
        options.add("SSAO_QUALITY", c_ssao);
        sh_name.append(ps_r_ssao);
    }
    else
        sh_name.append(static_cast<u32>(0));

    // Sun quality
    if (RImplementation.o.advancedpp && ps_r_sun_quality)
    {
        xr_sprintf(c_sun_quality, "%d", ps_r_sun_quality);
        options.add("SUN_QUALITY", c_sun_quality);
        sh_name.append(ps_r_sun_quality);
    }
    else
        sh_name.append(static_cast<u32>(0));

    // Steep parallax
    {
        const bool steepParallax = RImplementation.o.advancedpp && ps_r2_ls_flags.test(R2FLAG_STEEP_PARALLAX);
        appendShaderOption(steepParallax, "ALLOW_STEEPPARALLAX", "1");
    }

    // Geometry buffer optimization
    appendShaderOption(o.gbuffer_opt, "GBUFFER_OPTIMIZATION", "1");

    // [DA_PORT] Motion vectors as an extra G-buffer output — see f_deffer in common_iostructs.h. Must
    // match the target count bound in phase_scene_begin, hence both read the same latched o.velocity.
    appendShaderOption(o.velocity, "DA_VELOCITY", "1");
    appendShaderOption(o.velocity_debug_ids, "DA_DEBUG_SHADER_IDS", "1"); // [DA_PORT] see r2.cpp

    // Shader Model 4.1
    appendShaderOption(o.dx11_sm4_1, "SM_4_1", "1");

    // Shader Model 5.0
    appendShaderOption(HW.FeatureLevel >= D3D_FEATURE_LEVEL_11_0, "SM_5", "1");

    // Double precision
    appendShaderOption(HW.DoublePrecisionFloatShaderOps, "DOUBLE_PRECISION", "1");

    // Extended doubles instructions
    appendShaderOption(HW.ExtendedDoublesShaderInstructions, "EXTENDED_DOUBLES", "1");

    // SAD4 intrinsic support
    appendShaderOption(HW.SAD4ShaderInstructions, "SAD4_SUPPORTED", "1");

    // Minmax SM
    appendShaderOption(o.minmax_sm, "USE_MINMAX_SM", "1");

    // [DA_PORT] Enable Dead Air's visor rain-droplet ("lens water") effect that lives in the
    // post-combine shader (combine_1.ps: `#ifdef USE_LENS_WATER -> final = visor_drops(...)`). Stock R4
    // never defined this macro, so the whole block was compiled out — the actor's "clean mask" wipe
    // animation (dinamic_hud.script) then had nothing to wipe (drops never appeared on the visor).
    // Compiled in unconditionally; the effect is driven at runtime by the `lenswater` shader constant
    // (bound to ps_r2_lenswater_val in r4_rendertarget_phase_combine.cpp), which dinamic_hud ramps up
    // while it rains. lenswater == 0 makes visor_drops a no-op, so it costs nothing when dry.
    options.add("USE_LENS_WATER", "1");

    // [DA_PORT] Enable Dead Air's OWN screen-space reflections on water. DA's archive water.ps/waterd.ps
    // (shaders\r3, self-contained OGSE pack) already implement full SSLR: calc_reflections()->get_reflection()
    // ray-marches s_image (scene color) along the reflected ray, plus calc_moon_road(). The whole block is
    // gated by `#ifdef USE_REFLECTIONS`, which the R2 engine defined but OpenXRay R4 never did -> only the
    // cubemap env reflection was active ("sky reflects but nothing else"). USE_REFLECTIONS appears ONLY in
    // water.ps/waterd.ps (4x total), so defining it globally is safe. All deps (REFL_RANGE, get_depth_fast,
    // eye_direction, screen_res, is_sky) exist in the archive common.h/ogse_reflections.h. If reflections come
    // out black, s_image is not bound in the R4 water pass (bind it there) — the shader still compiles.
    // Driven by the existing "r3_water_refl" console setting (off/low/medium/high/ultra, default high),
    // the same knob the GL renderer already uses — so weaker machines can dial the ray-march down or
    // turn reflections off entirely. SSR_QUALITY picks the march length / range in ogse_reflections.h.
    // Off also drops USE_REFLECTIONS, so the water falls back to the plain cubemap and costs nothing.
    if (ps_r_water_reflection)
    {
        options.add("USE_REFLECTIONS", "1");
        xr_sprintf(c_water_reflection, "%d", ps_r_water_reflection);
        options.add("SSR_QUALITY", c_water_reflection);
        sh_name.append(ps_r_water_reflection);

        // "r3_water_refl_jitter": dithers each pixel's ray start along the march so the stair-stepping
        // of a fixed-stride trace breaks into fine noise instead of visible bands on the reflection.
        appendShaderOption(ps_r2_ls_flags_ext.test(R3FLAGEXT_SSR_JITTER), "SSR_JITTER", "1");

        // NOTE: "r3_water_refl_half_depth" is deliberately NOT wired up here. It would need rt_half_depth,
        // which only exists when o.ssao_opt_data is set — and R2FLAGEXT_SSAO_OPT_DATA is commented out of
        // the default flag mask (xrRender_console.cpp), so neither the RT nor phase_downsamp() ever runs.
        // Sampling it would hand the ray-march an empty buffer. Supporting it means giving SSR its own
        // downsample pass; until then the setting simply has no effect on R4.
    }
    else
    {
        // keep the compiled-shader name length stable
        sh_name.append(static_cast<u32>(0)); // quality
        sh_name.append(static_cast<u32>(0)); // jitter
    }

    // [DA_PORT] Do NOT inject H_*/L_*/PIXEL_SIZE/eye_direction macros here!
    // Dead Air's shader pack lives INSIDE database\configs.xdb0 (shaders\r2, shaders\r3) and is
    // self-contained: its own common.h defines H_MAIN=7.0 (H_TERR/H_GRASS=3.5, H_MODELS/H_BUSHES=1.4),
    // L_RANGE=1.0, L_BRIGHT=1.0, PIXEL_SIZE; shared\common.h declares 'uniform half3 eye_direction'
    // (bound by Blender_Recorder_StandartBinding.cpp r_Constant("eye_direction")).
    // The historical "undeclared identifier L_RANGE/eye_direction" compile errors were caused by
    // loose STOCK OpenXRay override files in gamedata\shaders\r2|r3 SHADOWING the archive versions
    // (X-Ray VFS: loose files override archives). Those stock files are quarantined to
    // "shaders_loose_stock_backup\" in the game root. Injecting zeroed macros here instead
    // (H_TERR=0 etc.) kills hemisphere lighting -> "world renders but far too dark".

    // Be carefull!!!!! this should be at the end to correctly generate
    // compiled shader name;
    // add a #define for DX10_1 MSAA support
    if (o.msaa)
    {
        appendShaderOption(o.msaa, "USE_MSAA", "1");

        // Number of samples
        {
            c_msaa_samples[0] = char(o.msaa_samples) + '0';
            c_msaa_samples[1] = 0;
            appendShaderOption(o.msaa_samples, "MSAA_SAMPLES", c_msaa_samples);
        }
        // Current sample
        {
            if (m_MSAASample < 0 || o.msaa_opt)
                c_msaa_current_sample[0] = '0';
            else
                c_msaa_current_sample[0] = '0' + char(m_MSAASample);
            c_msaa_current_sample[1] = 0;

            appendShaderOption(m_MSAASample >= 0 ? m_MSAASample : 0,
                "ISAMPLE", c_msaa_current_sample);
        }

        appendShaderOption(o.msaa_opt, "MSAA_OPTIMIZATION", "1");

        switch (o.msaa_alphatest)
        {
        case MSAA_ATEST_DX10_0_ATOC:
            options.add("MSAA_ALPHATEST_DX10_0_ATOC", "1");

            sh_name.append(static_cast<u32>(1)); // DX10_0_ATOC   on
            sh_name.append(static_cast<u32>(0)); // DX10_1_ATOC   off
            sh_name.append(static_cast<u32>(0)); // DX10_1_NATIVE off
            break;
        case MSAA_ATEST_DX10_1_ATOC:
            options.add("MSAA_ALPHATEST_DX10_1_ATOC", "1");

            sh_name.append(static_cast<u32>(0)); // DX10_0_ATOC   off
            sh_name.append(static_cast<u32>(1)); // DX10_1_ATOC   on
            sh_name.append(static_cast<u32>(0)); // DX10_1_NATIVE off
            break;
        case MSAA_ATEST_DX10_1_NATIVE:
            options.add("MSAA_ALPHATEST_DX10_1", "1");

            sh_name.append(static_cast<u32>(0)); // DX10_0_ATOC   off
            sh_name.append(static_cast<u32>(0)); // DX10_1_ATOC   off
            sh_name.append(static_cast<u32>(1)); // DX10_1_NATIVE on
            break;
        default:
            sh_name.append(static_cast<u32>(0)); // DX10_0_ATOC   off
            sh_name.append(static_cast<u32>(0)); // DX10_1_ATOC   off
            sh_name.append(static_cast<u32>(0)); // DX10_1_NATIVE off
        }
    }
    else
    {
        sh_name.append(static_cast<u32>(0)); // MSAA off
        sh_name.append(static_cast<u32>(0)); // No MSAA samples
        sh_name.append(static_cast<u32>(0)); // No MSAA optimization
        sh_name.append(static_cast<u32>(0)); // DX10_0_ATOC   off
        sh_name.append(static_cast<u32>(0)); // DX10_1_ATOC   off
        sh_name.append(static_cast<u32>(0)); // DX10_1_NATIVE off
    }

    // finish
    options.finish();
    sh_name.finish();

    HRESULT _result = E_FAIL;

    char extension[3];
    strncpy_s(extension, pTarget, 2);

    pcstr renderer;
    if (HW.FeatureLevel >= D3D_FEATURE_LEVEL_11_0)
        renderer = "r4" DELIMITER;
    else if (HW.FeatureLevel >= D3D_FEATURE_LEVEL_10_0)
        renderer = "r3" DELIMITER;
    else
    {
        renderer = "r4_level9" DELIMITER;
        R_ASSERT(!"Feature levels lower than 10.0 are unsupported");
    }

    string_path filename;
    strconcat(sizeof(filename), filename, renderer, name, ".", extension);

    // [DA_PORT] Путь записи внутри кэша — один и тот же и для личного кэша игрока, и для
    // поставляемого с игрой. Отличается только корень.
    string_path cache_rel;
    strconcat(sizeof(cache_rel), cache_rel, "shaders_cache_oxr" DELIMITER, filename, DELIMITER, sh_name.c_str());
    strconcat(sizeof(filename), filename, filename, DELIMITER, sh_name.c_str());

    string_path file_name;
    FS.update_path(file_name, "$app_data_root$", cache_rel);

    string_path shadersFolder;
    FS.update_path(shadersFolder, "$game_shaders$", RImplementation.getShaderPath());

    u32 fileCrc = 0;
    getFileCrc32(fs, shadersFolder, fileCrc);
    fs->seek(0);

    // [DA_PORT] Достать шейдер из готовой записи кэша. Вынесено в лямбду, потому что мест теперь два.
    const auto try_cached = [&](pcstr path) -> bool
    {
        if (!FS.exist(path))
            return false;

        bool ok = false;
        IReader* file = FS.r_open(path);
        if (file->length() > 4)
        {
            const bool dx9compatibility = file->r_u32();

            const u32 savedFileCrc = file->r_u32();
            if (savedFileCrc == fileCrc)
            {
                const u32 savedBytecodeCrc = file->r_u32();
                const u32 bytecodeCrc = crc32(file->pointer(), file->elapsed());
                if (bytecodeCrc == savedBytecodeCrc)
                {
#ifdef DEBUG
                    Log("* Loading shader:", path);
#endif
                    _result = create_shader(pTarget, (DWORD*)file->pointer(), file->elapsed(), filename, result,
                        o.disasm, dx9compatibility);
                    ok = SUCCEEDED(_result);
                }
            }
        }
        file->close();
        return ok;
    };

    // [DA_PORT] Сначала личный кэш игрока, потом поставляемый с игрой.
    //
    // Зачем поставляемый. Компиляция идёт лениво, по первому обращению материала, со скоростью около
    // десяти шейдеров в секунду в один поток (замерено). Полный набор мода — 908 штук, то есть
    // примерно полторы минуты, размазанные по первому запуску и первым выходам на новые локации
    // кусками по несколько секунд. Байткод DXBC от видеокарты не зависит — драйвер переводит его в
    // свои команды сам, — поэтому готовый кэш одинаков у всех и его можно просто положить в пакет.
    //
    // Личный кэш проверяется первым: если игрок менял настройки или мы выпустили патч по шейдерам,
    // у него уже лежит своя, более свежая запись.
    //
    // Поставляемый кэш только ЧИТАЕТСЯ. Дописывать в gamedata нельзя: он может лежать в архиве, быть
    // только для чтения, да и смешивать своё с чужим в одной папке — потом не разберёшь, чьё
    // устарело. Промах здесь просто ведёт к обычной компиляции с записью в личный кэш.
    //
    // Совпадение по контрольным суммам обязательно и здесь: набор дефайнов зашит в имя записи, а
    // исходник с include-ами проверяется по CRC. Чужой или устаревший кэш будет молча отвергнут.
    // [DA_PORT] В режиме прогрева объект шейдера не нужен — достаточно, чтобы запись легла в кэш.
    // Поэтому проверяем только НАЛИЧИЕ годной записи, не создавая ничего на устройстве.
    if (da_shader_cache_only)
    {
        string_path packed_name;
        FS.update_path(packed_name, "$game_data$", cache_rel);
        if (FS.exist(file_name) || FS.exist(packed_name))
        {
            ++da_shader_warm_hits;
            return S_OK;
        }
    }
    else if (!try_cached(file_name))
    {
        string_path packed_name;
        FS.update_path(packed_name, "$game_data$", cache_rel);
        try_cached(packed_name);
    }

    // [DA_PORT] Запись в манифест — здесь, а не в ветке компиляции: нам нужен ПОЛНЫЙ перечень того,
    // что игре понадобилось, включая взятое из кэша готовым. Иначе манифест, снятый на прогретом
    // кэше, окажется пустым и прогрев у игрока не сделает ничего.
    if (!da_shader_cache_only)
        da_warm_record(da_shader_src_path, name, pFunctionName, pTarget, Flags,
            da_shader_options_override ? *da_shader_options_override : m_ShaderOptions);

    if (FAILED(_result))
    {
        includer Includer;
        LPD3DBLOB pShaderBuf = NULL;
        LPD3DBLOB pErrorBuf = NULL;
        _result = HW.D3DCompile(fs->pointer(), fs->length(), "", options.data(),
            &Includer, pFunctionName, pTarget, Flags, 0, &pShaderBuf, &pErrorBuf);

        if (FAILED(_result) && pErrorBuf)
        {
            cpcstr str = static_cast<cpcstr>(pErrorBuf->GetBufferPointer());
            if (strstr(str, "error X3523")) // is there a better way?
            {
                // [DA_PORT] Было `pErrorBuf = nullptr` — указатель просто терялся, а сам буфер
                // оставался жив. Плюс вторая компиляция ниже пишет в pShaderBuf поверх первого,
                // теряя и его.
                _RELEASE(pShaderBuf);
                _RELEASE(pErrorBuf);
                Flags |= D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY;
                _result = HW.D3DCompile(fs->pointer(), fs->length(), "", options.data(),
                    &Includer, pFunctionName, pTarget, Flags, 0, &pShaderBuf, &pErrorBuf);
            }
        }

        if (SUCCEEDED(_result))
        {
            const bool dx9compatibility = Flags & D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY;

            // [DA_PORT] Запись кэша — под общим замком, потому что прогрев компилирует в несколько
            // потоков. Сама компиляция параллелится свободно (D3DCompile потокобезопасен), а вот
            // w_open/w_close правят ОБЩИЙ реестр файлов CLocatorAPI, и он к параллельной записи не
            // готов. Замок стоит только здесь: запись — доли процента времени против компиляции,
            // поэтому выигрыш от параллельности он не съедает.
            static std::mutex s_cache_write;
            std::lock_guard<std::mutex> cache_write_guard(s_cache_write);

            IWriter* file = FS.w_open(file_name);

            file->w_u32(dx9compatibility);
            file->w_u32(fileCrc);

            u32 bytecodeCrc = crc32(pShaderBuf->GetBufferPointer(), pShaderBuf->GetBufferSize());
            file->w_u32(bytecodeCrc); // Do not write anything below this line, take a look at reading (crc32)

            file->w(pShaderBuf->GetBufferPointer(), pShaderBuf->GetBufferSize());
            FS.w_close(file);
#ifdef DEBUG
            Log("- Compile shader:", file_name);
#endif
            // [DA_PORT] В прогреве останавливаемся здесь: запись в кэш легла, а объект на устройстве
            // создаст обычный ленивый путь, когда шейдер реально понадобится.
            if (!da_shader_cache_only)
                _result = create_shader(pTarget, (DWORD*)pShaderBuf->GetBufferPointer(),
                    pShaderBuf->GetBufferSize(), filename, result, o.disasm, dx9compatibility);
        }
        else
        {
            Log("! ", file_name);
            if (pErrorBuf)
                Log("! error: ", (LPCSTR)pErrorBuf->GetBufferPointer());
            else
                Msg("Can't compile shader hr=0x%08x", _result);
        }

        // [DA_PORT] Оба блоба D3D не освобождались ВООБЩЕ — ни при удаче, ни при ошибке. Течёт по
        // разу на каждый компилируемый шейдер, то есть на холодном кэше — на все несколько сотен
        // сразу. Правка того же класса, что и представления текстур: ресурс COM, счётчик ссылок,
        // и в наших кучах этого не видно.
        _RELEASE(pShaderBuf);
        _RELEASE(pErrorBuf);
    }

    return _result;
}

// [DA_PORT] Параллельный прогрев кэша шейдеров.
//
// Зачем, если кэш и так поставляется с игрой. Поставляемый кэш подходит только пока набор дефайнов
// совпадает с тем, при котором мы его собрали, а набор зависит от настроек графики: качество
// солнца, вид затенения, лучи, отражения воды, MSAA, размер теневой карты. Игрок сдвинул один
// ползунок — и промахивается ВЕСЬ кэш разом, все 908 записей. Тогда ленивая компиляция по одному в
// главном потоке даёт около полутора минут, размазанных по игре кусками (замерено: ~10 шейдеров в
// секунду). Прогрев в несколько потоков собирает то же самое за десяток секунд и один раз.
//
// Откуда берётся список. Из структуры поставляемого кэша: каталоги в нём названы `<имя>.<тип>` и
// перечисляют ровно те шейдеры, которые мод действительно просит. Отдельный файл-манифест не нужен,
// и он не может разойтись с содержимым — список и есть содержимое.
//
// Что делает: только заполняет кэш. Объекты на устройстве создаст обычный ленивый путь, когда
// шейдер понадобится, — и найдёт готовую запись.
// [DA_PORT] Сохранить манифест прогрева. Кладётся рядом с личным кэшем; в релизный пакет кладём его
// же, снятый на наших настройках, — тогда у игрока прогрев есть с первого запуска.
void CRender::da_shader_manifest_save()
{
    std::lock_guard<std::mutex> lock(g_warm_lock);
    if (g_warm_list.empty())
    {
        Msg("! [DA] манифест шейдеров пуст - записывать нечего");
        return;
    }

    string_path path;
    FS.update_path(path, "$app_data_root$", "shaders_cache_oxr" DELIMITER "warmup.list");

    // [DA_PORT] Сливаем с тем, что уже лежит, а не затираем.
    //
    // Манифест копится за сессию, а одна сессия видит один-два уровня. Полное покрытие набирается
    // только обходом карты, то есть несколькими запусками — и без слияния каждый следующий стирал бы
    // предыдущий. Ключ тот же, что при записи: имя + профиль + дефайны блендера.
    u32 merged = 0;
    if (FS.exist(path))
    {
        IReader* old = FS.r_open(path);
        if (old)
        {
            string4096 line;
            while (!old->eof())
            {
                old->r_string(line, sizeof(line));
                xr_string s(line);

                xr_string f[6];
                size_t p0 = 0;
                bool ok = true;
                for (int i = 0; i < 5; ++i)
                {
                    const size_t p1 = s.find('|', p0);
                    if (p1 == xr_string::npos) { ok = false; break; }
                    f[i] = s.substr(p0, p1 - p0);
                    p0 = p1 + 1;
                }
                if (!ok)
                    continue;
                f[5] = s.substr(p0);

                xr_string key = f[1] + "|" + f[3] + "|" + f[5];
                if (!g_warm_seen.emplace(key, true).second)
                    continue;

                warm_entry e;
                e.src = f[0];
                e.name = f[1];
                e.entry = f[2];
                e.target = f[3];
                e.flags = u32(atoi(f[4].c_str()));
                e.macros = f[5];
                g_warm_list.emplace_back(std::move(e));
                ++merged;
            }
            FS.r_close(old);
        }
    }

    IWriter* w = FS.w_open(path);
    if (!w)
    {
        Msg("! [DA] манифест шейдеров: не удалось создать %s", path);
        return;
    }

    for (const auto& e : g_warm_list)
    {
        string4096 line;
        xr_sprintf(line, sizeof(line), "%s|%s|%s|%s|%u|%s\r\n", e.src.c_str(), e.name.c_str(), e.entry.c_str(),
            e.target.c_str(), e.flags, e.macros.c_str());
        w->w(line, xr_strlen(line));
    }
    FS.w_close(w);

    Msg("* [DA] манифест шейдеров сохранён: %u записей (из них %u подхвачено из прежнего) -> %s",
        u32(g_warm_list.size()), merged, path);
}

void CRender::da_shader_warmup(bool serial)
{
    // Манифест: сначала личный (он свежее), потом поставляемый с игрой.
    string_path path;
    FS.update_path(path, "$app_data_root$", "shaders_cache_oxr" DELIMITER "warmup.list");
    if (!FS.exist(path))
        FS.update_path(path, "$game_data$", "shaders_cache_oxr" DELIMITER "warmup.list");

    IReader* manifest = FS.exist(path) ? FS.r_open(path) : nullptr;
    if (!manifest)
    {
        Msg("* [DA] прогрев шейдеров: манифеста нет (%s), пропускаю", path);
        return;
    }

    struct job
    {
        xr_string src, name, entry, target;
        xr_vector<xr_string> macro_storage; // строки должны пережить вызов компилятора
        u32 flags{};
    };
    xr_vector<job> jobs;

    string4096 line;
    while (!manifest->eof())
    {
        manifest->r_string(line, sizeof(line));
        xr_string str(line);
        if (str.size() < 8)
            continue;

        // src|name|entry|target|flags|macros
        xr_string f[6];
        size_t p0 = 0;
        bool ok = true;
        for (int i = 0; i < 5; ++i)
        {
            const size_t p1 = str.find('|', p0);
            if (p1 == xr_string::npos) { ok = false; break; }
            f[i] = str.substr(p0, p1 - p0);
            p0 = p1 + 1;
        }
        if (!ok)
            continue;
        f[5] = str.substr(p0);

        job j;
        j.src = f[0];
        j.name = f[1];
        j.entry = f[2];
        j.target = f[3];
        j.flags = u32(atoi(f[4].c_str()));

        size_t q0 = 0;
        while (q0 < f[5].size())
        {
            const size_t semi = f[5].find(';', q0);
            if (semi == xr_string::npos)
                break;
            const xr_string pair = f[5].substr(q0, semi - q0);
            q0 = semi + 1;
            const size_t eq = pair.find('=');
            if (eq == xr_string::npos)
                continue;
            j.macro_storage.push_back(pair.substr(0, eq));
            j.macro_storage.push_back(pair.substr(eq + 1));
        }

        jobs.push_back(std::move(j));
    }
    FS.r_close(manifest);

    if (jobs.empty())
    {
        Msg("* [DA] прогрев шейдеров: манифест пуст");
        return;
    }

    CTimer timer;
    timer.Start();
    da_shader_warm_hits = 0;
    std::atomic<u32> done{0}, failed{0};

    // [DA_PORT] Вся работа — через обычную shader_compile в режиме «только в кэш». Своей записи
    // файла здесь намеренно НЕТ: путь внутри кэша, контрольные суммы и формат должны считаться в
    // одном месте, иначе прогрев и ленивый путь однажды разойдутся, и разойдутся молча.
    const auto body = [&](const TaskRange<size_t>& range)
    {
        for (size_t i = range.begin(); i != range.end(); ++i)
        {
            job& j = jobs[i];

            IReader* src = FS.r_open(j.src.c_str());
            if (!src)
            {
                ++failed;
                continue;
            }

            xr_vector<D3D_SHADER_MACRO> macros;
            for (size_t k = 0; k + 1 < j.macro_storage.size(); k += 2)
                macros.push_back({j.macro_storage[k].c_str(), j.macro_storage[k + 1].c_str()});

            da_shader_options_override = &macros;
            da_shader_cache_only = true;

            void* dummy = nullptr;
            if (SUCCEEDED(shader_compile(j.name.c_str(), src, j.entry.c_str(), j.target.c_str(), j.flags, dummy)))
                ++done;
            else
                ++failed;

            da_shader_cache_only = false;
            da_shader_options_override = nullptr;

            FS.r_close(src);
        }
    };

    if (serial)
        body(TaskRange<size_t>(0, jobs.size()));
    else
        xr_parallel_for(TaskRange<size_t>(0, jobs.size()), body);

    const u32 hits = da_shader_warm_hits.load();
    Msg("* [DA] прогрев шейдеров%s: записей %u, скомпилировано %u, взято готовыми %u, не вышло %u за %.2f с",
        serial ? " (один поток)" : "", u32(jobs.size()), done.load() > hits ? done.load() - hits : 0u, hits,
        failed.load(), timer.GetElapsed_sec());
}
} // namespace xray::render::RENDER_NAMESPACE
