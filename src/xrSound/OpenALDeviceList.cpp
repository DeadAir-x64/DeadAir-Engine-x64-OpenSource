/*
 * Copyright (c) 2005, Creative Labs Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided
 * that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice, this list of conditions and
 * 	     the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, this list of conditions
 * 	     and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *     * Neither the name of Creative Labs Inc. nor the names of its contributors may be used to endorse or
 * 	     promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "stdafx.h"

#include "OpenALDeviceList.h"
#include "SoundRender_Core.h"

#include <al.h>
#include <alc.h>

ALDeviceList::ALDeviceList()
{
    snd_device_id = (u32)-1;
    Enumerate();
}

// [DA_PORT] OpenAL Soft returns device names as UTF-8 on Windows, but the game's fonts and string
// tables are Windows-1251. A device whose name contains Cyrillic therefore showed up as mojibake in
// the sound options ("OpenAL Soft on РКР°Р¶... (JBL TUNE770NC)"). Transcode once, where the names
// enter the engine, so everything downstream keeps working in the single-byte encoding it expects.
// Pure-ASCII names (the common case) pass through unchanged.
static void da_sound_name_to_ui_encoding([[maybe_unused]] pstr name, [[maybe_unused]] size_t size)
{
#if defined(XR_PLATFORM_WINDOWS)
    if (!name || !name[0])
        return;

    // ASCII-only? nothing to do.
    bool has_high_bit = false;
    for (pcstr p = name; *p; ++p)
    {
        if (static_cast<u8>(*p) >= 0x80)
        {
            has_high_bit = true;
            break;
        }
    }
    if (!has_high_bit)
        return;

    WCHAR wide[512];
    const int wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide, std::size(wide));
    if (wide_len <= 0)
        return; // not valid UTF-8 after all - leave the original bytes alone

    string512 converted;
    const int converted_len =
        WideCharToMultiByte(1251, 0, wide, -1, converted, sizeof(converted), "?", nullptr);
    if (converted_len <= 0)
        return;

    xr_strcpy(name, size, converted);
#endif
}

// [DA_PORT] Имя без служебного префикса бэкенда (см. Sound.h).
//
// Возвращает указатель внутрь исходной строки, копии не делает. Открывать устройство этим именем
// нельзя — драйверу нужна строка целиком, ровно так, как её выдало перечисление.
static pcstr da_sound_name_no_prefix(pcstr name)
{
    if (!name)
        return name;

    const size_t prefix_len = xr_strlen(snd_device_backend_prefix);
    if (0 == strncmp(name, snd_device_backend_prefix, prefix_len) && name[prefix_len])
        return name + prefix_len;

    return name;
}

// [DA_PORT] Слева от скобки стоит не имя, а РОЛЬ выхода — Windows пишет так, когда имя самого
// устройства вынесено в скобку. Список ролей короткий и заведомо неполный: незнакомое слово мы
// считаем именем, и это безопасная сторона ошибки — показать лишнее лучше, чем спрятать нужное.
static bool da_sound_name_is_generic(pcstr name)
{
    static constexpr pcstr generic[] = {
        // Имена приходят из перечисления в UTF-8…
        "Наушники", "Наушник", "Головной телефон", "Гарнитура",
        "Динамики", "Динамик", "Громкоговоритель",
        // …а из user.ltx — уже в windows-1251, потому что туда попадает строка, показанная игроку.
        // Функция одна на оба случая, поэтому кириллица перечислена в обеих кодировках; латиница
        // одинакова в любой.
        "\xCD\xE0\xF3\xF8\xED\xE8\xEA\xE8",                 // Наушники
        "\xCD\xE0\xF3\xF8\xED\xE8\xEA",                     // Наушник
        "\xC3\xEE\xEB\xEE\xE2\xED\xEE\xE9 \xF2\xE5\xEB\xE5\xF4\xEE\xED", // Головной телефон
        "\xC3\xE0\xF0\xED\xE8\xF2\xF3\xF0\xE0",             // Гарнитура
        "\xC4\xE8\xED\xE0\xEC\xE8\xEA\xE8",                 // Динамики
        "\xC4\xE8\xED\xE0\xEC\xE8\xEA",                     // Динамик
        "\xC3\xF0\xEE\xEC\xEA\xEE\xE3\xEE\xE2\xEE\xF0\xE8\xF2\xE5\xEB\xFC", // Громкоговоритель
        "Headphones", "Headphone", "Headset", "Speakers", "Speaker", "Earphones", "Handsfree",
    };

    for (pcstr g : generic)
    {
        if (0 == xr_stricmp(name, g))
            return true;
    }

    return false;
}

// [DA_PORT] Имя устройства для ЛЮДЕЙ: то, что стоит В МЕНЮ, в консоли и в user.ltx.
//
// Windows называет выход парой «роль (устройство)», и какая половина из них полезна — зависит от
// самой пары:
//
//   `Наушники (JBL TUNE770NC)`                  → `JBL TUNE770NC`  — слева роль, имя в скобке;
//   `P2510H PLUS (NVIDIA High Definition Audio)`→ `P2510H PLUS`    — слева монитор, в скобке чип;
//   `Realtek Digital Output (Realtek USB Audio)`→ `Realtek Digital Output`.
//
// Поэтому решает не позиция, а содержимое: если слева стоит роль («Наушники»), берём скобку, иначе
// левую часть. Отбрасываемая половина не несёт ничего, что помогло бы выбрать строку в списке, зато
// занимает больше места, чем нужная, и обрезает её на середине.
//
// Скобку возвращает на место вызывающий, если без неё два устройства становятся неразличимы.
XRSOUND_API void snd_device_display_name(pstr dst, size_t size, pcstr src)
{
    xr_strcpy(dst, size, da_sound_name_no_prefix(src));

    // Первая скобка, а не последняя: внутри имени их может быть несколько («Realtek(R) Audio»),
    // а точка разреза — та, что отделяет роль от устройства.
    pstr open = strchr(dst, '(');
    if (!open)
        return;

    string512 inner;
    xr_strcpy(inner, sizeof(inner), open + 1);
    if (pstr close = strrchr(inner, ')'))
        *close = 0;

    pstr left_end = open;
    while (left_end > dst && (' ' == left_end[-1] || '\t' == left_end[-1]))
        --left_end;
    *left_end = 0;

    if (inner[0] && (!dst[0] || da_sound_name_is_generic(dst)))
        xr_strcpy(dst, size, inner);
}

void ALDeviceList::IterateAndAddDevicesString(pcstr devices)
{
    // go through device list (each device terminated with a single NULL, list terminated with double NULL)
    while (*devices != '\0')
    {
        if (ALCdevice* device = alcOpenDevice(devices))
        {
            if (ALCcontext* context = alcCreateContext(device, nullptr))
            {
                alcMakeContextCurrent(context);

                const bool enumerateAllPresent = alcIsExtensionPresent(device, "ALC_ENUMERATE_ALL_EXT");

                // if new actual device name isn't already in the list, then add it...
                pcstr actualDeviceName = alcGetString(device, enumerateAllPresent ? ALC_ALL_DEVICES_SPECIFIER : ALC_DEVICE_SPECIFIER);

                if (actualDeviceName != nullptr && xr_strlen(actualDeviceName) > 0)
                {
                    int major, minor;
                    alcGetIntegerv(device, ALC_MAJOR_VERSION, sizeof(int), &major);
                    alcGetIntegerv(device, ALC_MINOR_VERSION, sizeof(int), &minor);

                    auto& addedDevice = m_devices.emplace_back(actualDeviceName, minor, major);

                    if (alIsExtensionPresent("EAX5.0"))
                        addedDevice.props.eax = 5;
                    else if (alIsExtensionPresent("EAX4.0"))
                        addedDevice.props.eax = 4;
                    else if (alIsExtensionPresent("EAX3.0"))
                        addedDevice.props.eax = 3;
                    else if (alIsExtensionPresent("EAX2.0"))
                        addedDevice.props.eax = 2;

                    addedDevice.props.efx = alcIsExtensionPresent(device, "ALC_EXT_EFX") == AL_TRUE;
                }
                alcDestroyContext(context);
            }
            alcCloseDevice(device);
        }
        devices += xr_strlen(devices) + 1;
    }
}

void ALDeviceList::Enumerate()
{
#ifndef MASTER_GOLD
    Msg("SOUND: OpenAL: enumerate devices...");
#endif
    // have a set of vectors storing the device list, selection status, spec version #
    // -- empty all the lists and reserve space for 10 devices
    m_devices.clear();

    // grab function pointers for 1.1-API functions, and if successful proceed to enumerate all devices
    if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT"))
    {
        pcstr devices = (pstr)alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER);

        xr_strcpy(m_defaultDeviceName, alcGetString(nullptr, ALC_DEFAULT_ALL_DEVICES_SPECIFIER));
        string512 default_name;
        snd_device_display_name(default_name, sizeof(default_name), m_defaultDeviceName);
        Log("SOUND: OpenAL: system default sound device name is", default_name);

        IterateAndAddDevicesString(devices);
    }
    // grab function pointers for 1.0-API functions, and if successful proceed to enumerate all devices
    else if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT"))
    {
        pcstr devices = (pstr)alcGetString(nullptr, ALC_DEVICE_SPECIFIER);

        xr_strcpy(m_defaultDeviceName, alcGetString(nullptr, ALC_DEFAULT_DEVICE_SPECIFIER));
        string512 default_name;
        snd_device_display_name(default_name, sizeof(default_name), m_defaultDeviceName);
        Log("SOUND: OpenAL: system default sound device name is", default_name);

#if defined(XR_PLATFORM_WINDOWS)
        // Xottab_DUTY
        // The problem from 2000s described below should not be relevant for Linux,
        // but still the case on Windows in 2022. And it probably won't be ever fixed...

        // ManowaR
        // "Generic Hardware" device on software AC'97 codecs introduce
        // high CPU usage ( up to 30% ) as a consequence - freezes, FPS drop
        // So if default device is "Generic Hardware" which maps to DirectSound3D interface
        // We re-assign it to "Generic Software" to get use of old good DirectSound interface
        // This makes 3D-sound processing unusable on cheap AC'97 codecs
        // Also we assume that if "Generic Hardware" exists, than "Generic Software" is also exists
        // Maybe wrong

        constexpr pcstr AL_GENERIC_HARDWARE = "Generic Hardware";
        constexpr pcstr AL_GENERIC_SOFTWARE = "Generic Software";

        if (0 == xr_stricmp(m_defaultDeviceName, AL_GENERIC_HARDWARE))
        {
            xr_strcpy(m_defaultDeviceName, AL_GENERIC_SOFTWARE);
            Log("SOUND: OpenAL: default sound device name set to", da_sound_name_no_prefix(m_defaultDeviceName));
        }
#endif

        IterateAndAddDevicesString(devices);
    }
    else
    {
        Msg("~ SOUND: OpenAL: EnumerationExtension NOT Present");
    }

    // make token
    const auto _cnt = GetNumDevices();

    auto& devices = SoundRender->Parent.GetDevicesList();
    devices.reserve(_cnt + 2);

    // [DA_PORT] Первым пунктом — «системное устройство». Это не имя конкретной звуковой карты, а
    // намерение «бери то, что выбрано в Windows», и разрешается оно заново при каждом запуске.
    // Поэтому подключённые наушники, переезд на другую машину и смена устройства в системе больше не
    // оставляют игру немой. Игрок по-прежнему может выбрать конкретную карту — тогда запомнится она.
    devices.emplace_back(xr_strdup(snd_device_auto_token), snd_device_auto);

    for (u32 i = 0; i < _cnt; ++i)
    {
        // [DA_PORT] Only the string shown in the UI/console is transcoded; m_devices[i].name keeps the
        // original bytes because alcOpenDevice() and the default-device comparisons need them verbatim.
        string512 short_name;
        snd_device_display_name(short_name, sizeof(short_name), m_devices[i].name);

        // [DA_PORT] Если без скобки два устройства свелись к одной строке (две пары наушников — обе
        // «Наушники»), обоим возвращаем полное имя: выбирать вслепую из одинаковых пунктов хуже.
        bool ambiguous = false;
        for (u32 j = 0; j < _cnt && !ambiguous; ++j)
        {
            if (i == j)
                continue;

            string512 other;
            snd_device_display_name(other, sizeof(other), m_devices[j].name);
            ambiguous = (0 == xr_stricmp(short_name, other));
        }

        string512 ui_name;
        xr_strcpy(ui_name, ambiguous ? da_sound_name_no_prefix(m_devices[i].name) : short_name);
        da_sound_name_to_ui_encoding(ui_name, sizeof(ui_name));
        devices.emplace_back(xr_strdup(ui_name), i);
    }
    devices.emplace_back(nullptr, -1);
    //--

    if (0 == GetNumDevices())
    {
        Log("SOUND: OpenAL: No devices available.");
    }
    else
    {
#ifndef MASTER_GOLD
        Log("SOUND: OpenAL: All available devices:");
        int majorVersion, minorVersion;

        for (u32 j = 0; j < GetNumDevices(); j++)
        {
            GetDeviceVersion(j, &majorVersion, &minorVersion);
            Msg("%d. %s, Spec Version %d.%d %s eax[%d] efx[%s]", j + 1, da_sound_name_no_prefix(GetDeviceName(j)), majorVersion,
                minorVersion, xr_stricmp(GetDeviceName(j), m_defaultDeviceName) == 0 ? "(default)" : "",
                GetDeviceDesc(j).props.eax, GetDeviceDesc(j).props.efx ? "yes" : "no");
        }
#endif
    }
}

pcstr ALDeviceList::GetDeviceName(size_t index) const
{
    return m_devices[index].name;
}

// [DA_PORT] Разрешает ВЫБОР игрока (snd_device_id) в индекс устройства, которое сейчас будем
// открывать (snd_device_active_id). Сам выбор при этом не трогается — иначе «авто» превратилось бы в
// имя конкретной карты и уехало бы в user.ltx навсегда (см. комментарий в Sound.h).
void ALDeviceList::SelectBestDevice()
{
    int best_majorVersion = -1;
    int best_minorVersion = -1;
    int majorVersion;
    int minorVersion;

    if (GetNumDevices() == 0)
    {
        snd_device_active_id = snd_device_auto;
        Msg("SOUND: Can't select device. List empty");
        return;
    }

    // Явный выбор игрока — но только если такое устройство ещё существует. Список между запусками
    // меняется (наушники отключили, драйвер переустановили), и индекс из прошлого сеанса может уже
    // никуда не показывать; молча взять чужое устройство хуже, чем вернуться к системному.
    if (snd_device_id != snd_device_auto)
    {
        if (snd_device_id < GetNumDevices())
        {
            snd_device_active_id = snd_device_id;
            string512 chosen;
            snd_device_display_name(chosen, sizeof(chosen), GetDeviceName(snd_device_active_id));
            Msg("SOUND: Selected device is %s (выбран игроком)", chosen);
            return;
        }

        Msg("! SOUND: выбранное устройство №%u больше не существует (осталось %u) — берём системное",
            snd_device_id, GetNumDevices());
    }

    // «Авто»: то, что выбрано в системе. Имя ищем в списке — из одноимённых берём с самой свежей
    // версией спецификации.
    u32 new_device_id = snd_device_auto;
    for (u32 i = 0; i < GetNumDevices(); ++i)
    {
        if (xr_stricmp(m_defaultDeviceName, GetDeviceName(i)) != 0)
            continue;

        GetDeviceVersion(i, &majorVersion, &minorVersion);
        if (majorVersion > best_majorVersion ||
            (majorVersion == best_majorVersion && minorVersion > best_minorVersion))
        {
            best_majorVersion = majorVersion;
            best_minorVersion = minorVersion;
            new_device_id = i;
        }
    }

    if (new_device_id == snd_device_auto)
    {
        // Системного устройства нет в перечислении — берём первое, но говорим об этом вслух: тишина
        // «без единой строки в логе» уже стоила одного разбора.
        Msg("~ SOUND: системное устройство [%s] в списке не найдено — берём первое из списка",
            da_sound_name_no_prefix(m_defaultDeviceName));
        new_device_id = 0;
    }

    snd_device_active_id = new_device_id;
    string512 selected;
    snd_device_display_name(selected, sizeof(selected), GetDeviceName(snd_device_active_id));
    Msg("SOUND: Selected device is %s (системное по умолчанию)", selected);
}

/*
 * Returns the major and minor version numbers for a device at a specified index in the complete list
 */
void ALDeviceList::GetDeviceVersion(size_t index, int* major, int* minor)
{
    *major = m_devices[index].major_ver;
    *minor = m_devices[index].minor_ver;
}
