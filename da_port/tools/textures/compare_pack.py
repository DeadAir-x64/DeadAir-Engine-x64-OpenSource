# -*- coding: utf-8 -*-
"""
[DA_PORT] Сравнение чужого набора текстур с тем, что уже есть в моде.

ЗАЧЕМ. Вопрос «стоит ли брать пакет» — это не «красивее ли», а «где он ДЕЙСТВИТЕЛЬНО крупнее».
Пакет на 1200 текстур может на треть состоять из того, что у нас уже такое же или лучше, и тогда
его установка целиком — это лишние гигабайты видеопамяти без выигрыша.

⛔ Сравниваем по ЗАГОЛОВКАМ DDS, а не по именам и не по размеру файла. Имя про разрешение не
говорит ничего, а размер файла зависит от формата сжатия: DXT1 вдвое легче DXT5 при том же
разрешении, и «файл меньше» вовсе не значит «текстура хуже».

Запуск:
    python compare_pack.py --pack "C:\\...\\Stalker Two-K Tinny Little"
"""
import argparse
import csv
import os
import struct
import sys


def dds_info(path):
    try:
        with open(path, "rb") as f:
            data = f.read(148)
    except Exception:
        return None
    if len(data) < 128 or data[:4] != b"DDS ":
        return None
    height, width = struct.unpack_from("<II", data, 12)
    mips = struct.unpack_from("<I", data, 28)[0]
    fourcc = data[84:88]
    fmt = fourcc.decode("latin1").strip("\x00") or "RGB"
    if fourcc == b"DX10" and len(data) >= 148:
        fmt = "DX10:%d" % struct.unpack_from("<I", data, 128)[0]
    return width, height, mips, fmt


def norm(name):
    """Приводим к общему виду: только путь от textures/, слеши вперёд, нижний регистр."""
    n = name.replace("\\", "/").lower()
    i = n.find("textures/")
    if i >= 0:
        n = n[i + len("textures/"):]
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pack", required=True)
    ap.add_argument("--inventory", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "опись_текстур.csv"))
    args = ap.parse_args()

    # ---- наша опись ----
    ours = {}
    with open(args.inventory, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if not r["width"]:
                continue
            ours[norm(r["name"])] = (int(r["width"]), int(r["height"]), r["format"], int(r["bytes"]))

    # ---- чужой пакет ----
    bigger, same, smaller, new, unreadable = [], [], [], [], []
    pack_bytes = 0

    for root, _dirs, files in os.walk(args.pack):
        for fn in files:
            if not fn.lower().endswith(".dds"):
                continue
            full = os.path.join(root, fn)
            info = dds_info(full)
            if not info:
                unreadable.append(full)
                continue
            w, h, _mips, fmt = info
            size = os.path.getsize(full)
            pack_bytes += size

            key = norm(os.path.relpath(full, args.pack))
            mine = ours.get(key)
            row = (key, w, h, fmt, size, mine)

            if not mine:
                new.append(row)
            elif w * h > mine[0] * mine[1]:
                bigger.append(row)
            elif w * h == mine[0] * mine[1]:
                same.append(row)
            else:
                smaller.append(row)

    total = len(bigger) + len(same) + len(smaller) + len(new)
    print("текстур в пакете: %d (%.1f МБ)" % (total, pack_bytes / 1024 / 1024))
    if unreadable:
        print("⛔ не прочиталось заголовков: %d" % len(unreadable))
    print("")
    print("  КРУПНЕЕ нашего : %5d  <- ради этого и ставят" % len(bigger))
    print("  такое же       : %5d" % len(same))
    print("  МЕЛЬЧЕ нашего  : %5d  <- ставить бессмысленно, станет хуже" % len(smaller))
    print("  у нас вообще нет: %4d  <- новые, а не замена" % len(new))

    def show(title, rows, n=12):
        if not rows:
            return
        print("")
        print(title)
        for key, w, h, fmt, size, mine in sorted(rows, key=lambda r: -(r[1] * r[2]))[:n]:
            if mine:
                print("   %-52s %4dx%-4d %-6s  было %dx%d %s" % (key[:52], w, h, fmt, mine[0], mine[1], mine[2]))
            else:
                print("   %-52s %4dx%-4d %-6s  (новая)" % (key[:52], w, h, fmt))

    show("самые крупные из тех, что КРУПНЕЕ нашего:", bigger)
    show("МЕЛЬЧЕ нашего (эти брать не надо):", smaller)

    # Сколько весит только полезная часть — то есть чего стоит выборочная установка.
    useful = sum(r[4] for r in bigger)
    print("")
    print("объём только «крупнее нашего»: %.1f МБ из %.1f МБ пакета" % (useful / 1024 / 1024, pack_bytes / 1024 / 1024))


if __name__ == "__main__":
    main()
