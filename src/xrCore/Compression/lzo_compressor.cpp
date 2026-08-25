#include "stdafx.h"
#include "lzo_compressor.h"
#include "lzo/lzo1x.h"

int lzo_compress_dict(
    const void* input, u32 inputSize, void* output, u32& outputSize, void* workMem, const void* dict, u32 dictSize)
{
    // [DA_PORT] ⛔ Приведение &outputSize к lzo_uintp — запись ВОСЬМИ байт по адресу ЧЕТЫРЁХБАЙТНОГО
    // поля. На x86 типы совпадали, на x64 lzo_uint шире, и LZO затирает 4 байта за концом.
    //
    // Чем это опасно у нас: outputSize приходит ссылкой на NET_Buffer::count, а count — ПОСЛЕДНЕЕ
    // поле структуры. То есть затираются 4 байта за концом объекта; если он в куче — это порча
    // кучи, которая обнаруживается много позже и в другом месте.
    //
    // Дефект нашёл автор порта Dead Air Refined; у нас он был свой, того же происхождения.
    lzo_uint size = outputSize;
    const int r = lzo1x_999_compress_dict(
        (lzo_bytep)input, inputSize, (lzo_bytep)output, &size, workMem, (lzo_bytep)dict, dictSize);
    R_ASSERT2(size <= u32(-1), "LZO вернул длину, не влезающую в u32");
    outputSize = u32(size);
    return r;
}

int lzo_decompress_dict(
    const void* input, u32 inputSize, void* output, u32& outputSize, void* workMem, const void* dict, u32 dictSize)
{
    // [DA_PORT] То же самое, разбор выше.
    lzo_uint size = outputSize;
    const int r = lzo1x_decompress_dict_safe(
        (lzo_bytep)input, inputSize, (lzo_bytep)output, &size, workMem, (lzo_bytep)dict, dictSize);
    R_ASSERT2(size <= u32(-1), "LZO вернул длину, не влезающую в u32");
    outputSize = u32(size);
    return r;
}

int lzo_initialize() { return lzo_init(); }
u32 lzo_get_workmem_size() { return LZO1X_999_MEM_COMPRESS; }
