# -*- coding: utf-8 -*-
"""
[DA_PORT] Перечислитель класса «VERIFY(x) → разыменование/индекс/деление x ПОСЛЕ» — тот самый баг,
что исчезает в релизе. Это НЕ полноценный AST (для этого libclang/CodeQL), но по сравнению с плоским
extract_verify.py уже отбирает именно опасный шаблон и режет безопасные (разыменование ВНУТРИ
выражения VERIFY вырезается целиком и крашить не может).

Эвристика на файл:
  1. Найти VERIFY/2/3/4(expr).
  2. Из expr достать «защищаемый символ» (указатель/индекс): первый идентификатор/цепочку до
     сравнения. Примеры: VERIFY(p) -> p; VERIFY(p != end()) -> p; VERIFY(idx < n) -> idx;
     VERIFY(V && x) -> V.
  3. В СЛЕДУЮЩИХ N строках искать реальное обращение к символу: sym-> / *sym / sym[ / [sym] / /sym.
  4. Отсечь: разыменование уже ВНУТРИ выражения VERIFY (безопасно); наличие [DA_PORT] в ±3 строках
     (уже разобрано); return сразу после (guard мог быть в виде if).

Запуск:
    python verify_deref_scan.py <корень src> [out.csv]
Печатает сводку и пишет кандидатов в csv (file,line,symbol,kind,deref_line).
"""
import os, re, sys, csv

VERIFY_RE = re.compile(r'\bVERIFY([234]?)\s*\(')
IDENT = r'[A-Za-z_]\w*(?:\(\))?(?:(?:->|\.)\w+(?:\(\))?)*'
DA_PORT = re.compile(r'\[DA_PORT\]|DA_PORT')
# Сигнал «значение из ДАННЫХ» (конфиг/сейв/сеть/скрипт/реестр по номеру) — именно наш опасный класс.
DATA_SRC = re.compile(
    r'smart_cast|dynamic_cast|\br_(?:u8|u16|u32|s8|s16|s32|float|stringZ|string|s64|u64)\b|'
    r'pSettings\s*->|READ_IF_EXISTS|line_exist|\.find\b|->find\b|net_Find|'
    r'object\s*\(|_by_id|_by_name|IndexToId|ReadAttrib|xml\.|LL_GetMotionDef|LL_BoneID|'
    r'get_upgrade|GetMaterial|section\b|ActiveItem|Visual\s*\(\)')


def find_paren(s, i):
    d = 0
    while i < len(s):
        if s[i] == '(':
            d += 1
        elif s[i] == ')':
            d -= 1
            if d == 0:
                return i
        i += 1
    return -1


def guarded_symbol(expr):
    """Достать защищаемый символ из выражения VERIFY."""
    e = expr.strip()
    # снять внешние !
    e = e.lstrip('!').strip()
    # цепочка сравнений/логики — берём левый операнд первого сравнения или всего
    for op in ('!=', '==', '<=', '>=', '<', '>', '&&', '||', '&', ','):
        idx = e.find(op)
        if idx > 0:
            e = e[:idx].strip()
            break
    e = e.lstrip('(').lstrip('!').strip()
    m = re.match(IDENT, e)
    if not m:
        return None
    sym = m.group(0)
    # базовый идентификатор для поиска обращения (без хвоста ()/.member)
    base = re.match(r'[A-Za-z_]\w*', sym).group(0)
    return base


def deref_kind(line, sym):
    s = re.escape(sym)
    if re.search(r'\b' + s + r'\s*\[', line):
        return 'index'          # sym[...]
    if re.search(r'\[\s*' + s + r'\b', line):
        return 'index-of'       # arr[sym]
    if re.search(r'\b' + s + r'\s*->', line):
        return 'arrow'          # sym->...
    if re.search(r'\*\s*' + s + r'\b', line):
        return 'star'           # *sym
    if re.search(r'/\s*' + s + r'\b', line) or re.search(r'\b' + s + r'\s*\)?\s*;?\s*$', line) and False:
        return 'div'
    if re.search(r'/\s*' + s + r'\b', line):
        return 'div'            # / sym
    return None


