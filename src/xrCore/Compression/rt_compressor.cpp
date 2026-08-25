#include "stdafx.h"
#pragma hdrstop

#include "lzo/lzo1x.h"

#define HEAP_ALLOC(var, size) lzo_align_t __LZO_MMODEL var[((size) + (sizeof(lzo_align_t) - 1)) / sizeof(lzo_align_t)]

thread_local HEAP_ALLOC(rtc_wrkmem, LZO1X_1_MEM_COMPRESS);

void rtc_initialize() { VERIFY(lzo_init() == LZO_E_OK); }
u32 rtc_csize(u32 in)
{
    VERIFY(in);
    return in + in / 64 + 16 + 3;
}

size_t rtc_compress(void* dst, size_t dst_len, const void* src, size_t src_len)
{
    lzo_uint out_size = dst_len;
    // [DA_PORT] ЖИВЫЕ проверки вместо VERIFY. Возвращаемое значение здесь — единственный признак
    // того, что out_size осмыслен: при отказе LZO оставляет его частично записанным, а вызывающий
    // считает эту длину настоящей и берёт ровно столько байт. То есть в релизе битые данные шли бы
    // дальше молча — в сохранение или в сетевой пакет, — и обнаружились бы много позже и не здесь.
    const int r = lzo1x_1_compress((const lzo_byte*)src, (lzo_uint)src_len, (lzo_byte*)dst, &out_size, rtc_wrkmem);
    if (r != LZO_E_OK)
    {
        string32 code;
        R_ASSERT3(false, "сжатие LZO не удалось, код", xr_itoa(r, code, 10));
    }
    return out_size;
}
size_t rtc_decompress(void* dst, size_t dst_len, const void* src, size_t src_len)
{
    lzo_uint out_size = dst_len;
    // См. rtc_compress выше: та же причина, обратное направление.
    const int r = lzo1x_decompress((const lzo_byte*)src, (lzo_uint)src_len, (lzo_byte*)dst, &out_size, rtc_wrkmem);
    if (r != LZO_E_OK)
    {
        string32 code;
        R_ASSERT3(false, "распаковка LZO не удалась, код", xr_itoa(r, code, 10));
    }
    return out_size;
}
