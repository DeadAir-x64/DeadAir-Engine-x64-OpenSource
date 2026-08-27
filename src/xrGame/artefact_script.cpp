#include "pch_script.h"
#include "Artefact.h"
#include "MercuryBall.h"
#include "GraviArtifact.h"
#include "BlackDrops.h"
#include "Needles.h"
#include "BastArtifact.h"
#include "BlackGraviArtifact.h"
#include "DummyArtifact.h"
#include "ZudaArtifact.h"
#include "ThornArtifact.h"
#include "FadedBall.h"
#include "ElectricBall.h"
#include "RustyHairArtifact.h"
#include "GalantineArtifact.h"
#include "cta_game_artefact.h"

void CArtefact::script_register(lua_State* luaState)
{
    using namespace luabind;

    module(luaState)
    [
        class_<CArtefact, CGameObject>("CArtefact")
            .def(constructor<>())
            .def("FollowByPath", &CArtefact::FollowByPath)
            .def("SwitchVisibility", &CArtefact::SwitchVisibility)
            .def("GetAfRank", &CArtefact::GetAfRank)
            // [DA_PORT] Прибавка к переносимому весу от артефакта.
            //
            // В движке метод был всегда (Artefact.h, зовётся из Actor_Movement при подсчёте
            // предела веса), а привязки к скриптам не имел. Наткнулись на это переносом
            // паркура: demonized_ledge_grabbing считает вес снаряжения и падал с "attempt to
            // call method 'AdditionalInventoryWeight' (a nil value)" на первом же артефакте
            // на поясе.
            .def("AdditionalInventoryWeight", &CArtefact::AdditionalInventoryWeight),

        class_<CMercuryBall, CArtefact>("CMercuryBall")
            .def(constructor<>()),
        class_<CBlackDrops, CArtefact>("CBlackDrops")
            .def(constructor<>()),
        class_<CBlackGraviArtefact, CArtefact>("CBlackGraviArtefact")
            .def(constructor<>()),
        class_<CBastArtefact, CArtefact>("CBastArtefact")
            .def(constructor<>()),
        class_<CDummyArtefact, CArtefact>("CDummyArtefact")
            .def(constructor<>()),
        class_<CZudaArtefact, CArtefact>("CZudaArtefact")
            .def(constructor<>()),
        class_<CThornArtefact, CArtefact>("CThornArtefact")
            .def(constructor<>()),
        class_<CFadedBall, CArtefact>("CFadedBall")
            .def(constructor<>()),
        class_<CElectricBall, CArtefact>("CElectricBall")
            .def(constructor<>()),
        class_<CRustyHairArtefact, CArtefact>("CRustyHairArtefact")
            .def(constructor<>()),
        class_<CGalantineArtefact, CArtefact>("CGalantineArtefact")
            .def(constructor<>()),
        class_<CGraviArtefact, CArtefact>("CGraviArtefact")
            .def(constructor<>())
    ];
}
