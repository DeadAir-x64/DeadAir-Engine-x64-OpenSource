#!/bin/bash
# [DA_PORT] Сборка движка под санитайзерами в контейнере. Разбор — в Dockerfile рядом.
#
# ЗАЧЕМ ЭТО ВООБЩЕ. Каждый класс вылетов, который мы ловили руками неделями, санитайзер выдаёт сам и
# с точным стеком:
#
#   форма столкновений умирает раньше владельца   -> ASan, use-after-free
#   запись за границы массива костей (22 места)   -> ASan, out-of-bounds
#   переполнение u32 в level.vertex_id            -> UBSan, переполнение
#   разыменования нуля                            -> ASan, с адресом и стеком
#
# ⛔ СОБИРАЕТ ТОЛЬКО ДЛЯ ПРОВЕРКИ. Получившиеся бинарники в игру на Windows не ставятся: это ELF под
# Linux, да ещё в два-три раза медленнее. Их дело — упасть красиво и назвать строку.
#
# ⚠️ Сборка идёт в ОТДЕЛЬНЫЙ каталог build_asan. Класть её в build_mingw нельзя: у ninja там свой
# кэш под другой компилятор, и смешение даёт «собралось» при неверных объектниках.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMAGE="deadair-asan:24.04"
OUT_DIR="$REPO_ROOT/da_port/tools/asan/out"
JOBS="${JOBS:-8}"

# Что собирать. По умолчанию только xrCore и xrEngine: они собираются за минуты и уже покрывают
# память, потоки и файловую систему. Полный движок (TARGETS=all) идёт заметно дольше.
TARGETS="${TARGETS:-xrCore xrEngine}"

mkdir -p "$OUT_DIR"

# 🪤 Docker на Windows ждёт пути вида D:/..., а MSYS отдаёт /d/... и вдобавок сам переписывает
# короткие пути аргументов (`-w /src` превращался в `S:/`). Поэтому путь переводим руками, а
# преобразование глушим переменной MSYS_NO_PATHCONV.
to_win() { case "$1" in /[a-z]/*) echo "$(echo "${1:1:1}" | tr a-z A-Z):${1:2}";; *) echo "$1";; esac; }
SRC_WIN="$(to_win "$REPO_ROOT")"
OUT_WIN="$(to_win "$OUT_DIR")"
export MSYS_NO_PATHCONV=1

echo "== дерево:   $SRC_WIN"
echo "== цели:     $TARGETS"
echo "== отчёты:   $OUT_WIN"

docker run --rm \
    -v "$SRC_WIN:/src" \
    -v "$OUT_WIN:/out" \
    -w /src \
    "$IMAGE" \
    bash -lc "
        set -e
        cmake -S /src -B /src/build_asan -G Ninja \
              -DCMAKE_BUILD_TYPE=Debug \
              -DXRAY_USE_ASAN=ON
        cmake --build /src/build_asan --target $TARGETS -j $JOBS
        echo
        echo '== собрано. Отчёты санитайзеров лягут в da_port/tools/asan/out/'
    "
