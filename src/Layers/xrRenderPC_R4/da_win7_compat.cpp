#include "stdafx.h"

// [DA_PORT] Совместимость с Windows 7: убираем импорт CreateFile2.
//
// Симптом: «Точка входа в процедуру CreateFile2 не найдена в библиотеке DLL Kernel32.dll»,
// игра не запускается вовсе. CreateFile2 появилась в Windows 8, на Windows 7 её нет, а Windows
// разрешает ВСЕ импорты при загрузке модуля — независимо от того, вызываются они или нет.
//
// Откуда берётся: DirectXTex приходит готовой статической библиотекой из MSYS2
// (mingw64/lib/libDirectXTex.a), собранной под Win8+. Мы её файловый API не вызываем НИ РАЗУ —
// текстуры читаются через память (LoadFromDDSMemory/SaveToDDSMemory), потому что файлы лежат в
// архивах и идут через ФС движка. Но линковщик тянет объектный файл целиком, а в одном
// DirectXTexDDS.cpp.obj лежат и memory-, и file-варианты. Значит игра падала из-за кода,
// который никогда бы не выполнился. Пересобрать саму библиотеку нельзя — она уже скомпилирована.
//
// Как работает правка: обращение в DirectXTex скомпилировано как `call *__imp_CreateFile2`, то
// есть косвенно через запись таблицы импорта. Определяя `__imp_CreateFile2` сами, мы даём
// линковщику готовый символ, и он не вытягивает соответствующий член архива libkernel32.a —
// импорт из таблицы исчезает, а вызов уходит на нашу реализацию.
//
// Реализация не заглушка, а честный переходник на CreateFileW (есть во всех версиях Windows):
// код мёртвый, но оставлять мину «вернёт ошибку, если однажды позовут» не хочется.
//
// ⚠️ Если линковщик когда-нибудь скажет «duplicate symbol __imp_CreateFile2» — значит
// libkernel32.a всё же вытянули по другой причине, и этот файл больше не нужен.
// ⚠️ Проверять после правок рендера: `objdump -p xrRenderPC_R4.dll | grep CreateFile2`
// должно быть пусто. Соседние импорты (GetFileInformationByHandleEx, SetFileInformationByHandle)
// существуют начиная с Vista и Windows 7 не мешают.

#ifdef XR_PLATFORM_WINDOWS

extern "C"
{
// Структура объявлена в fileapi.h под `#if _WIN32_WINNT >= 0x0602`, а наша сборка целится ниже —
// поэтому там её нет, и это ровно то же расхождение, из-за которого правка вообще нужна:
// библиотеку собирали под Win8, нас собирают под более раннюю цель. Объявляем сами, раскладка
// фиксирована ABI.
struct da_CREATEFILE2_EXTENDED_PARAMETERS
{
    DWORD dwSize;
    DWORD dwFileAttributes;
    DWORD dwFileFlags;
    DWORD dwSecurityQosFlags;
    LPSECURITY_ATTRIBUTES lpSecurityAttributes;
    HANDLE hTemplateFile;
};

static HANDLE WINAPI da_CreateFile2_win7(LPCWSTR file_name, DWORD desired_access, DWORD share_mode,
    DWORD creation_disposition, da_CREATEFILE2_EXTENDED_PARAMETERS* extended)
{
    DWORD flags_and_attributes = FILE_ATTRIBUTE_NORMAL;
    LPSECURITY_ATTRIBUTES security = nullptr;
    HANDLE template_file = nullptr;

    if (extended)
    {
        // CreateFileW принимает атрибуты, флаги и SQOS одним полем — CreateFile2 их разделяет.
        flags_and_attributes =
            extended->dwFileAttributes | extended->dwFileFlags | extended->dwSecurityQosFlags;
        security = extended->lpSecurityAttributes;
        template_file = extended->hTemplateFile;
    }

    return CreateFileW(file_name, desired_access, share_mode, security, creation_disposition,
        flags_and_attributes, template_file);
}

// Сама запись таблицы импорта. Не static и с used — иначе LTO вправе её выбросить, ссылка на
// неё живёт в чужом объектном файле, которого оптимизатор не видит.
__attribute__((used)) void* __imp_CreateFile2 = reinterpret_cast<void*>(&da_CreateFile2_win7);
}

#endif // XR_PLATFORM_WINDOWS
