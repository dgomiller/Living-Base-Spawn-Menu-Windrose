#pragma once

// CustomMenu: the "Custom" tab's cloth-color panel (2026-09-08, RedFalcon's request) -- a
// target-gated list of 7 garment categories (Torso/Legs/Waist/Hands/Feet/Hat/Cape), each with a
// clickable gradient-swatch button that opens a picker over the real 24-entry cloth palette
// (decoded this same session -- see WINDROSE_MODDING_NOTES.md's cloth-palette addendum and the
// published "Cloth Color Palette" artifact for where kClothColors below comes from), plus a single
// Apply button that writes every category the user actually picked a color for to
// custom_color_request.txt in one shot. Same "Selected Target" readout as MoveMenu.cpp (reads
// MenuStatus::TargetLabel()) so it's always clear which NPC an Apply click will affect -- and the
// whole panel is gated on a target being locked, same reasoning as MoveMenu's own hasTarget gate.
//
// Deliberately ONE swatch per category, not separate Color1/Color2/Color3 pickers -- see
// Config.CUSTOM_TAB_CLOTH_CATEGORIES's own comment (config.lua) for why writing the chosen index
// to all 3 CPD slots uniformly is safe for every category here. This file only ever SIGNALS
// intent (which category, which palette index) -- main.lua's pollCustomColorRequest is what
// actually calls Spawner.TestSetCPDPaletteColor, same division of responsibility every other panel
// in this bridge already follows (SpawnMenu/MoveMenu headers have the fuller reasoning).
namespace RC::LivingBaseSpawnMenu::CustomMenu
{
    // Draws the panel into the CURRENT ImGui window -- call from inside an existing
    // BeginTabItem("Custom")/EndTabItem() pair, same convention as SpawnMenu::Draw()/MoveMenu::Draw().
    auto Draw() -> void;
} // namespace RC::LivingBaseSpawnMenu::CustomMenu
