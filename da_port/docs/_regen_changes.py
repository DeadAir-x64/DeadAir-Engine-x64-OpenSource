# Пересобирает docs/02_PORT_CHANGES.md из маркеров [DA_PORT] в исходниках.
#
# Смысл: список правок порта не должен вестись руками — рукописный список расходится с кодом на
# третьей неделе. Здесь единственный источник правды — сами комментарии в исходниках, а документ
# всегда можно перегенерировать одной командой:
#
#     python xray-16/da_port/docs/_regen_changes.py
#
# Каталог запуска значения не имеет — пути берутся от самого файла.
import json
import os
import re

# Пути считаются от самого файла, а не от текущего каталога: документы переехали из корня в
# xray-16/da_port/docs, и прежние относительные пути после переезда молча писали в старое место —
# запуск «из корня репозитория» стирал документ в ноль правок.
HERE = os.path.dirname(os.path.abspath(__file__))  # xray-16/da_port/docs
REPO = os.path.dirname(os.path.dirname(HERE))  # xray-16
SRC = os.path.join(REPO, "src")
PATH_PREFIX = os.path.basename(REPO) + "/src/"  # как пути выглядят в документе
OUT_INDEX = os.path.join(HERE, "_da_port_index.json")
OUT_DOC = os.path.join(HERE, "02_PORT_CHANGES.md")

GROUPS = [
    ("Рендер — DirectX 11 (R4)", lambda p: p.startswith("xray-16/src/Layers/")),
    ("Движок и устройство", lambda p: p.startswith("xray-16/src/xrEngine/")),
    ("Игровая логика", lambda p: p.startswith("xray-16/src/xrGame/") and "_script" not in p and "/ui/" not in p),
    ("Интерфейс (UI)", lambda p: "/ui/" in p or "UI" in p.split("/")[-1]),
    ("Мост Lua ↔ C++", lambda p: "xrScriptEngine" in p or "_script" in p),
    ("Серверные сущности / ALife", lambda p: "xrServerEntities" in p or "alife" in p.lower()),
    ("Звук", lambda p: "xrSound" in p),
    ("Ядро и прочее", lambda p: True),
]


def collect():
    found = {}
    for dirpath, _, filenames in os.walk(SRC):
        for name in filenames:
            if not name.endswith((".cpp", ".h", ".inl")):
                continue
            path = os.path.join(dirpath, name)
            try:
                lines = open(path, encoding="utf-8", errors="surrogateescape").read().split("\n")
            except OSError:
                continue
            hits = []
            for i, line in enumerate(lines):
                if "DA_PORT" in line or re.search(r"//\s*DA:", line):
                    text = re.sub(r"^\s*//\s*", "", line).strip()
                    text = re.sub(r"^\[DA_PORT\]\s*", "", text).replace("DA: ", "")
                    hits.append((i + 1, text))
            if hits:
                rel = os.path.relpath(path, SRC).replace("\\", "/")
                found[PATH_PREFIX + rel] = hits
    return found


def group_of(path):
    for name, test in GROUPS:
        if test(path):
            return name
    return "Ядро и прочее"


def render(index):
    buckets = {}
    for path, hits in index.items():
        buckets.setdefault(group_of(path), []).append((path, hits))

    total = sum(len(hits) for hits in index.values())
    out = []
    add = out.append
    add("# Карта правок порта\n")
    add("Всё, что отличает этот порт от чистого OpenXRay, помечено в исходниках маркером `[DA_PORT]`")
    add("(инфраструктурные вещи — просто `DA:`). Ниже — полный список: **%d правк(и) в %d файлах**.\n"
        % (total, len(index)))
    add("Список сгенерирован из самих исходников, не написан вручную — значит он не разойдётся с кодом,")
    add("пока маркеры на месте. Пересобрать: `python xray-16/da_port/docs/_regen_changes.py`.\n")
    add("> Как читать: каждая строка — это `файл:строка` и первая строка комментария, объясняющего правку.")
    add("> Развёрнутое объяснение «почему» лежит рядом с кодом — комментарии писались как основная документация,")
    add("> потому что они не теряются при переносе и видны тому, кто читает функцию.\n")

    for name, _ in GROUPS:
        if name not in buckets:
            continue
        files = sorted(buckets[name])
        count = sum(len(hits) for _, hits in files)
        add(f"\n## {name}\n")
        add(f"*{len(files)} файл(ов), {count} правк(и)*\n")
        for path, hits in files:
            add(f"\n### `{path.replace('xray-16/src/', '')}`\n")
            for line_no, text in hits:
                text = text.strip() or "(продолжение комментария выше)"
                if len(text) > 150:
                    text = text[:147] + "..."
                add(f"- **:{line_no}** — {text}")
        add("")
    return "\n".join(out)


if __name__ == "__main__":
    index = collect()
    json.dump(index, open(OUT_INDEX, "w", encoding="utf-8"), ensure_ascii=False, indent=0)
    open(OUT_DOC, "w", encoding="utf-8").write(render(index))
    print("правок: %d, файлов: %d -> %s" % (sum(len(v) for v in index.values()), len(index), OUT_DOC))
