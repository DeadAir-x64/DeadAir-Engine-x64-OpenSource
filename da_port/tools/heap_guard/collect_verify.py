# -*- coding: utf-8 -*-
"""
[DA_PORT] Свод VERIFY со всех логов verify-тура: уникальные места + частота по локациям + топ стека.

Проходит tour/verify_all/NN_<level>.log, вынимает каждый FATAL ERROR (Expression/Function/File/Line
+ первые кадры стека после xrDebug::Fail) и группирует по (File:Line, Expression). Печатает: сколько
РАЗНЫХ локаций дали это место, пример аргумента, топ-кадр стека (вызывающий). Так весь урожай тура
виден одним списком, отсортированным по распространённости.
"""
import os, re, glob, collections

TOUR = r"D:\Dead Air\xray-16\da_port\tools\heap_guard\tour\verify_all"


def parse_log(path):
    """Yield dict per FATAL: expr, func, file, line, desc, arg, caller (2-й кадр стека)."""
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    i = 0
    n = len(lines)
    while i < n:
        if lines[i].startswith("FATAL ERROR"):
            rec = {"expr": "", "func": "", "file": "", "line": "", "desc": "", "arg": "", "caller": ""}
            j = i + 1
            while j < n and j < i + 12:
                m = re.match(r"\[error\] (\w+)\s*:\s*(.*)", lines[j].rstrip())
                if m:
                    k, v = m.group(1).lower(), m.group(2)
                    if k == "expression": rec["expr"] = v
                    elif k == "function": rec["func"] = v
                    elif k == "file": rec["file"] = v.replace("D:/Dead Air/xray-16/src/", "")
                    elif k == "line": rec["line"] = v
                    elif k == "description": rec["desc"] = v
                    elif k in ("argument", "arguments"): rec["arg"] = v
                if "stack trace:" in lines[j]:
                    # первый кадр после Fail = вызывающий r/verify
                    for s in lines[j:j + 8]:
                        if "xrDebug" in s and "Fail" in s:
                            continue
                        cm = re.search(r"at [0-9A-F]+ (\w[\w:]*)", s)
                        if cm and "Fail" not in cm.group(1):
                            rec["caller"] = cm.group(1)
                            break
                    break
                j += 1
            yield rec
            i = j
        else:
            i += 1


def main():
    logs = sorted(glob.glob(os.path.join(TOUR, "[0-9][0-9]_*.log")))
    if not logs:
        print("логов тура ещё нет:", TOUR)
        return
    # ключ = file:line|expr ; значение = множество локаций + пример
    groups = collections.defaultdict(lambda: {"levels": set(), "expr": "", "func": "", "arg": "", "caller": ""})
    for lp in logs:
        level = re.sub(r"^\d+_|\.log$", "", os.path.basename(lp))
        for r in parse_log(lp):
            key = "%s:%s | %s" % (r["file"], r["line"], r["expr"][:70])
            g = groups[key]
            g["levels"].add(level)
            g["expr"] = r["expr"]; g["func"] = r["func"]
            if r["arg"]: g["arg"] = r["arg"]
            if r["caller"]: g["caller"] = r["caller"]

    print("логов обработано: %d, уникальных мест VERIFY: %d\n" % (len(logs), len(groups)))
    rows = sorted(groups.items(), key=lambda kv: (-len(kv[1]["levels"]), kv[0]))
    for key, g in rows:
        print("×%-2d лок  %s" % (len(g["levels"]), key))
        print("        func=%s  caller=%s%s" % (
            g["func"], g["caller"] or "?", ("  arg=" + g["arg"]) if g["arg"] else ""))
        print("        локации: %s" % ", ".join(sorted(g["levels"])))
        print()


if __name__ == "__main__":
    main()
