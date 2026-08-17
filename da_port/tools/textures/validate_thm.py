# -*- coding: utf-8 -*-
"""
[DA_PORT] Оффлайн-проверка всех .thm тем же алгоритмом, что STextureParams::Load в движке.

ЗАЧЕМ. Загрузка падала на чтении за границей файла при разборе одного из ~6000 .thm, но обработка
многопоточная — имя виновного файла в лог не попадало. Гадать по одному кадру краха бессмысленно.
Этот скрипт читает КАЖДЫЙ .thm из архивов и повторяет ровно те чтения, что делает Load, отмечая
первый выход за границу. Итог — точное имя файла и место разрыва.
"""
import os, sys, struct, glob
sys.path.insert(0, r"D:/Dead Air")
import deadair_xdb as X

TEXTUREPARAM = 0x0812
TYPE         = 0x0813
TEXTURE_TYPE = 0x0814
DETAIL_EXT   = 0x0815
MATERIAL     = 0x0816
BUMP         = 0x0817
EXT_NORMALMAP= 0x0818
FADE_DELAY   = 0x0819
CFS_CompressMark = 0x80000000


class Overrun(Exception):
    def __init__(self, where):
        self.where = where


class Reader:
    """Повторяет IReader: r/advance/find_chunk с проверкой границ (как живой VERIFY в Mixed)."""
    def __init__(self, data):
        self.d = data
        self.n = len(data)
        self.pos = 0

    def r(self, cnt, where):
        if self.pos + cnt > self.n:
            raise Overrun("%s: r(%d) при pos=%d size=%d" % (where, cnt, self.pos, self.n))
        v = self.d[self.pos:self.pos + cnt]
        self.pos += cnt
        return v

    def r_u32(self, where):
        return struct.unpack("<I", self.r(4, where))[0]

    def r_u8(self, where):
        return self.r(1, where)[0]

    def r_stringZ(self, where):
        # Как в движке: читает по байту до нуля. За концом файла — выход за границу.
        start = self.pos
        while True:
            if self.pos >= self.n:
                raise Overrun("%s: r_stringZ без завершающего нуля от pos=%d" % (where, start))
            b = self.d[self.pos]; self.pos += 1
            if b == 0:
                return self.d[start:self.pos - 1]

    def find_chunk(self, cid):
        # Перебор чанков от начала (rewind). Возвращает размер данных найденного чанка и ставит pos
        # на его начало; 0 — не найдено или файл битый (совпадает с нашими правками find_chunk).
        self.pos = 0
        while self.pos + 8 <= self.n:
            t = struct.unpack_from("<I", self.d, self.pos)[0]
            s = struct.unpack_from("<I", self.d, self.pos + 4)[0]
            self.pos += 8
            if (t & ~CFS_CompressMark) == cid:
                if self.pos + s > self.n:
                    return 0  # заявленный размер выводит за конец — битый
                return s
            # пропуск чужого чанка
            if self.pos + s > self.n:
                return 0  # битый заголовок промежуточного чанка
            self.pos += s
        return 0


def check_thm(data):
    """Возвращает None если ок, иначе строку с местом разрыва — как это увидел бы движок."""
    R = Reader(data)
    try:
        # processFile: find_chunk(TYPE) + r_u32
        if R.find_chunk(TYPE) == 0:
            return "нет чанка TYPE"
        R.r_u32("TYPE")

        # Load: TEXTUREPARAM
        ps = R.find_chunk(TEXTUREPARAM)
        if ps < 4 + 7 * 4:
            return "TEXTUREPARAM усечён (%d < 32)" % ps
        R.r(4, "fmt"); [R.r_u32("param%d" % i) for i in range(7)]

        if R.find_chunk(TEXTURE_TYPE):
            R.r_u32("TEXTURE_TYPE")
        if R.find_chunk(DETAIL_EXT):
            R.r_stringZ("DETAIL_EXT.detail_name"); R.r_u32("DETAIL_EXT.scale")
        if R.find_chunk(MATERIAL):
            R.r_u32("MATERIAL"); R.r_u32("MATERIAL.weight")
        if R.find_chunk(BUMP):
            R.r_u32("BUMP.height"); R.r_u32("BUMP.mode"); R.r_stringZ("BUMP.bump_name")
        if R.find_chunk(EXT_NORMALMAP):
            R.r_stringZ("EXT_NORMALMAP")
        if R.find_chunk(FADE_DELAY):
            R.r_u8("FADE_DELAY")
    except Overrun as e:
        return e.where
    return None


def main():
    archives = sorted(glob.glob(os.path.join(X.DB_DIR, "textures.xdb*")))
    total = bad = 0
    for arc in archives:
        try:
            fat = X.load_fat(arc)
        except Exception as ex:
            print("! архив не прочитан:", arc, ex); continue
        fp = open(arc, "rb")
        _, data_off, _, _ = X.read_chunks(arc)
        for e in fat:
            if not e["name"].lower().endswith(".thm"):
                continue
            total += 1
            try:
                data = X.extract_file(fp, data_off, e)
            except Exception as ex:
                print("! не распакован:", e["name"], ex); bad += 1; continue
            problem = check_thm(data)
            if problem:
                bad += 1
                print("БИТЫЙ  %-60s размер=%d  -> %s" % (e["name"], len(data), problem))
        fp.close()
    print("\nвсего .thm: %d, с проблемой: %d" % (total, bad))


if __name__ == "__main__":
    main()
