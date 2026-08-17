# -*- coding: utf-8 -*-
"""
[DA_PORT] Извлечь ВСЕ VERIFY/VERIFY2/VERIFY3/VERIFY4 из дерева движка: файл, строка, выражение.

Нужно для diff «наш код ↔ Anomaly (xray-monolith) ↔ база CoC»: где VERIFY совпадают, где мы их
трогали, где Anomaly чинил то же место. Выражение (первый аргумент VERIFY) нормализуется (убраны
пробелы), чтобы матчить одно и то же место в разошедшихся деревьях.

Запуск:
    python extract_verify.py <корень src> [выходной csv]
"""
import os, re, sys, csv

# VERIFY, VERIFY2..4, а также условные версии. Берём первый аргумент как «выражение».
VERIFY_RE = re.compile(r'\bVERIFY[234]?\s*\(')


def find_matching_paren(s, start):
    """start указывает на '(' — вернуть индекс закрывающей и содержимое."""
    depth = 0
    i = start
    while i < len(s):
        c = s[i]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def first_arg(inside):
    """Первый аргумент до верхнеуровневой запятой."""
    depth = 0
    for i, c in enumerate(inside):
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        elif c == ',' and depth == 0:
            return inside[:i]
    return inside


def norm(expr):
    # убрать пробелы/переносы для матчинга одинаковых мест
    return re.sub(r'\s+', '', expr)


def strip_comments(text):
    # убрать /* */ и // ... чтобы не считать закомментированные VERIFY
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    return text


def extract(root):
    out = []
    for dirpath, _dirs, files in os.walk(root):
        # пропустить внешние и билд-каталоги
        low = dirpath.lower()
        if any(x in low for x in ('\\build', '/build', 'externals', '.git', '3rd', 'thirdparty')):
            continue
        for fn in files:
            if not fn.endswith(('.cpp', '.h', '.hpp', '.inl')):
                continue
            path = os.path.join(dirpath, fn)
            try:
                with open(path, encoding='utf-8', errors='replace') as f:
                    text = f.read()
            except Exception:
                continue
            text = strip_comments(text)
            for m in VERIFY_RE.finditer(text):
                op = m.end() - 1  # индекс '('
                cp = find_matching_paren(text, op)
                if cp < 0:
                    continue
                inside = text[op + 1:cp]
                expr = first_arg(inside).strip()
                line = text.count('\n', 0, m.start()) + 1
                rel = os.path.relpath(path, root).replace('\\', '/')
                # нормализуем путь: убрать префикс src/ для сопоставимости деревьев
                rel = re.sub(r'^(src/|engine/)', '', rel)
                out.append((rel, line, norm(expr), expr[:120]))
    return out


def main():
    if len(sys.argv) < 2:
        print("usage: extract_verify.py <src root> [out.csv]")
        return
    root = sys.argv[1]
    rows = extract(root)
    outp = sys.argv[2] if len(sys.argv) > 2 else None
    print("VERIFY найдено: %d в %s" % (len(rows), root))
    if outp:
        with open(outp, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f)
            w.writerow(['file', 'line', 'expr_norm', 'expr'])
            w.writerows(rows)
        print("записано:", outp)


if __name__ == '__main__':
    main()
