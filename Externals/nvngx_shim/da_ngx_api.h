#pragma once

// [DA_PORT] Плоский C-интерфейс между рендером (MinGW) и NVIDIA NGX (MSVC).
//
// ЗАЧЕМ ЭТОТ ФАЙЛ ВООБЩЕ СУЩЕСТВУЕТ
//
// NVIDIA отдаёт NGX статическими библиотеками, собранными MSVC. MinGW их не линкует: замерено
// 77-123 неразрешённых символа на каждом варианте тулсета (x64, vs2013, vs2012, vs2010), и это не
// мелочь, а реализации MSVC-STL (std::vector, std::basic_string), структуры RTTI, security-cookie и
// _Init_thread_epoch из vcruntime140. Подменить их libstdc++ нельзя - ABI другой. Перебирать
// варианты библиотеки бесполезно, все перебраны.
//
// Поэтому NGX живёт в отдельной маленькой библиотеке da_ngx.dll, собранной MSVC, а наружу торчит
// только то, что переживает границу компиляторов: целые числа, float, указатели. Никакого STL, ни
// одного класса, ни исключений. Это самый обычный способ сшивать разные компиляторы, а не хак.
//
// ПРАВИЛА, КОТОРЫЕ НЕЛЬЗЯ НАРУШАТЬ ПРИ ПРАВКЕ
//
//   1. Только POD. Появится в сигнатуре std::string или класс с виртуальными методами - получим
//      падение в чужой памяти, которое не отладить: обе стороны будут правы по-своему.
//   2. Меняешь сигнатуру - увеличивай DA_NGX_ABI_VERSION. Рендер сверяет её при загрузке и
//      отказывается работать со старой библиотекой вместо того, чтобы упасть.
//   3. Ресурсы передаются как void*, чтобы этот заголовок не тянул за собой d3d11.h.

#define DA_NGX_ABI_VERSION 1

// Экспорт нужен только при сборке самой прослойки. Рендер берёт функции через GetProcAddress, чтобы
// отсутствие da_ngx.dll не мешало игре запуститься: без DLSS она прекрасно живёт, а при линковке на
// этапе загрузки пропавший файл убил бы весь модуль рендера.
#ifdef DA_NGX_BUILDING
#define DA_NGX_API __declspec(dllexport)
#else
#define DA_NGX_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Куда прослойка пишет сообщения. Строка живёт только на время вызова - копировать, если нужна.
typedef void (*da_ngx_log_fn)(const char* message);

// Возвращает DA_NGX_ABI_VERSION той сборки, из которой взята библиотека.
DA_NGX_API int da_ngx_abi_version(void);

DA_NGX_API void da_ngx_set_log(da_ngx_log_fn fn);

// data_dir - АБСОЛЮТНЫЙ путь к каталогу, где лежит nvngx_dlss.dll.
// Относительный NGX не принимает: он отвечает "файл отсутствует или повреждён" при живом и целом
// файле. Это стоило отдельного захода, поэтому сказано здесь.
// Возвращает 1 при успехе.
DA_NGX_API int da_ngx_init(void* d3d11_device, const wchar_t* data_dir);
DA_NGX_API void da_ngx_shutdown(void* d3d11_device);

// 1, если драйвер и видеокарта поддерживают DLSS. Осмысленно только после da_ngx_init.
DA_NGX_API int da_ngx_available(void);

// Разрешение рендера, которое NGX считает оптимальным для этого режима качества.
// Спрашивается у самой библиотеки, а не берётся из таблицы коэффициентов: NVIDIA меняет их между
// версиями, и зашитые числа разъедутся молча.
DA_NGX_API int da_ngx_optimal_size(int quality, unsigned display_w, unsigned display_h, unsigned* out_render_w,
                        unsigned* out_render_h);

DA_NGX_API int da_ngx_create(void* d3d11_context, unsigned render_w, unsigned render_h, unsigned display_w,
                  unsigned display_h, int quality);
DA_NGX_API void da_ngx_destroy(void* d3d11_context);

// Дрожание - в ПИКСЕЛЯХ разрешения рендера, ровно как его выдаёт CCameraManager.
// mv_scale - перевод наших векторов из NDC в пиксели; см. da_dlss.cpp, там же и про знак.
DA_NGX_API int da_ngx_evaluate(void* d3d11_context, void* colour, void* depth, void* velocity, void* output,
                    void* reactive, float jitter_x, float jitter_y, float mv_scale_x,
                    float mv_scale_y, int reset, unsigned render_w, unsigned render_h);

#ifdef __cplusplus
}
#endif
