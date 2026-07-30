////////////////////////////////////////////////////////////////////////////
//	Module 		: script_sound.cpp
//	Created 	: 06.02.2004
//  Modified 	: 06.02.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script sound class
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "script_sound.h"
#include "script_game_object.h"
#include "script_game_object_impl.h"
#include "GameObject.h"
#include "ai_space.h"
#include "xrScriptEngine/script_engine.hpp"

CScriptSound::CScriptSound(LPCSTR caSoundName, ESoundTypes sound_type)
{
    m_bIsNoSound = !Engine.Sound.IsSoundEnabled();
    m_caSoundToPlay = caSoundName;
    string_path l_caFileName;
    if (FS.exist(l_caFileName, "$game_sounds$", caSoundName, ".ogg"))
        m_sound.create(caSoundName, st_Effect, sound_type);
    else
        GEnv.ScriptEngine->script_log(LuaMessageType::Error, "File not found \"%s\"!", l_caFileName);
}

CScriptSound::~CScriptSound()
{
    // [DA_PORT] Это уведомление, а не ошибка, и стек Lua к нему не нужен.
    //
    // Строкой ниже звук честно останавливается и освобождается: ни висящих указателей, ни утечки
    // источника здесь нет. Всё, что произошло, — скрипт отпустил объект звука раньше, чем тот
    // доиграл, и сборщик мусора убрал его. Слышно это ровно тогда, когда объект перестал
    // существовать для игры (дверь ушла в офлайн, уровень выгружается), то есть почти никогда.
    //
    // Печаталось это через script_log с типом Error, а он у ошибок дополнительно выводит ВЕСЬ стек
    // Lua. В логе получалось четыре строки вместо одной, причём стек называл тот скрипт, в котором
    // случайно сработал сборщик мусора, — то есть заведомо не виноватый. Именно так и выглядели
    // жалобы на «двери»: сверху стоял dinamic_hud, не имеющий к дверям никакого отношения.
    //
    // Поэтому печатаем сами: одна строка, знак уведомления, без стека.
    if (m_sound._feedback())
        Msg("~ [DA_PORT] звук \"%s\" отпущен скриптом до конца воспроизведения и остановлен",
            m_sound._handle() ? m_sound._handle()->file_name() : "неизвестный");

    m_sound.destroy();
}

Fvector CScriptSound::GetPosition() const
{
    VERIFY(m_sound._handle() || m_bIsNoSound);
    const CSound_params* l_tpSoundParams = m_sound.get_params();
    if (l_tpSoundParams)
        return (l_tpSoundParams->position);
    else
    {
        GEnv.ScriptEngine->script_log(LuaMessageType::Error, "Sound was not launched, can't get position!");
        return (Fvector().set(0, 0, 0));
    }
}

void CScriptSound::Play(CScriptGameObject* object, float delay, int flags)
{
    THROW3(m_sound._handle() || m_bIsNoSound, "There is no sound", m_caSoundToPlay.c_str());
    //	Msg							("%6d : CScriptSound::Play (%s), delay %f, flags
    //%d",Device.dwTimeGlobal,m_sound._handle()->file_name(),delay,flags);
    m_sound.play((object) ? &object->object() : NULL, flags, delay);
}

void CScriptSound::PlayAtPos(CScriptGameObject* object, const Fvector& position, float delay, int flags)
{
    THROW3(m_sound._handle() || m_bIsNoSound, "There is no sound", m_caSoundToPlay.c_str());
    //	Msg							("%6d : CScriptSound::Play (%s), delay %f, flags
    //%d",m_sound._handle()->file_name(),delay,flags);
    m_sound.play_at_pos((object) ? &object->object() : NULL, position, flags, delay);
}

void CScriptSound::PlayNoFeedback(
    CScriptGameObject* object, u32 flags /*!< Looping */, float delay /*!< Delay */, Fvector pos, float vol)
{
    THROW3(m_sound._handle() || m_bIsNoSound, "There is no sound", m_caSoundToPlay.c_str());
    m_sound.play_no_feedback((object) ? &object->object() : NULL, flags, delay, &pos, &vol);
}
