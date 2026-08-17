# -*- coding: utf-8 -*-
"""
[DA_PORT] Установка набора травы Two-K (Grass Pack) в чистую копию игры.

⚠️ ПОЧЕМУ СТАВИТСЯ ПАРА, А НЕ ОДИН АТЛАС. Набор кладёт на каждый уровень ДВА файла:
`build_details.dds` — атлас травинок, и `level.details` — раскладку, то есть какая травинка растёт
в какой точке И КАКОЙ КУСОК АТЛАСА ей соответствует. Эти файлы связаны по номерам слотов. Поставить
только атлас — значит оставить старую раскладку указывать на новые слоты: трава получит чужие
текстуры вперемешку. Поэтому либо оба файла, либо ни одного.

⚠️ ЧЕМ ЭТО ПЛАТИТСЯ. Раскладка из набора сделана под Call of Chernobyl. Dead Air — форк CoC, имена
уровней те же, но авторская расстановка травы заменяется на COC-овскую. Если разница будет заметна
на глаз, набор снимается по манифесту (см. ниже) — файлы кладутся в loose-каталог gamedata и
перекрывают архив, а не заменяют его, так что откат сводится к удалению.

Запуск:
    python install_grass.py --pack "...\\Grass_COC_2k"
    python install_grass.py --pack "..." --dry-run     # показать, что будет сделано, без записи
"""
import argparse
import os
import shutil
import struct

MANIFEST = "_установлено_two-k_трава.txt"


def dds_size(path):
    """Разрешение из ЗАГОЛОВКА DDS: имя файла про размер не говорит ничего."""
    with open(path, "rb") as f:
        head = f.read(128)
    if len(head) < 128 or head[:4] != b"DDS ":
        return None
    height, width = struct.unpack_from("<II", head, 12)
    return width, height


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pack", required=True, help="каталог варианта набора (внутри должен быть gamedata/levels)")
    ap.add_argument("--game", default=r"D:\DA Clear\Dead Air")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    src_levels = os.path.join(args.pack, "gamedata", "levels")
    if not os.path.isdir(src_levels):
        raise SystemExit("в наборе нет gamedata/levels: %s" % src_levels)

    dst_levels = os.path.join(args.game, "gamedata", "levels")

    rows = []
    copied = 0
    total_bytes = 0

    for level in sorted(os.listdir(src_levels)):
        src_dir = os.path.join(src_levels, level)
        if not os.path.isdir(src_dir):
            continue

        atlas = os.path.join(src_dir, "build_details.dds")
        layout = os.path.join(src_dir, "level.details")
        if not (os.path.isfile(atlas) and os.path.isfile(layout)):
            print("! %s: нет пары атлас+раскладка, уровень пропущен" % level)
            continue

        size = dds_size(atlas)
        rows.append((level, size, os.path.getsize(atlas), os.path.getsize(layout)))

        dst_dir = os.path.join(dst_levels, level)
        for src in (atlas, layout):
            dst = os.path.join(dst_dir, os.path.basename(src))
            total_bytes += os.path.getsize(src)
            copied += 1
            if args.dry_run:
                continue
            os.makedirs(dst_dir, exist_ok=True)
            # Перед перезаписью отодвигаем чужой файл, если он вдруг уже лежит здесь: в чистой копии
            # loose-каталога levels нет вовсе, но повторный запуск не должен затирать молча.
            if os.path.exists(dst) and not os.path.exists(dst + ".до_two-k"):
                shutil.move(dst, dst + ".до_two-k")
            shutil.copy2(src, dst)

    if not args.dry_run:
        os.makedirs(dst_levels, exist_ok=True)
        with open(os.path.join(dst_levels, MANIFEST), "w", encoding="utf-8") as f:
            f.write("Набор травы Two-K (Akinaro), вариант Call of Chernobyl, 2K.\n")
            f.write("Ставится ПАРАМИ: build_details.dds (атлас) + level.details (раскладка).\n")
            f.write("Снять: удалить перечисленные каталоги уровней из gamedata/levels.\n")
            f.write("Файлы лежат в loose-каталоге и перекрывают архив levels.xdb*, сам архив не тронут.\n\n")
            for level, size, a, l in rows:
                f.write("%s\tатлас %s\t%.1f МБ\tраскладка %.1f МБ\n"
                        % (level, "%dx%d" % size if size else "?", a / 1048576, l / 1048576))

    print("уровней: %d, файлов: %d, объём: %.0f МБ%s"
          % (len(rows), copied, total_bytes / 1048576, "  (проба, ничего не записано)" if args.dry_run else ""))
    if not args.dry_run:
        print("манифест: %s" % os.path.join(dst_levels, MANIFEST))


if __name__ == "__main__":
    main()
