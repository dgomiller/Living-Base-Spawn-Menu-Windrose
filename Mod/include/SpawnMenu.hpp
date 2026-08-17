#pragma once

// SpawnMenu: parses LivingBase's spawn_menu.ini (see LivingBase/Scripts/spawnmenu_manifest.lua
// for the generator and the format) into a category tree, and renders it as a clickable ImGui
// tree inside StandaloneWindow. Clicking a leaf only SELECTS it (highlighted, shown in a
// "Selected:" readout); the Spawn/Replace buttons below the tree write a "SPAWN:"/"REPLACE:"
// request LivingBase's own main.lua poll loop picks up and turns into a real spawn (or an
// in-place swap of the currently targeted/locked object) -- this file never touches game state
// directly.

namespace RC::LivingBaseSpawnMenu::SpawnMenu
{
    // Re-reads spawn_menu.ini from disk and rebuilds the in-memory tree. Cheap enough to call
    // once at startup and again on demand (e.g. a manual refresh button) rather than watching the
    // file for changes.
    auto Reload() -> void;

    // Draws the tree (and a Refresh button) into the CURRENT ImGui window -- call this from
    // inside an existing ImGui::Begin()/End() pair, same as any other ImGui widget call.
    auto Draw() -> void;
} // namespace RC::LivingBaseSpawnMenu::SpawnMenu
