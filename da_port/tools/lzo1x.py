# Pure-python LZO1X decompressor, faithful to minilzo lzo1x_decompress.
def lzo1x_decompress(src, out_len):
    out = bytearray()
    ip = 0
    n = len(src)

    def cp(m, count):
        # copy 'count' bytes from match position m (may overlap); appends to out
        for _ in range(count):
            out.append(out[m]); m += 1
        return m

    t = 0
    # --- initial state ---
    if src[ip] > 17:
        t = src[ip] - 17; ip += 1
        if t < 4:
            state = 'match_next'
        else:
            for _ in range(t): out.append(src[ip]); ip += 1
            state = 'first_literal_run'
    else:
        state = 'loop'

    while True:
        if state == 'loop':
            t = src[ip]; ip += 1
            if t >= 16:
                state = 'match'; continue
            if t == 0:
                while src[ip] == 0:
                    t += 255; ip += 1
                t += 15 + src[ip]; ip += 1
            # copy t+3 literals
            for _ in range(t + 3): out.append(src[ip]); ip += 1
            state = 'first_literal_run'; continue

        if state == 'first_literal_run':
            t = src[ip]; ip += 1
            if t >= 16:
                state = 'match'; continue
            m = len(out) - (1 + 0x0800) - (t >> 2) - (src[ip] << 2); ip += 1
            out.append(out[m]); out.append(out[m+1]); out.append(out[m+2])
            state = 'match_done'; continue

        if state == 'match':
            if t >= 64:
                m = len(out) - 1 - ((t >> 2) & 7) - (src[ip] << 3); ip += 1
                cnt = (t >> 5) - 1 + 2
                cp(m, cnt)
                state = 'match_done'; continue
            elif t >= 32:
                tt = t & 31
                if tt == 0:
                    while src[ip] == 0:
                        tt += 255; ip += 1
                    tt += 31 + src[ip]; ip += 1
                m = len(out) - 1 - (src[ip] >> 2) - (src[ip+1] << 6); ip += 2
                cp(m, tt + 2)
                state = 'match_done'; continue
            elif t >= 16:
                m = len(out) - ((t & 8) << 11)
                tt = t & 7
                if tt == 0:
                    while src[ip] == 0:
                        tt += 255; ip += 1
                    tt += 7 + src[ip]; ip += 1
                m = m - (src[ip] >> 2) - (src[ip+1] << 6); ip += 2
                if m == len(out):
                    break  # EOF
                m -= 0x4000
                cp(m, tt + 2)
                state = 'match_done'; continue
            else:
                m = len(out) - 1 - (t >> 2) - (src[ip] << 2); ip += 1
                out.append(out[m]); out.append(out[m+1])
                state = 'match_done'; continue

        if state == 'match_done':
            t = src[ip-2] & 3
            if t == 0:
                state = 'loop'; continue
            state = 'match_next'; continue

        if state == 'match_next':
            for _ in range(t): out.append(src[ip]); ip += 1
            t = src[ip]; ip += 1
            state = 'match'; continue

    return bytes(out)
