include_guard()

# The MSVC compiler settings:
# Set properties:
set(CMAKE_VS_USE_DEBUG_LIBRARIES "$<CONFIG:Debug>")

# Clear predefined flags which we going to define ourselves
string(REGEX REPLACE "/EH[a-z]+" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS}) # exceptions
string(REGEX REPLACE "/Z(7|i|I)" "" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG}) # debug information format

# Enable standard C++ exceptions everywhere except ReleaseMasterGold
add_compile_options($<$<NOT:$<CONFIG:ReleaseMasterGold>>:/EHsc>)

# Disable MS STL exceptions on ReleaseMasterGold
add_compile_definitions($<$<CONFIG:ReleaseMasterGold>:_HAS_EXCEPTIONS=0>)

# Enable debug information for all configurations
add_compile_options(/Zi)

# Enable SSE2 for 32-bit build
# (on x64 it's always enabled and produces error if try to to enable it)
add_compile_options($<$<EQUAL:${CMAKE_SIZEOF_VOID_P},4>:/arch:SSE2>)

# Disable specific warnings
add_compile_options(
    /wd4201 # nonstandard extension used : nameless struct/union
    /wd4251 # class 'x' needs to have dll-interface to be used by clients of class 'y'
    /wd4275 # non dll-interface class 'x' used as base for dll-interface class 'y'
)

# [DA_PORT] AddressSanitizer для MSVC — путь к ИГРАБЕЛЬНОЙ сборке с санитайзером.
#
# ЗАЧЕМ. В MinGW libasan нет вовсе (`cannot find -lasan`), а под Linux, где санитайзеры работают,
# игру не запустить: рендер R4 существует только под DX11. MSVC умеет и то и другое — ASan под
# Windows и DirectX, — поэтому играбельная проверка возможна только здесь.
#
# ⛔ БЛОК ПОКА НЕ РАБОТАЕТ, и это проверено, а не предположено. Две стены:
#
# 1. Ветка MSVC в CMake не доводится до генерации: `find_package(OpenAL)` стоит только в
#    XRay.Compiler.GNULike.cmake, а импортируемых целей для библиотек из sdk/libraries/x64
#    (OpenAL32.lib, lzo.lib, jpeg-static.lib, ogg/theora/vorbis) не создаёт никто. Конфигурация
#    падает на «Target xrSound links to OpenAL::OpenAL but the target was not found».
#
# 2. Под MSVC движок исторически собирается ВООБЩЕ НЕ CMake, а MSBuild по src/engine.sln с NuGet
#    (см. .github/workflows/cibuild.yml, задача windows). А это решение у нас устарело: файлы
#    da_gpu_timer.cpp, da_memory_probe.cpp и прочие наши не входят НИ В ОДИН .vcxproj, и сами
#    проекты не правились с 25.06.2026 — то есть до всех правок порта.
#
# Блок оставлен намеренно: он верен и заработает в тот день, когда закроют любую из двух стен.
# Пока играбельной проверки нет, есть прогонный стенд — src/utils/da_asan_probe.
#
# ⚠️ /Zi выше и /fsanitize=address совместимы, а вот с инкрементальной компоновкой и /RTC — нет,
# поэтому первую гасим явно.
#
# ⛔ Аллокатор обязан быть ЧИСТЫМ. В релизной конфигурации движок берёт mimalloc, и тогда ASan видит
# один большой блок вместо тысяч объектов — то есть молчит, а мы считаем, что всё чисто. Ставим
# USE_PURE_ALLOC принудительно; это медленнее, но проверка ради того и делается.
if (XRAY_USE_ASAN)
    add_compile_options(/fsanitize=address)
    add_compile_definitions(USE_PURE_ALLOC)
    add_link_options(/INCREMENTAL:NO)
endif()

# The MSVC linker settings:
add_link_options("/LARGEADDRESSAWARE")

set(XRAY_DISABLE_WARNINGS "/w")
