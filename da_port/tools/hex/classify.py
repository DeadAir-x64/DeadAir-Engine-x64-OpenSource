# -*- coding: utf-8 -*-
"""Отбор текстур, годных для шестиугольного разрыва повторов.

Нейронка тут не нужна: обе решающие величины меряются напрямую по картинке.

  1. ШОВ. Если текстура задумана тайловой, её левый край продолжает правый. Сравниваем разрыв
     на стыке с обычным перепадом между соседними столбцами ВНУТРИ картинки. Отношение около
     единицы = бесшовная, много больше = уникальная развёртка, приём применять НЕЛЬЗЯ.

  2. ПРАВИЛЬНОСТЬ УЗОРА. Кладка, плитка, доски дают резкие пики в автокорреляции: узор строго
     периодичен. Грунт, бетон, ржавчина дают гладкую — они «случайны». Приём ломает именно
     периодичные: ряды соседних плиток не сойдутся.

Считается через БПФ, входит в numpy, ничего дополнительно ставить не надо.
"""
import sys, os, glob, io
import numpy as np
from PIL import Image

TOOLS = r"D:/Dead Air/xray-16/da_port/tools"
sys.path.insert(0, TOOLS)
_src = open(os.path.join(TOOLS, 'unpack_xdb.py'), 'r', encoding='utf-8').read().split('if __name__')[0]
M = {}
exec(compile(_src, 'unpack_xdb.py', 'exec'), M)
BS = chr(92)


def alpha_fraction(data):
    """Доля заметно прозрачных пикселей. Больше нуля = это НЕ тайловая поверхность.

    ⛔ Заслон добавлен после провала первого прогона: в отобранное попали 127 текстур деревьев и
    47 текстур персонажей. Их обманул шов - у атласа листвы края ПУСТЫЕ (прозрачные), левый край
    сходится с правым идеально, и мера рапортует «бесшовная». Но лист, декаль и лицо не тайлятся
    никогда: там атлас, а не повторяющаяся поверхность.
    """
    im = Image.open(io.BytesIO(data))
    if im.mode not in ('RGBA', 'LA', 'PA'):
        try:
            im = im.convert('RGBA')
        except Exception:
            return 0.0
    a = np.asarray(im.convert('RGBA'))[:, :, 3]
    return float(np.mean(a < 250))


def edge_content(a):
    """Есть ли на краях вообще рисунок. Меньше ~0.5 = край пустой, шву верить нельзя.

    Вторая половина того же заслона: край может быть не прозрачным, а просто ровным (чёрная рамка,
    залитое поле). Тогда разрыв на стыке нулевой по построению, и текстура притворяется бесшовной.
    """
    inner = float(np.std(a))
    if inner < 1e-6:
        return 0.0
    edge = float(np.std(np.concatenate([a[0, :], a[-1, :], a[:, 0], a[:, -1]])))
    return edge / inner


def load_gray(data, max_side=256):
    """DDS -> полутоновый массив float32 в [0,1]."""
    im = Image.open(io.BytesIO(data))
    im = im.convert('L')
    # Безусловно в квадрат. Пропорции при этом теряются, и это НЕ важно: обе меры к ним
    # безразличны. Шов сравнивает края с внутренним перепадом - сжатие меняет и то, и другое
    # одинаково. Пик-к-среднему в спектре не зависит от того, на какую частоту лёг пик, - только
    # от того, насколько он острый.
    im = im.resize((max_side, max_side), Image.BILINEAR)
    return np.asarray(im, dtype=np.float32) / 255.0


def seam_score(a):
    """Во сколько раз разрыв на стыке больше обычного перепада внутри. 1 = бесшовная."""
    inner_x = np.mean(np.abs(np.diff(a, axis=1)))
    inner_y = np.mean(np.abs(np.diff(a, axis=0)))
    edge_x = np.mean(np.abs(a[:, 0] - a[:, -1]))
    edge_y = np.mean(np.abs(a[0, :] - a[-1, :]))
    eps = 1e-6
    return max(edge_x / (inner_x + eps), edge_y / (inner_y + eps))


