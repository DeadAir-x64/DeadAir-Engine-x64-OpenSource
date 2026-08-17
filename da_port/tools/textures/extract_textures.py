# -*- coding: utf-8 -*-
"""
[DA_PORT] Полная распаковка текстур мода + ОПИСЬ (разрешение, формат, мип-уровни).

ЗАЧЕМ ОПИСЬ, А НЕ ПРОСТО ФАЙЛЫ. Вопрос стоит так: «чем чужой набор отличается и что из него имеет
смысл брать». Ответ на него дают не файлы, а числа — какое разрешение у нас сейчас, какое там, и
где чужая текстура действительно КРУПНЕЕ, а не просто другая. Без описи сравнение сводится к
разглядыванию картинок, а это не сравнение.

⚠️ Работает по ЧИСТОЙ копии игры, а не по рабочей: рабочую мы постоянно правим, и опись по ней
через день станет неправдой.

Запуск:
    python extract_textures.py                      # из D:\\DA Clear\\Dead Air
    python extract_textures.py --game "D:\\..."     # другой каталог игры
    python extract_textures.py --inventory-only     # не писать файлы, только опись
"""
import argparse
import csv
import glob
import os
import struct
import sys

# Модуль чтения архивов X-Ray лежит в корне проекта, рядом с прочими разборными скриптами.
sys.path.insert(0, r"D:/Dead Air")
import deadair_xdb as X  # noqa: E402


def dds_info(data):
    """Разрешение, формат и число мип-уровней из заголовка DDS.

    ⛔ Читаем ЗАГОЛОВОК, а не имя файла. Имя про размер не говорит ничего: у X-Ray сплошь и рядом
    лежат текстуры, названные как «...2048», но пересобранные в меньшее разрешение, и наоборот.
    """
    if len(data) < 128 or data[:4] != b"DDS ":
        return None

    height, width = struct.unpack_from("<II", data, 12)
    mips = struct.unpack_from("<I", data, 28)[0]
    fourcc = data[84:88]

    fmt = fourcc.decode("latin1").strip("\x00") or "RGB"
    if fourcc == b"DX10" and len(data) >= 148:
        # У DX10 настоящий формат лежит в дополнительном заголовке, а не в fourcc.
        dxgi = struct.unpack_from("<I", data, 128)[0]
        fmt = "DX10:%d" % dxgi

    return dict(width=width, height=height, mips=mips, format=fmt)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default=r"D:\DA Clear\Dead Air")
    ap.add_argument("--out", default=None, help="куда писать (по умолчанию <game>\\_textures_extracted)")
    ap.add_argument("--inventory-only", action="store_true")
    args = ap.parse_args()

    db_dir = os.path.join(args.game, "database")
    archives = sorted(glob.glob(os.path.join(db_dir, "textures.xdb*")))
    if not archives:
        sys.exit("не найдено ни одного textures.xdb* в " + db_dir)

    out_root = args.out or os.path.join(args.game, "_textures_extracted")
    if not args.inventory_only:
        os.makedirs(out_root, exist_ok=True)

    print("игра:    ", args.game)
    print("архивов: ", len(archives))
    for a in archives:
        print("   %-22s %8.1f МБ" % (os.path.basename(a), os.path.getsize(a) / 1024 / 1024))

    rows = []
    written = 0
    skipped = 0

    for arc in archives:
        try:
            fat = X.load_fat(arc)
        except Exception as ex:
            print("  ⛔ %s: не читается (%r) — пропущен" % (os.path.basename(arc), ex))
            continue

        fp = open(arc, "rb")
        for e in fat:
            if e["size_real"] == 0:
                continue
            try:
                data = X.extract_file(fp, None, e)
            except Exception as ex:
                # ⚠️ Считаем и НАЗЫВАЕМ пропуски. Молчаливый пропуск превратил бы неполную
                # распаковку в «у мода этих текстур нет» — а это ложный вывод о чужом наборе.
                skipped += 1
                if skipped <= 10:
                    print("  ⛔ не распаковалось: %s (%r)" % (e["name"], ex))
                continue

            info = dds_info(data) if e["name"].lower().endswith(".dds") else None
            rows.append(
                dict(
                    archive=os.path.basename(arc),
                    name=e["name"].replace("\\", "/"),
                    bytes=e["size_real"],
                    width=info["width"] if info else "",
                    height=info["height"] if info else "",
                    mips=info["mips"] if info else "",
                    format=info["format"] if info else "",
                )
            )

            if not args.inventory_only:
                dest = os.path.join(out_root, e["name"].replace("\\", "/"))
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with open(dest, "wb") as w:
                    w.write(data)
                written += 1
        fp.close()

    csv_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "опись_текстур.csv")
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        wr = csv.DictWriter(f, fieldnames=["archive", "name", "bytes", "width", "height", "mips", "format"])
        wr.writeheader()
        wr.writerows(rows)

    # ---- сводка: то, ради чего всё и делалось ----
    dds = [r for r in rows if r["width"]]
    by_size = {}
    by_fmt = {}
    for r in dds:
        key = "%dx%d" % (r["width"], r["height"])
        by_size[key] = by_size.get(key, 0) + 1
        by_fmt[r["format"]] = by_fmt.get(r["format"], 0) + 1

    print("")
    print("записей в описи: %d (из них DDS с заголовком: %d)" % (len(rows), len(dds)))
    if skipped:
        print("⛔ НЕ РАСПАКОВАЛОСЬ: %d — опись НЕПОЛНАЯ, сравнивать с ней нельзя" % skipped)
    if not args.inventory_only:
        print("файлов записано: %d -> %s" % (written, out_root))
    print("опись:           %s" % csv_path)

    print("")
    print("по разрешению (первая десятка):")
    for k, v in sorted(by_size.items(), key=lambda kv: -kv[1])[:10]:
        print("   %-12s %5d" % (k, v))
    print("по формату:")
    for k, v in sorted(by_fmt.items(), key=lambda kv: -kv[1]):
        print("   %-12s %5d" % (k, v))


if __name__ == "__main__":
    main()
