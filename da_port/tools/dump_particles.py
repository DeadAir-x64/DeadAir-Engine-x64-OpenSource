# -*- coding: utf-8 -*-
"""Разбор particles.xr: имена эффектов, потолок частиц, текстуры, состав групп.

Формат взят из движка (Layers/xrRender/PSLibrary.cpp, ParticleEffectDef.cpp, ParticleGroup.cpp):
файл — дерево чанков (id u32, size u32, data). Внутри PS_CHUNK_SECONDGEN лежат эффекты
подчанками с номерами 0,1,2..., внутри THIRDGEN — группы.
"""
import io, struct, sys

PS_VERSION_CH, PS_SECONDGEN, PS_THIRDGEN = 0x0001, 0x0003, 0x0004
PED_NAME, PED_EFFECTDATA, PED_FLAGS, PED_SPRITE = 0x0002, 0x0003, 0x0005, 0x0007
PGD_NAME, PGD_EFFECTS = 0x0002, 0x0004
COMPRESS_MARK = 1 << 31


def chunks(buf):
    """Возвращает {id: bytes} для одного уровня дерева."""
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


def parse_effect(b):
    c = chunks(b)
    if PED_NAME not in c:
        return None
    name, _ = stringz(c[PED_NAME], 0)
    mx = struct.unpack_from('<I', c[PED_EFFECTDATA], 0)[0] if PED_EFFECTDATA in c else 0
    flags = struct.unpack_from('<I', c[PED_FLAGS], 0)[0] if PED_FLAGS in c else 0
    shader = texture = ''
    if PED_SPRITE in c:
        shader, p = stringz(c[PED_SPRITE], 0)
        texture, _ = stringz(c[PED_SPRITE], p)
    return dict(name=name, max=mx, flags=flags, shader=shader, texture=texture)


def parse_group(b):
    c = chunks(b)
    if PGD_NAME not in c:
        return None
    name, _ = stringz(c[PGD_NAME], 0)
    items = []
    raw = c.get(PGD_EFFECTS)
    if raw:
        cnt = struct.unpack_from('<I', raw, 0)[0]
        pos = 4
        for _ in range(cnt):
            try:
                # имя эффекта и три имени детей (on_play / on_birth / on_dead)
                eff, pos = stringz(raw, pos)
                for _ in range(3):
                    _child, pos = stringz(raw, pos)
                pos += 12  # time0, time1 (float) и флаги (u32)
                items.append(eff)
            except (ValueError, struct.error):
                break
    return dict(name=name, effects=items)


def main(path, out_path):
    data = io.open(path, 'rb').read()
    top = chunks(data)
    effects, groups = [], []
    if PS_SECONDGEN in top:
        for i, sub in sorted(chunks(top[PS_SECONDGEN]).items()):
            e = parse_effect(sub)
            if e:
                effects.append(e)
    if PS_THIRDGEN in top:
        for i, sub in sorted(chunks(top[PS_THIRDGEN]).items()):
            g = parse_group(sub)
            if g:
                groups.append(g)

    by_name = dict((e['name'], e) for e in effects)
    with io.open(out_path, 'w', encoding='utf-8') as out:
        out.write('файл: %s\nэффектов: %d, групп: %d\n' % (path, len(effects), len(groups)))
        tot = sum(e['max'] for e in effects)
        out.write('суммарный потолок частиц по всем эффектам: %d\n\n' % tot)

        key = [k.lower() for k in sys.argv[3:]] or ['gas', 'acid', 'chem', 'green', 'poison']
        out.write('=== ЭФФЕКТЫ по ключам %s ===\n' % key)
        for e in sorted(effects, key=lambda x: -x['max']):
            if any(k in e['name'].lower() or k in e['texture'].lower() for k in key):
                out.write('  %-46s частиц %5d  тексура %s\n' % (e['name'], e['max'], e['texture']))

        out.write('\n=== ГРУППЫ по тем же ключам (состав и сумма) ===\n')
        for g in groups:
            if not any(k in g['name'].lower() for k in key):
                continue
            s = sum(by_name.get(n, {}).get('max', 0) for n in g['effects'])
            out.write('  %-46s эффектов %2d, частиц всего %5d\n' % (g['name'], len(g['effects']), s))
            for n in g['effects']:
                e = by_name.get(n)
                out.write('       %-42s %5s  %s\n' % (n, e['max'] if e else '?', e['texture'] if e else ''))

        out.write('\n=== 15 самых тяжёлых эффектов вообще ===\n')
        for e in sorted(effects, key=lambda x: -x['max'])[:15]:
            out.write('  %-46s частиц %5d\n' % (e['name'], e['max']))
    print('готово')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
