#pragma once

struct XRPHYSICS_API ph_console
{
    static BOOL g_bDebugDumpPhysicsStep; //= 0;
    static float ph_tri_query_ex_aabb_rate; //= 1.3f;
    static int ph_tri_clear_disable_count; //= 10;
    static float phBreakCommonFactor; //= 0.01f;
    static float phRigidBreakWeaponFactor; //= 1.f;
    static float ph_step_time; //=fixed_step;
};

// [DA_PORT] Разрешены ли актёру лестницы. Ставится из скрипта (game.set_actor_allow_ladder), нужно
// скриптовым сценам вроде паркура: подтягивание само задаёт положение актёра покадрово, и автомат
// лестницы, перехватив управление на середине, утащил бы его к себе.
//
// ⚠️ Живёт ИМЕННО ЗДЕСЬ, а не в xrGame, хотя ставят его оттуда. Читает флаг ElevatorState, то есть
// xrPhysics, а xrPhysics про xrGame не знает — зависимость идёт в обратную сторону. Первая попытка
// объявить его в xrGame кончилась неразрешённой ссылкой на линковке.
extern XRPHYSICS_API bool g_da_actor_allow_ladder;
