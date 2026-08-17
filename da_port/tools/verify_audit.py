# -*- coding: utf-8 -*-
"""
[DA_PORT] Заход по VERIFY на путях ЗАГРУЗКИ и СМЕНЫ УРОВНЯ.

ПОВОД. За один день три вылета одного класса: #73 (кросс-таблица), #74 (уборка потомков),
#75 (метка карты). Везде настоящая проверка существовала ТОЛЬКО в отладочной сборке: стоял VERIFY,
в релизе он исчезает, и следом шло разыменование или индексация. Это не совпадение, а система —
значит остальные такие места надо не ждать, а найти.

⛔ ПОЧЕМУ НЕ ГРЕП РУКАМИ. VERIFY в дереве тысячи. Опасны из них те, где проверенное выражение
ТУТ ЖЕ используется по назначению: разыменовывается, индексируется или служит индексом. Отобрать
это глазами по тысяче мест нельзя — потеряется половина, а доклад будет о полноте, которой нет.

ЧТО ДЕЛАЕТ. Находит VERIFY, определяет объемлющую функцию, отбирает функции путей загрузки и смены
уровня, и оставляет только те случаи, где проверенное выражение используется рядом.

⚠️ Средство РАЗВЕДКИ, а не приговор. Разбор охватности грубый (по отступам и скобкам), возможны и
пропуски, и лишнее. Каждое место всё равно читается глазами — но читать предстоит десятки, а не
тысячи.

Запуск:
    python verify_audit.py
    python verify_audit.py --all-functions      # без отбора по путям загрузки
"""
import argparse
import csv
import os
import re

ROOTS = [
    r"D:\Dead Air\xray-16\src\xrGame",
    r"D:\Dead Air\xray-16\src\xrServerEntities",
    r"D:\Dead Air\xray-16\src\xrAICore",
]

# Функции путей загрузки и смены уровня. Список намеренно широкий: лучше прочесть лишнее, чем
# пропустить — цена пропуска это вылет у игрока.
LOAD_FUNCS = re.compile(
    r"(?:^|::|\b)("
    r"load|Load|load_data|STATE_Read|state_read|net_Spawn|net_Destroy|"
    r"on_before_change_level|change_level|OnEvent|spawn|Spawn|restart|"
    r"reload|Reload|before_save|after_load|load_shedule|read"
    r")\w*\s*\(",
    re.I,
)

VERIFY_RE = re.compile(r"\bVERIFY[23]?\s*\(")
# Начало определения функции: тип, имя (возможно с классом), скобки, дальше { на этой или след. строке.
FUNC_RE = re.compile(r"^[A-Za-z_][\w:<>,\s\*&~]*?([A-Za-z_~]\w*)\s*\([^;{]*\)\s*(?:const\s*)?(?:\{)?\s*$")


def checked_expr(line):
    """Достаём проверяемое выражение из VERIFY(...) — первый аргумент."""
    i = line.find("VERIFY")
    j = line.find("(", i)
    if j < 0:
        return None
    depth = 0
    for k in range(j, len(line)):
        c = line[k]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                inner = line[j + 1:k]
                break
        elif c == "," and depth == 1:
            inner = line[j + 1:k]
            break
    else:
        return None
    return inner.strip()


def base_symbol(expr):
    """Из выражения вытаскиваем «главное» имя, за которым и следим дальше."""
    e = expr.strip()
    e = re.sub(r"^[!\(\s]+", "", e)
    m = re.match(r"([A-Za-z_]\w*(?:\(\))?(?:\.\w+|->\w+)*)", e)
    return m.group(1) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--all-functions", action="store_true")
    ap.add_argument("--window", type=int, default=15, help="сколько строк после VERIFY смотреть")
    args = ap.parse_args()

    rows = []
    scanned = 0

    for root in ROOTS:
        for dirpath, _dirs, files in os.walk(root):
            for fn in files:
                if not fn.endswith((".cpp", ".h", ".hpp", ".inl")):
                    continue
                path = os.path.join(dirpath, fn)
                try:
                    with open(path, encoding="utf-8", errors="replace") as f:
                        lines = f.readlines()
                except Exception:
                    continue
                scanned += 1

                cur_func = ""
                for i, line in enumerate(lines):
                    m = FUNC_RE.match(line.rstrip())
                    if m and "VERIFY" not in line and "return" not in line:
                        cur_func = m.group(1)

                    if not VERIFY_RE.search(line):
                        continue
                    if line.lstrip().startswith("//"):
                        continue

                    if not args.all_functions and not LOAD_FUNCS.search(cur_func + "("):
                        continue

                    expr = checked_expr(line)
                    if not expr:
                        continue
                    sym = base_symbol(expr)
                    if not sym:
                        continue

                    # Ищем использование проверенного рядом: разыменование, индексация,
                    # или использование как индекса.
                    tail = "".join(lines[i + 1: i + 1 + args.window])
                    esc = re.escape(sym)
                    used = None
                    if re.search(esc + r"\s*(->|\.)", tail):
                        used = "разыменование"
                    elif re.search(esc + r"\s*\[", tail):
                        used = "индексация"
                    elif re.search(r"\[\s*" + esc + r"\s*\]", tail):
                        used = "как индекс"
                    elif re.search(r"\*\s*" + esc + r"\b", tail):
                        used = "разыменование *"

                    if not used:
                        continue

                    rows.append(
                        dict(
                            file=os.path.relpath(path, r"D:\Dead Air\xray-16\src"),
                            line=i + 1,
                            func=cur_func,
                            expr=expr[:80],
                            usage=used,
                            code=line.strip()[:100],
                        )
                    )

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "verify_audit.csv")
    with open(out, "w", newline="", encoding="utf-8") as f:
        wr = csv.DictWriter(f, fieldnames=["file", "line", "func", "expr", "usage", "code"])
        wr.writeheader()
        wr.writerows(rows)

    print("просмотрено файлов: %d" % scanned)
    print("подозрительных мест: %d" % len(rows))
    print("отчёт: %s" % out)

    by_file = {}
    for r in rows:
        by_file[r["file"]] = by_file.get(r["file"], 0) + 1
    print("")
    print("по файлам (первые 20):")
    for k, v in sorted(by_file.items(), key=lambda kv: -kv[1])[:20]:
        print("   %-58s %3d" % (k, v))


if __name__ == "__main__":
    main()
