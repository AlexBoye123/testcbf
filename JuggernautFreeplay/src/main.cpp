#include <Geode/Geode.hpp>
#include "CorridorGenerator.hpp"

using namespace geode::prelude;

$on_mod(Loaded) {
    log::info("=================================================");
    log::info(" Juggernaut Freeplay (JFP) v1.7.0 Loaded ");
    log::info(" Target: Geometry Dash 2.2081 | Geode SDK v5.8.2 ");
    log::info("=================================================");

    listenForSettingChanges<std::string>("corridor-rule", [](std::string val) {
        if (val == "Juggernaut") JFP::CorridorGenerator::get().setRule(JFP::CorridorRule::Juggernaut);
        else if (val == "Classic") JFP::CorridorGenerator::get().setRule(JFP::CorridorRule::Classic);
        else if (val == "Narrow Slopes") JFP::CorridorGenerator::get().setRule(JFP::CorridorRule::NarrowSlopes);
        else if (val == "Nine Circles") JFP::CorridorGenerator::get().setRule(JFP::CorridorRule::NineCircles);
    });

    listenForSettingChanges<bool>("speed-portals", [](bool val) {
        JFP::CorridorGenerator::get().setSpeedPortals(val);
    });

    listenForSettingChanges<bool>("gravity-portals", [](bool val) {
        JFP::CorridorGenerator::get().setGravityPortals(val);
    });

    listenForSettingChanges<bool>("remove-spam", [](bool val) {
        JFP::CorridorGenerator::get().setRemoveSpam(val);
    });
}