def scan_file(path, root, out, look=6):
    try:
        text = open(path, encoding='utf-8', errors='replace').read()
    except Exception:
        return
    # убрать блочные комментарии, чтобы не считать закомментированное
    text_nc = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)
    lines = text_nc.split('\n')
    raw = text.split('\n')
    for i, line in enumerate(lines):
        for m in VERIFY_RE.finditer(line):
            op = line.index('(', m.start())
            # выражение может быть многострочным — склеим до баланса
            joined = '\n'.join(lines[i:i + 4])
            op2 = joined.index('(', joined.index('VERIFY'))
            cp = find_paren(joined, op2)
            if cp < 0:
                continue
            expr = joined[op2 + 1:cp]
            sym = guarded_symbol(expr)
            if not sym:
                continue
            # безопасно: символ разыменован уже ВНУТРИ выражения -> вырезается целиком
            if deref_kind(expr, sym):
                continue
            # сколько строк занимает сам VERIFY
            span = joined[:cp].count('\n')
            # контекст вокруг для [DA_PORT]
            ctx = '\n'.join(raw[max(0, i - 3):i + look + 1])
            already = bool(DA_PORT.search(ctx))
            # data-driven сигнал: как присвоен sym (5 строк выше) + само выражение VERIFY
            assign_ctx = '\n'.join(raw[max(0, i - 5):i + 1])
            data_driven = bool(DATA_SRC.search(assign_ctx) or DATA_SRC.search(expr))
            # искать обращение в следующих строках
            for j in range(i + span + 1, min(len(lines), i + span + 1 + look)):
                k = deref_kind(lines[j], sym)
                if k:
                    rel = os.path.relpath(path, root).replace('\\', '/')
                    rel = re.sub(r'^(src/|engine/)', '', rel)
                    # приоритет: деление всегда важно; data-driven важно
                    prio = 'HIGH' if (k == 'div' or data_driven) else 'low'
                    out.append((rel, i + 1, sym, k, lines[j].strip()[:100],
                                'da_port' if already else '', prio))
                    break


def main():
    root = sys.argv[1]
    out = []
    for dp, _d, fs in os.walk(root):
        low = dp.lower()
        if any(x in low for x in ('\\build', '/build', 'externals', '.git', '3rd', 'thirdparty')):
            continue
        for fn in fs:
            if fn.endswith(('.cpp', '.h', '.hpp', '.inl')):
                scan_file(os.path.join(dp, fn), root, out)
    import collections
    total = len(out)
    done = sum(1 for r in out if r[5] == 'da_port')
    high = [r for r in out if r[6] == 'HIGH' and r[5] != 'da_port']
    print("Всего VERIFY-then-deref кандидатов:", total, "(из 5391 VERIFY)")
    print("  рядом уже [DA_PORT]:", done)
    print("  --- ПРИОРИТЕТ HIGH (data-driven / деление), не разобрано:", len(high), "---")
    print("      по каталогу:", dict(collections.Counter(r[0].split('/')[0] for r in high).most_common(10)))
    print("      делений (div):", sum(1 for r in high if r[3] == 'div'))
    print("  low (вероятные ложняки — член класса/локаль/инвариант):", total - done - len(high))
    if len(sys.argv) > 2:
        with open(sys.argv[2], 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f)
            w.writerow(['file', 'verify_line', 'symbol', 'kind', 'deref_line', 'da_port', 'prio'])
            # HIGH сверху
            w.writerows(sorted(out, key=lambda r: (r[6] != 'HIGH', r[0], r[1])))
        print("записано (HIGH сверху):", sys.argv[2])


if __name__ == '__main__':
    main()
