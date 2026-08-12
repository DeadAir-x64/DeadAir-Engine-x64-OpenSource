#pragma once

namespace xray::render::RENDER_NAMESPACE
{
struct SMAP_Rect
{
    Ivector2 min, max;
    bool intersect(SMAP_Rect& R)
    {
        if (max.x < R.min.x)
            return false;
        if (max.y < R.min.y)
            return false;
        if (min.x > R.max.x)
            return false;
        if (min.y > R.max.y)
            return false;
        return true;
    }
    bool valid()
    {
        if (min.x == max.x)
            return false;
        if (min.y == max.y)
            return false;
        return true;
    }
    void setup(Ivector2& p, u32 size)
    {
        min = max = p;
        max.add(size - 1);
    }
    void get_cp(Ivector2& p0, Ivector2& p1)
    {
        p0.set(max.x + 1, min.y); // right
        p1.set(min.x, max.y + 1); // down
    }
};

class SMAP_Allocator
{
    u32 psize; // pool size
    xr_vector<SMAP_Rect> stack; //
    xr_vector<Ivector2> cpoint; // critical points
private:
    void _add(SMAP_Rect& R)
    {
        stack.push_back(R);
        Ivector2 p0, p1;
        R.get_cp(p0, p1);
        s32 ps = s32(psize);
        if ((p0.x < ps) && (p0.y < ps))
            cpoint.push_back(p0); // 1st
        if ((p1.x < ps) && (p1.y < ps))
            cpoint.push_back(p1); // 2nd
    }

public:
    void initialize(u32 _size)
    {
        psize = _size;
        stack.clear();
        cpoint.clear();
    }
    // [DA_PORT] Занять КОНКРЕТНЫЙ прямоугольник, если он свободен.
    //
    // Нужно для закрепления ячейки за лампой. Иначе упаковка «первый подходящий» раскладывает
    // атлас заново каждый кадр, и содержимое прошлого кадра переиспользовать нельзя. Замер:
    // при подписи на весь набор ламп совпадений было НОЛЬ из 106 — состав меняется сам собой,
    // видимость мигает по запросам перекрытия. Значит держать место надо полампово.
    BOOL push_at(SMAP_Rect& R, u32 _size, const Ivector2& at)
    {
        if (_size > psize || _size <= 4)
            return false;
        if (at.x < 0 || at.y < 0)
            return false;
        if (u32(at.x) + _size > psize || u32(at.y) + _size > psize)
            return false;

        Ivector2 p = at;
        R.setup(p, _size);
        for (SMAP_Rect& busy : stack)
            if (busy.intersect(R))
                return false;

        _add(R);
        return true;
    }

    BOOL push(SMAP_Rect& R, u32 _size)
    {
        VERIFY(_size <= psize && _size > 4);

        // setup first in the soup, if empty state
        if (stack.empty())
        {
            Ivector2 p;
            p.set(0, 0);
            R.setup(p, _size);
            _add(R);
            return true;
        }

        // perform search	(first-fit)
        for (u32 it = 0; it < cpoint.size(); it++)
        {
            R.setup(cpoint[it], _size);
            if (R.max.x >= int(psize))
                continue;
            if (R.max.y >= int(psize))
                continue;
            BOOL bIntersect = false;
            for (u32 t = 0; t < stack.size(); t++)
                if (stack[t].intersect(R))
                {
                    bIntersect = true;
                    break;
                }
            if (bIntersect)
                continue;

            // OK, place
            cpoint.erase(cpoint.begin() + it);
            _add(R);
            return true;
        }

        // fail
        return false;
    }
};
} // namespace xray::render::RENDER_NAMESPACE