def regularity(a, lowcut=3):
    """Сила самого заметного повтора, 0..1. Высокое = строгий периодичный узор.

    Автокорреляция ПОСЛЕ высокочастотной фильтрации. Порядок здесь и есть суть.

    ⛔ Сырая автокорреляция врёт: у текстуры с плавными крупными пятнами она остаётся высокой вдали
    от центра, и бетон получал 0.6-0.8 наравне с кладкой. Периодичность там ни при чём - это низкие
    частоты. Замерено, не рассуждение.

    ⛔ Пик-к-среднему в спектре тоже не разделяет: по замеру на 1279 текстурах медианы всех папок
    легли в 2.5-3.0, у грунта 2.52 против кладки 3.02 - щели нет. У любой текстуры есть САМАЯ
    сильная частота, и её отношение к средней определяется статистикой шума, а не узором.

    ✅ Работает связка: сначала убрать плавную составляющую, потом автокорреляция. Замер на тех же
    папках: грунт 0.153, кладка 0.341, и квартили не пересекаются.
    """
    h, w = a.shape
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    r2 = fy * fy + fx * fx

    # 1) Высокочастотный фильтр: вычитаем размытую копию. Гаусс считаем прямо в частотной области -
    #    scipy у нас нет, а через БПФ это одна строка.
    #
    #    Без этого шага автокорреляция врёт: у текстуры с плавными крупными пятнами она остаётся
    #    высокой ВДАЛИ от центра, и бетон получает столько же, сколько кладка. Периодичность там ни
    #    при чём - это низкие частоты. Проверено замером, а не рассуждением.
    sigma_px = 8.0
    lp = np.exp(-2.0 * (np.pi * sigma_px) ** 2 * r2)
    A = np.fft.fft2(a - a.mean())
    b = np.fft.ifft2(A * (1.0 - lp)).real

    # 2) Теперь автокорреляция честно отвечает на вопрос «повторяется ли рисунок со сдвигом».
    B = np.fft.fft2(b)
    ac = np.fft.fftshift(np.fft.ifft2(B * np.conj(B)).real)
    c = ac[h // 2, w // 2]
    if c <= 0:
        return 0.0
    ac /= c

    # 3) Центр вырезаем: совпадение картинки самой с собой к делу не относится, нас интересуют
    #    ВТОРИЧНЫЕ максимумы - шаг кирпича, доски, плитки.
    yy, xx = np.ogrid[:h, :w]
    rad = np.sqrt((yy - h // 2) ** 2 + (xx - w // 2) ** 2)
    mask = rad > max(lowcut, 8)
    if not np.any(mask):
        return 0.0
    return float(np.max(ac[mask]))


def verdict(seam, reg, alpha, edge):
    if alpha > 0.02:
        return 'НЕТ - прозрачность (атлас, листва, декаль)'
    if edge < 0.5:
        return 'НЕТ - пустой край (шву верить нельзя)'
    if seam > 2.5:
        return 'НЕТ - шов (уникальная развёртка)'
    if reg > 0.32:
        return 'НЕТ - строгий узор (ряды разъедутся)'
    if reg > 0.22:
        return 'спорно - узор заметен'
    return 'ДА'


def main():
    pats = sys.argv[1:] or ['briks/', 'crete/', 'ground/', 'prop/', 'metal/', 'wood/']
    seen = {}
    for arc in sorted(glob.glob(r'D:/Dead Air/Dead Air/database/textures.xdb*')):
        try:
            fat = M['load_fat'](arc)
        except Exception:
            continue
        fp = open(arc, 'rb')
        for e in fat:
            n = e['name'].lower().replace(BS, '/')
            if not n.endswith('.dds') or e['size_real'] == 0:
                continue
            if n.endswith('_bump.dds') or n.endswith('_bump#.dds'):
                continue
            short = n.replace('textures/', '')
            if not any(short.startswith(p) for p in pats):
                continue
            if short in seen:
                continue
            try:
                seen[short] = M['extract_file'](fp, None, e)
            except Exception:
                pass
        fp.close()

    rows = []
    skipped = []
    for name in sorted(seen):
        try:
            a = load_gray(seen[name])
        except Exception as ex:
            skipped.append((name, type(ex).__name__ + ': ' + str(ex)[:60]))
            continue
        s = seam_score(a)
        r = regularity(a)
        al = alpha_fraction(seen[name])
        ec = edge_content(a)
        rows.append((name, s, r, verdict(s, r, al, ec)))

    print('%-46s %7s %8s   %s' % ('текстура', 'шов', 'узор', 'вердикт'))
    print('-' * 104)
    for name, s, r, v in rows:
        print('%-46s %7.2f %8.3f   %s' % (name[:46], s, r, v))

    print()
    ok = sum(1 for _, _, _, v in rows if v == 'ДА')
    print('прочитано: %d, НЕ прочитано: %d' % (len(rows), len(skipped)))
    if skipped:
        print()
        print('ПРОПУЩЕНЫ (Pillow не осилил формат):')
        import collections
        why = collections.Counter(r for _, r in skipped)
        for r, k in why.most_common(6):
            print('  %4d  %s' % (k, r))
        for n, r in skipped[:5]:
            print('     ', n)
    print()
    print('годных: %d из %d прочитанных (%.0f%%)' % (ok, len(rows), 100.0 * ok / max(1, len(rows))))
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'hex_whitelist.txt')
    with io.open(out, 'w', encoding='utf-8') as f:
        f.write('; [DA_PORT] Текстуры, годные для шестиугольного разрыва повторов.' + chr(10))
        f.write('; Отбор автоматический: шов < 2.5 (бесшовная) и узор < 0.22 (без строгой периодики).' + chr(10))
        f.write('; Меры и пороги - в hex_classify.py. Список НЕ окончателен, требует просмотра глазом.' + chr(10))
        for n, sc, rg, v in rows:
            if v == 'ДА':
                f.write(n[:-4] + chr(9) + ('; шов %.2f узор %.3f' % (sc, rg)) + chr(10))
    print('список записан:', out)


if __name__ == '__main__':
    main()
