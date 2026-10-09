#pragma once

#include <string>
#include <vector>

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

    // Public, ImGui-agnostic snapshot of the "Custom > Poses" subtree (2026-09-16, RedFalcon:
    // "bring over the poses... have it display a similar tree view to the tools, but have it
    // contain only everything under the poses branch") -- lets CustomMenu.cpp's own "Poses and
    // Actions" windowshade render the identical category tree spawnmenu_manifest.lua's
    // custom_poses_path_and_label already produces here, without exposing this file's private
    // MenuNode/INI-parsing internals. Rebuilt every Reload() alongside the main tree. A node's own
    // `children` ARE its sub-categories (or, for a leaf, empty) -- the root returned by
    // GetPosesTree() has "Custom"/"Poses" already stripped, so its own children are the top
    // categories (Standing/Combat/Monsterous/Misc/...).
    struct PoseNode
    {
        std::string label;
        std::vector<PoseNode> children;
        bool is_leaf = false;
        int index = 0; // <roster>[index] -- meaningless unless is_leaf
        // `roster` (2026-10-08 fix): every pose used to implicitly be Config.CUSTOM_POSES, but
        // spawnmenu_manifest.lua's Custom-*.ini loader can now generate OTHER pose rosters too
        // (CUSTOMFILE_<file>_POSE) living in the same "Custom > Poses" tree. Without this field,
        // ApplyPoseByIndex had no way to know which roster a given leaf actually came from, and
        // hardcoding "CUSTOM_POSES" meant every custom pose applied the wrong (built-in) pose at
        // whatever index happened to match. Meaningless unless is_leaf.
        std::string roster;
    };

    // Root of the Poses subtree. Empty children if spawn_menu.ini has no Poses branch (yet).
    auto GetPosesTree() -> const PoseNode&;

    // Same idea, "Custom > Hand" (2026-10-08, RedFalcon: collapse the Left/Right Hand item
    // dropdowns into one tree with "+" leaves, same UX as Poses). A leaf's own `label` IS the
    // friendlyName Spawner.ApplySocketItemManual resolves by -- `index`/`roster` are unused here,
    // CustomMenu.cpp's "+" button writes a HANDITEM request with the leaf's label directly.
    auto GetHandItemsTree() -> const PoseNode&;

    // Applies <roster>[index]'s animation to whatever's currently target-locked -- the exact same
    // "REPLACE:<roster>:index" request a Tools-tab tree click on a Poses leaf would write,
    // factored out so CustomMenu.cpp's own per-leaf "+" buttons (and its pose-reset "X") can fire
    // it directly without duplicating this file's private write_request/REQUEST_PATH. `roster` is
    // REQUIRED now (2026-10-08) -- always pass the PoseNode's own `roster` field, never a literal
    // "CUSTOM_POSES", or a custom pose will silently apply the wrong animation again.
    auto ApplyPoseByIndex(const std::string& roster, int index) -> void;
} // namespace RC::LivingBaseSpawnMenu::SpawnMenu
