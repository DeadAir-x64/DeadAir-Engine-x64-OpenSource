# ЗАДАЧА для GLM — Кластер B: артефактные иммунитеты + вес (~22 биндинга, 1 пересборка)

Архитектор (я) уже всё разведал. Твоя работа — **механически накатать по образцу ниже**. НЕ
диагностируй, НЕ придумывай — все классы, поля, enum, файлы и сигнатуры даны точно. Если
что-то не сходится с тем, что видишь в коде — СТОП, спроси архитектора, не импровизируй.

## Что закрываем
DA-скрипты зовут на клиентском объекте (`arte`/`object` = `CScriptGameObject`) методы
`get/set_artefact_*_immunity` и веса. В движке их нет (подтверждено свипом). Делаем тонкие
обёртки — ровно как Anomaly делает `GetArtefactImmunity` (донор:
`scratchpad/monolith/src/xrGame/script_game_object3.cpp:1632`), но под именами DA.

ПРИНЦИП (потроха из донора): `smart_cast<CArtefact*>(&object())` → дёргаем поле артефакта.
Регистрируем на `CScriptGameObject` (НЕ на серверном классе — DA зовёт на клиентском объекте).

## Данные движка (проверено архитектором)
- Иммунитеты: `CArtefact::m_ArtefactHitImmunities` (тип `CHitImmunity`), `Artefact.h:93`.
- `CHitImmunity` (`hit_immunity.h`): геттер `GetHitImmunity(EHitType) const` ЕСТЬ; сеттера НЕТ — добавишь.
- Вес предмета: `CInventoryItem::Weight() const` + `SetWeight(float)` — ОБА есть (`inventory_item.h:148-149`).
- Доп.вес: `CArtefact::m_additional_weight` + геттер `AdditionalInventoryWeight() const` (`Artefact.h:67,86`); сеттера нет — добавишь.
- `enum ALife::EHitType` (точные имена!): eHitTypeBurn, eHitTypeShock, eHitTypeChemicalBurn,
  eHitTypeRadiation, eHitTypeTelepatic, eHitTypeWound, eHitTypeFireWound, eHitTypeStrike, eHitTypeExplosion.

## Мэппинг DA-метод → EHitType
| DA-имя (immunity) | EHitType |
|---|---|
| burn | eHitTypeBurn |
| shock | eHitTypeShock |
| chemical_burn | eHitTypeChemicalBurn |
| radiation | eHitTypeRadiation |
| telepatic | eHitTypeTelepatic |
| wound | eHitTypeWound |
| fire_wound | eHitTypeFireWound |
| strike | eHitTypeStrike |
| explosion | eHitTypeExplosion |

Вес: `get/set_artefact_weight` → `CInventoryItem::Weight()/SetWeight()`;
`get/set_artefact_additional_weight` → `CArtefact::AdditionalInventoryWeight()/m_additional_weight`.

---

## ШАГ 1 — добавить сеттер в `src/xrGame/hit_immunity.h`
В `class CHitImmunity`, public-секция, рядом с `GetHitImmunity`, добавь:
```cpp
    void SetHitImmunity(ALife::EHitType hit_type, float koef) { m_HitImmunityKoefs[hit_type] = koef; }
```

## ШАГ 2 — добавить аксессоры в `src/xrGame/Artefact.h`
В `class CArtefact`, в `public:` секцию (со строки 94), добавь:
```cpp
    float GetArtefactImmunity(ALife::EHitType t) const { return m_ArtefactHitImmunities.GetHitImmunity(t); }
    void  SetArtefactImmunity(ALife::EHitType t, float v) { m_ArtefactHitImmunities.SetHitImmunity(t, v); }
    void  SetAdditionalInventoryWeight(float w) { m_additional_weight = w; }
    // AdditionalInventoryWeight() геттер уже есть (строка 86)
```

## ШАГ 3 — объявления в `src/xrGame/script_game_object.h`
Рядом с другими `float Get.../void Set...` (например около строки 183), добавь объявления
(порядок свободный, но ровно эти 22 метода):
```cpp
    // Dead Air: artefact immunity/weight
    float GetArtefactBurnImmunity();        void SetArtefactBurnImmunity(float v);
    float GetArtefactShockImmunity();       void SetArtefactShockImmunity(float v);
    float GetArtefactChemicalBurnImmunity();void SetArtefactChemicalBurnImmunity(float v);
    float GetArtefactRadiationImmunity();   void SetArtefactRadiationImmunity(float v);
    float GetArtefactTelepaticImmunity();   void SetArtefactTelepaticImmunity(float v);
    float GetArtefactWoundImmunity();       void SetArtefactWoundImmunity(float v);
    float GetArtefactFireWoundImmunity();   void SetArtefactFireWoundImmunity(float v);
    float GetArtefactStrikeImmunity();      void SetArtefactStrikeImmunity(float v);
    float GetArtefactExplosionImmunity();   void SetArtefactExplosionImmunity(float v);
    float GetArtefactWeight();              void SetArtefactWeight(float v);
    float GetArtefactAdditionalWeight();    void SetArtefactAdditionalWeight(float v);
```

