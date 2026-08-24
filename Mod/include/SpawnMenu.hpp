#pragma once

#include <string>

// SpawnMenu: parses LivingBase's spawn_menu.ini (see LivingBase/Scripts/spawnmenu_manifest.lua
// for the generator and the format) into a category tree. Two front-ends render it: the ImGui
// tree inside StandaloneWindow (Draw(), unchanged) and the real UMG tree inside InGamePanel
// (Phase 2, 2026-08-23, via the accessors below). The tree data, selection state, and
// write_request logic live HERE ONCE and are shared by both -- selecting/spawning from either
// surface behaves identically and neither front-end re-implements spawn_menu.ini parsing. This
// file never touches game state directly; Spawn/Replace only ever write a request file LivingBase's
// own main.lua poll loop picks up.

namespace RC::LivingBaseSpawnMenu::SpawnMenu
{
    // Re-reads spawn_menu.ini from disk and rebuilds the in-memory tree. Cheap enough to call
    // once at startup and again on demand (e.g. a manual refresh button) rather than watching the
    // file for changes.
    auto Reload() -> void;

    // Draws the tree (and a Refresh button) into the CURRENT ImGui window -- call this from
    // inside an existing ImGui::Begin()/End() pair, same as any other ImGui widget call.
    auto Draw() -> void;

    // --- Exposed for InGamePanel's Phase 2 port. MenuNode's real layout stays private to
    // SpawnMenu.cpp -- callers only ever hold a `const MenuNode&` and walk it through these
    // accessors, never construct or store one directly. ---
    struct MenuNode;

    auto RootNode() -> const MenuNode&;
    auto ChildCount(const MenuNode& node) -> int;
    auto ChildAt(const MenuNode& node, int index) -> const MenuNode&;
    auto Label(const MenuNode& node) -> const std::string&;
    auto IsLeaf(const MenuNode& node) -> bool;

    // UMG has no built-in TreeNode, so the in-game panel needs somewhere to persist "is this
    // category expanded" across rebuilds -- stored directly on the node (ImGui's own Draw() above
    // ignores it entirely; it manages expand state itself via ImGui's ID stack).
    auto IsExpanded(const MenuNode& node) -> bool;
    auto ToggleExpanded(const MenuNode& node) -> void;

    auto HasSelection() -> bool;
    auto IsSelected(const MenuNode& node) -> bool;
    auto SelectedPath() -> const std::string&;
    // full_path: the breadcrumb the caller has already accumulated while walking down to this
    // node (MenuNode itself has no parent pointer to reconstruct it from). No-op if !IsLeaf(node).
    auto SelectLeaf(const MenuNode& node, const std::string& full_path) -> void;

    auto CanSpawn() -> bool;
    // Additionally requires a locked target -- see write_request's own callers in Draw() for why.
    auto CanReplace() -> bool;
    auto SpawnSelected() -> void;
    auto ReplaceSelected() -> void;
} // namespace RC::LivingBaseSpawnMenu::SpawnMenu
