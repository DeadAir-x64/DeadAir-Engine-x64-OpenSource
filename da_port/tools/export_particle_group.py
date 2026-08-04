# -*- coding: utf-8 -*-
"""Достаёт группу частиц из particles.xr в .pg — формат читает движок (CPGDef::Load2).

    python export_group.py "anomaly2\\studen_idle_bottom" <куда> [--drop подстрока ...]

--drop убирает из группы записи, чьё имя эффекта содержит подстроку. Так снимается лишнее,
не трогая сами эффекты.
"""
import io, os, struct, sys

PS_THIRDGEN = 0x0004
PGD_NAME, PGD_FLAGS, PGD_EFFECTS, PGD_TIME_LIMIT = 0x0002, 0x0003, 0x0004, 0x0005
COMPRESS_MARK = 1 << 31
XR = r'D:/Dead Air/extracted/particles.xr'


def chunks(buf):
    out, pos = {}, 0
    while pos + 8 <= len(buf):
        cid, size = struct.unpack_from('<II', buf, pos)
        pos += 8
        if size > len(buf) - pos:
            break
        out[cid & ~COMPRESS_MARK] = buf[pos:pos + size]
        pos += size
    return out


def stringz(buf, pos):
    end = buf.index(b'\0', pos)
    return buf[pos:end].decode('cp1251', 'replace'), end + 1


def find_group(name):
    top = chunks(io.open(XR, 'rb').read())
    for _, sub in sorted(chunks(top[PS_THIRDGEN]).items()):
        c = chunks(sub)
        if PGD_NAME not in c:
            continue
        nm, _ = stringz(c[PGD_NAME], 0)
        if nm.lower() != name.lower():
            continue
        flags = struct.unpack_from('<I', c[PGD_FLAGS], 0)[0] if PGD_FLAGS in c else 0
        tl = struct.unpack_from('<f', c[PGD_TIME_LIMIT], 0)[0] if PGD_TIME_LIMIT in c else 0.0
        items, raw = [], c[PGD_EFFECTS]
        cnt = struct.unpack_from('<I', raw, 0)[0]
        pos = 4
        for _ in range(cnt):
            eff, pos = stringz(raw, pos)
            kids = []
            for _ in range(3):
                k, pos = stringz(raw, pos)
                kids.append(k)
            t0, t1, fl = struct.unpack_from('<ffI', raw, pos)
            pos += 12
            items.append(dict(name=eff, kids=kids, t0=t0, t1=t1, flags=fl))
        return dict(name=nm, flags=flags, timelimit=tl, items=items)
    return None


def write_pg(g, path, drop):
    kept = [e for e in g['items'] if not any(d.lower() in e['name'].lower() for d in drop)]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with io.open(path, 'w', encoding='cp1251', newline='\r\n') as w:
        w.write('; [DA_PORT] Группа частиц, выгружена из particles.xr.\n')
        w.write('; Исходная: %s -- записей %d\n' % (g['name'], len(g['items'])))
        if drop:
            w.write('; Убрано по ключам %s: %d\n' % (', '.join(drop), len(g['items']) - len(kept)))
        w.write('\n[_group]\nflags = %d\neffects_count = %d\ntimelimit = %s\n'
                % (g['flags'], len(kept), repr(round(g['timelimit'], 6))))
        for i, e in enumerate(kept):
            w.write('\n[effect_%04d]\n' % i)
            w.write('effect_name = %s\n' % e['name'])
            w.write('on_play_child = %s\n' % e['kids'][0])
            w.write('on_birth_child = %s\n' % e['kids'][1])
            w.write('on_death_child = %s\n' % e['kids'][2])
            w.write('time0 = %s\n' % repr(round(e['t0'], 6)))
            w.write('time1 = %s\n' % repr(round(e['t1'], 6)))
            w.write('flags = %d\n' % e['flags'])
    print('записано: %s (%d из %d записей)' % (path, len(kept), len(g['items'])))


if __name__ == '__main__':
    args = sys.argv[1:]
    drop = []
    while '--drop' in args:
        i = args.index('--drop')
        drop.append(args[i + 1])
        del args[i:i + 2]
    g = find_group(args[0])
    if not g:
        raise SystemExit('группа не найдена: %s' % args[0])
    write_pg(g, args[1], drop)