## ШАГ 4 — реализации в `src/xrGame/script_game_object_inventory_owner.cpp`
(тут уже живут DA-compat методы типа `EnableTorch2`.) Сверху добавь инклуды, если их нет:
```cpp
#include "Artefact.h"
#include "inventory_item.h"
```
Образец на ОДНУ пару (иммунитет) — повтори для всех 9 типов, подставляя EHitType из мэппинга:
```cpp
float CScriptGameObject::GetArtefactWoundImmunity()
{
    CArtefact* a = smart_cast<CArtefact*>(&object());
    if (!a) return 0.0f;
    return a->GetArtefactImmunity(ALife::eHitTypeWound);
}
void CScriptGameObject::SetArtefactWoundImmunity(float v)
{
    CArtefact* a = smart_cast<CArtefact*>(&object());
    if (!a) return;
    a->SetArtefactImmunity(ALife::eHitTypeWound, v);
}
```
Вес — 4 метода (точно так):
```cpp
float CScriptGameObject::GetArtefactWeight()
{
    CInventoryItem* i = smart_cast<CInventoryItem*>(&object());
    if (!i) return 0.0f;
    return i->Weight();
}
void CScriptGameObject::SetArtefactWeight(float v)
{
    CInventoryItem* i = smart_cast<CInventoryItem*>(&object());
    if (!i) return;
    i->SetWeight(v);
}
float CScriptGameObject::GetArtefactAdditionalWeight()
{
    CArtefact* a = smart_cast<CArtefact*>(&object());
    if (!a) return 0.0f;
    return a->AdditionalInventoryWeight();
}
void CScriptGameObject::SetArtefactAdditionalWeight(float v)
{
    CArtefact* a = smart_cast<CArtefact*>(&object());
    if (!a) return;
    a->SetAdditionalInventoryWeight(v);
}
```

## ШАГ 5 — регистрация в `src/xrGame/script_game_object_script3.cpp`
В цепочку `.def(...)` функции `script_register_game_object2` (рядом с блоком "Dead Air compat",
~строка 258) добавь 22 строки:
```cpp
        .def("get_artefact_burn_immunity", &CScriptGameObject::GetArtefactBurnImmunity) // Dead Air
        .def("set_artefact_burn_immunity", &CScriptGameObject::SetArtefactBurnImmunity)
        .def("get_artefact_shock_immunity", &CScriptGameObject::GetArtefactShockImmunity)
        .def("set_artefact_shock_immunity", &CScriptGameObject::SetArtefactShockImmunity)
        .def("get_artefact_chemical_burn_immunity", &CScriptGameObject::GetArtefactChemicalBurnImmunity)
        .def("set_artefact_chemical_burn_immunity", &CScriptGameObject::SetArtefactChemicalBurnImmunity)
        .def("get_artefact_radiation_immunity", &CScriptGameObject::GetArtefactRadiationImmunity)
        .def("set_artefact_radiation_immunity", &CScriptGameObject::SetArtefactRadiationImmunity)
        .def("get_artefact_telepatic_immunity", &CScriptGameObject::GetArtefactTelepaticImmunity)
        .def("set_artefact_telepatic_immunity", &CScriptGameObject::SetArtefactTelepaticImmunity)
        .def("get_artefact_wound_immunity", &CScriptGameObject::GetArtefactWoundImmunity)
        .def("set_artefact_wound_immunity", &CScriptGameObject::SetArtefactWoundImmunity)
        .def("get_artefact_fire_wound_immunity", &CScriptGameObject::GetArtefactFireWoundImmunity)
        .def("set_artefact_fire_wound_immunity", &CScriptGameObject::SetArtefactFireWoundImmunity)
        .def("get_artefact_strike_immunity", &CScriptGameObject::GetArtefactStrikeImmunity)
        .def("set_artefact_strike_immunity", &CScriptGameObject::SetArtefactStrikeImmunity)
        .def("get_artefact_explosion_immunity", &CScriptGameObject::GetArtefactExplosionImmunity)
        .def("set_artefact_explosion_immunity", &CScriptGameObject::SetArtefactExplosionImmunity)
        .def("get_artefact_weight", &CScriptGameObject::GetArtefactWeight)
        .def("set_artefact_weight", &CScriptGameObject::SetArtefactWeight)
        .def("set_artefact_additional_weight", &CScriptGameObject::SetArtefactAdditionalWeight)
        .def("get_artefact_additional_weight", &CScriptGameObject::GetArtefactAdditionalWeight)
```

## ШАГ 6 — compile-check (НЕ запускай игру, НЕ деплой — это сделает архитектор)
```bash
/c/msys64/usr/bin/bash.exe -l -c "export PATH='/mingw64/bin:/usr/bin:\$PATH'; cd '/d/Dead Air/xray-16/build_mingw' && cmake --build . --target xrGame -j8 2>&1 | tail -20"
```
Должно собраться без ошибок. Отдай архитектору список изменённых файлов + результат сборки.

## ЖЕЛЕЗНЫЕ правила
- Трогай ТОЛЬКО эти 5 файлов. Не рефактори соседний код.
- НЕ меняй раскладку `m_weight`/`m_additional_weight`/`m_HitImmunityKoefs` — мы их только
  читаем/пишем как рантайм-значения (они грузятся из ltx, не из save/net пакета — безопасно).
- Полную пересборку всех DLL и деплой делает архитектор. Ты — только compile-check `xrGame`.
- Если `smart_cast`/`THROW`/`CInventoryItem` не находятся (нет инклуда) — добавь инклуд, как в
  ШАГ 4; если по-прежнему не сходится — СТОП, спроси.
```
