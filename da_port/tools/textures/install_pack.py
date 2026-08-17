# -*- coding: utf-8 -*-
"""
[DA_PORT] Выборочная установка чужого набора текстур.

ЗАЧЕМ ВЫБОРОЧНО. Установка пакета целиком ухудшает часть картинки: в нём есть текстуры МЕЛЬЧЕ тех,
что уже лежат в моде (в разобранном наборе Two-K таких 76, почти все — карты рельефа, вдвое ниже
наших). Ставя всё подряд, вы их понижаете и не замечаете этого.

ЧТО ДЕЛАЕМ:
  • берём только то, что КРУПНЕЕ нашего, и то, чего у нас нет;
  • пропускаем равное (смысла нет) и мельче (станет хуже);
  • ⚠️ detail_grnd_* выносим ОТДЕЛЬНЫМ СЛОЕМ, не включая по умолчанию.

Почему detail отдельно: это текстуры земли и травы, а вокруг них крутилась наша работа по мерцанию
земли — причина была в r__tf_aniso 16 и в недосэмплировании трилинейкой при отрицательном mip bias
от апскейлеров. Учетверение их разрешения эту связку заденет напрямую, и проверять её надо глазами
отдельно от всего остального. Слой лежит рядом и включается копированием.

⚠️ .thm копируем ВМЕСТЕ с .dds. Это описатель текстуры (тип, параметры рельефа); если положить
картинку без него, движок возьмёт описатель из архива — от старой текстуры другого размера.

Запуск:
    python install_pack.py --pack "...\\Stalker Two-K Tinny Little" --game "D:\\DA Clear\\Dead Air"
    python install_pack.py ... --dry-run      # только посчитать, ничего не писать
"""
import argparse
import csv
import os
import shutil
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
    return width, height


def norm(name):
    n = name.replace("\\", "/").lower()
    i = n.find("textures/")
    if i >= 0:
        n = n[i + len("textures/"):]
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pack", required=True)
    ap.add_argument("--game", default=r"D:\DA Clear\Dead Air")
    ap.add_argument("--inventory", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "опись_текстур.csv"))
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    ours = {}
    with open(args.inventory, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["width"]:
                ours[norm(r["name"])] = (int(r["width"]), int(r["height"]))

    dst_main = os.path.join(args.game, "gamedata", "textures")
    dst_detail = os.path.join(args.game, "_слой_детальных_текстур_4k", "gamedata", "textures")

    installed, layered, skipped_same, skipped_small, no_header = [], [], 0, 0, 0
    bytes_main = bytes_layer = 0

    for root, _dirs, files in os.walk(args.pack):
        for fn in files:
            if not fn.lower().endswith(".dds"):
                continue
            full = os.path.join(root, fn)
            info = dds_info(full)
            if not info:
                no_header += 1
                continue
            w, h = info
            rel = norm(os.path.relpath(full, args.pack))
            mine = ours.get(rel)

            if mine and w * h < mine[0] * mine[1]:
                skipped_small += 1
                continue
            if mine and w * h == mine[0] * mine[1]:
                skipped_same += 1
                continue

            # Детальные текстуры земли — в отдельный слой.
            to_layer = rel.startswith("detail/")
            base = dst_detail if to_layer else dst_main
            dest = os.path.join(base, rel.replace("/", os.sep))
            size = os.path.getsize(full)

            if not args.dry_run:
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                shutil.copy2(full, dest)
                # Описатель рядом, если он есть в пакете.
                thm_src = os.path.splitext(full)[0] + ".thm"
                if os.path.exists(thm_src):
                    shutil.copy2(thm_src, os.path.splitext(dest)[0] + ".thm")

            if to_layer:
                layered.append((rel, w, h))
                bytes_layer += size
            else:
                installed.append((rel, w, h))
                bytes_main += size

    print("установлено в игру: %4d файлов (%.1f МБ)" % (len(installed), bytes_main / 1024 / 1024))
    print("вынесено в слой    : %4d файлов (%.1f МБ)  — detail_grnd_*, по умолчанию ВЫКЛЮЧЕН"
          % (len(layered), bytes_layer / 1024 / 1024))
    print("пропущено равных   : %4d" % skipped_same)
    print("пропущено мельче   : %4d  <- иначе стало бы хуже" % skipped_small)
    if no_header:
        print("⛔ без заголовка DDS: %d" % no_header)

    if args.dry_run:
        print("\n(пробный проход, ничего не записано)")
        return

    # ⭐ Список установленного — чтобы снять было так же легко, как поставить, и чтобы через месяц
    # не гадать, что здесь наше, а что чужое.
    manifest = os.path.join(dst_main, "_установлено_two-k.txt")
    os.makedirs(dst_main, exist_ok=True)
    with open(manifest, "w", encoding="utf-8") as f:
        f.write("Набор Two-K, выборочная установка. Только то, что КРУПНЕЕ имевшегося или ново.\n")
        f.write("Снять: удалить перечисленные файлы (и одноимённые .thm).\n\n")
        for rel, w, h in sorted(installed):
            f.write("%s\t%dx%d\n" % (rel, w, h))
    print("\nсписок установленного: %s" % manifest)
    print("слой детальных:        %s" % dst_detail)


if __name__ == "__main__":
    main()
