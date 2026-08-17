# [DA_PORT] Образ для прогона движка под санитайзерами.
#
# ЗАЧЕМ. В нашем MinGW санитайзеры недоступны: флаг `-fsanitize=address` компилятор принимает, а
# библиотеки нет — компоновка падает на `cannot find -lasan`. Clang в MSYS2 не установлен. При этом
# ручка `XRAY_USE_ASAN` в CMakeLists.txt существует с самого начала и просто никогда не включалась.
#
# Движок кроссплатформенный (наш CI собирает Ubuntu, Alpine, Fedora, macOS), а под Linux ASan,
# LeakSanitizer и UBSan работают штатно. Отсюда решение: собирать и гонять под Linux в контейнере, а
# играть по-прежнему на Windows.
#
# ⛔ ОБРАЗ НЕ СОДЕРЖИТ ИСХОДНИКОВ. Дерево монтируется при запуске (см. run_asan.sh): иначе каждый
# пересбор образа тащил бы несколько гигабайт, а правки в коде не были бы видны без пересборки.
#
# Список зависимостей взят из нашего же .github/workflows/cibuild.yml (шаг «Install Ubuntu
# packages») — держать два разных списка значит однажды разойтись с CI и искать разницу руками.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -qq && apt-get install -qq -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        libsdl2-dev \
        liblzo2-dev \
        libjpeg-dev \
        libopenal-dev \
        libogg-dev \
        libtheora-dev \
        libvorbis-dev \
    && rm -rf /var/lib/apt/lists/*

# [DA_PORT] Поведение санитайзеров задаётся здесь, а не в командной строке запуска: так оно
# одинаково у любого, кто взял образ, и не теряется при копировании команды.
#
#   halt_on_error=0     — не падать на первой находке. Нам нужен СПИСОК за прогон, а не первая
#                         ошибка: движок живёт долго, а перезапуск ради каждой находки съедает день.
#   detect_leaks=1      — утечки ищем: у нас их уже было шесть штук, и все нашлись вручную.
#   print_stacktrace=1  — UBSan без этого печатает одну строку без стека и почти бесполезен.
# 🪤 Каталог создаётся В ОБРАЗЕ намеренно. `log_path` уводит отчёты в файл, и если каталога нет,
# санитайзер молчит СОВСЕМ — ни отчёта, ни жалобы. Поймано на проверке: тестовая программа с явным
# выходом за границу массива не напечатала ничего.
RUN mkdir -p /out

ENV ASAN_OPTIONS=halt_on_error=0:detect_leaks=1:log_path=/out/asan:abort_on_error=0
ENV UBSAN_OPTIONS=halt_on_error=0:print_stacktrace=1:log_path=/out/ubsan
ENV LSAN_OPTIONS=report_objects=1

WORKDIR /src
