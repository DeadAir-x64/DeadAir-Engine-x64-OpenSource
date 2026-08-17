# -*- coding: utf-8 -*-
"""
[DA_PORT] Тройной diff VERIFY: наш код ↔ Anomaly (monolith) ↔ база CoC.

Матч по (файл, нормализованное выражение). База CoC — общий предок нас и Anomaly. Правка =
место (file,expr), которое в CoC ЕСТЬ, а в потомке НЕТ (убрано/переписано).

Вопросы, на которые отвечает:
  A. Что Anomaly убрал/переписал относительно CoC (их VERIFY-bugfixes).
  B. Что убрали МЫ.
  C. Пересечение — где мы и Anomaly сошлись.
  D. КАНДИДАТЫ ПЕРЕНЯТЬ: Anomaly тронул, а мы ещё нет (место живо у нас).
Сравнение — только по ФАЙЛАМ, присутствующим в обоих деревьях (иначе «отсутствие» = просто нет файла).
"""
import csv, sys, os, collections

S = r"C:/Users/cap3347/AppData/Local/Temp/claude/D--Dead-Air/dd18c6fc-030b-4ba4-8d44-42123d7a1df3/scratchpad"


def load(name):
    places = set()          # (file, expr_norm)
    files = set()
    byfile = collections.defaultdict(list)  # file -> [(expr_norm, expr)]
    with open(os.path.join(S, name), encoding='utf-8') as f:
        for r in csv.DictReader(f):
            key = (r['file'], r['expr_norm'])
            places.add(key)
            files.add(r['file'])
            byfile[r['file']].append((r['expr_norm'], r['expr']))
    return places, files, byfile


ours_p, ours_f, ours_b = load('verify_ours.csv')
anom_p, anom_f, anom_b = load('verify_anomaly.csv')
coc_p, coc_f, coc_b = load('verify_coc.csv')

# Общие файлы для каждой пары
coc_anom_files = coc_f & anom_f
coc_ours_files = coc_f & ours_f

# A. Anomaly убрал относительно CoC (в общих файлах)
anom_removed = {(fp, e) for (fp, e) in coc_p if fp in coc_anom_files and (fp, e) not in anom_p}
# B. Мы убрали относительно CoC
ours_removed = {(fp, e) for (fp, e) in coc_p if fp in coc_ours_files and (fp, e) not in ours_p}

# C. Пересечение (сошлись)
both_removed = anom_removed & ours_removed
# D. Кандидаты перенять: Anomaly убрал, а у нас место ЕЩЁ ЕСТЬ (не трогали), и файл у нас есть
candidates = {(fp, e) for (fp, e) in anom_removed if fp in ours_f and (fp, e) in ours_p}
# Наши уникальные: мы убрали, Anomaly нет
ours_only = ours_removed - anom_removed

print("=" * 70)
print("VERIFY: наш %d | Anomaly %d | CoC %d" % (len(ours_p), len(anom_p), len(coc_p)))
print("Anomaly тронул (убрал/переписал vs CoC): %d мест" % len(anom_removed))
print("Мы тронули (vs CoC):                     %d мест" % len(ours_removed))
print("Сошлись (и мы, и Anomaly):               %d" % len(both_removed))
print("КАНДИДАТЫ ПЕРЕНЯТЬ (Anomaly тронул, мы НЕТ): %d" % len(candidates))
print("Наши уникальные (мы тронули, Anomaly нет):   %d" % len(ours_only))
print("=" * 70)

# expr → пример строки для читаемости
expr_sample = {}
for fp, lst in coc_b.items():
    for en, e in lst:
        expr_sample[(fp, en)] = e

print("\n### КАНДИДАТЫ ПЕРЕНЯТЬ (Anomaly убрал VERIFY, у нас ещё стоит) — топ по файлам ###\n")
bycount = collections.Counter(fp for fp, e in candidates)
for fp, n in bycount.most_common(40):
    print("  %2d  %s" % (n, fp))

print("\n### Примеры конкретных мест-кандидатов (файл : выражение) ###\n")
for fp, e in sorted(candidates)[:60]:
    print("  %s : %s" % (fp, expr_sample.get((fp, e), e)[:90]))
