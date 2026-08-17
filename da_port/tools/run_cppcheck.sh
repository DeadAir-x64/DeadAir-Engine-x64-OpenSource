#!/bin/bash
# [DA_PORT] Статический анализ ТОЛЬКО наших правок.
#
# ЗАЧЕМ. Именно так X-Ray вскрывали публично: разбор «Anomalies in X-Ray Engine» прогнал версию 1.6
# через анализатор и нашёл, по их словам, «немало избыточного и подозрительного кода, а также
# ошибочных и опасных мест». У нас в CI до сих пор только сборка и проверка стиля.
#
# ⛔ ПОЧЕМУ НЕ ВЕСЬ ДВИЖОК. Сток 2007 года даёт тысячи срабатываний, которые мы всё равно не будем
# чинить: это чужой код, он работает, и правка ради предупреждения там опаснее самого
# предупреждения. Заслон, который кричит без причины, перестают читать целиком — это у нас уже было
# с проверкой ABI.
#
# Поэтому берём файлы, помеченные нашими комментариями `[DA_PORT]`/`[DA]` — их около 450, и это
# ровно то, за что отвечаем мы.
#
# ⚠️ Анализатор НЕ собирает проект и не знает наших define. Часть срабатываний будет ложной из-за
# невидимых ему веток (#ifdef USE_DX11 и подобные). Читать вывод надо как список мест для взгляда, а
# не как список дефектов.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$REPO_ROOT/da_port/tools/cppcheck_report.txt"
CPPCHECK="${CPPCHECK:-/c/msys64/mingw64/bin/cppcheck.exe}"
JOBS="${JOBS:-8}"

[ -x "$CPPCHECK" ] || { echo "не найден cppcheck: $CPPCHECK"; exit 1; }

cd "$REPO_ROOT"

# Список наших файлов. Externals не трогаем никогда: это чужие библиотеки целиком.
grep -rl "\[DA_PORT\]\|\[DA\]" src --include=*.cpp 2>/dev/null | grep -v "^src/Externals/" > /tmp/da_files.txt
echo "== файлов на разбор: $(wc -l < /tmp/da_files.txt)"

# --enable=warning,performance,portability: style намеренно НЕ включён. Он про оформление, а нам
#   нужны дефекты; на чужой кодовой базе он даёт основной объём шума.
# --inline-suppr: позволяет глушить точечно комментарием в коде, если срабатывание разобрано и ложно.
"$CPPCHECK" \
    --enable=warning,performance,portability \
    --inline-suppr \
    --inconclusive \
    --std=c++17 \
    --language=c++ \
    -j "$JOBS" \
    --suppress=missingInclude \
    --suppress=missingIncludeSystem \
    --suppress=unknownMacro \
    --suppress=unmatchedSuppression \
    --file-list=/tmp/da_files.txt \
    --template='{severity}\t{file}:{line}\t{id}\t{message}' \
    2> "$OUT"

echo "== отчёт: $OUT"
echo
echo "== по видам:"
awk -F'\t' '{print $1}' "$OUT" 2>/dev/null | sort | uniq -c | sort -rn | head
echo
echo "== по частоте проверок:"
awk -F'\t' '{print $3}' "$OUT" 2>/dev/null | sort | uniq -c | sort -rn | head -12
