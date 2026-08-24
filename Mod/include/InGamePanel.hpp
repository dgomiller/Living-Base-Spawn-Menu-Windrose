#pragma once

// InGamePanel: the in-game-viewport UMG replacement for StandaloneWindow (see
// HANDOFF_INGAME_PANEL.md at the repo root for the full plan/rationale). StandaloneWindow itself
// is left running unmodified alongside this -- same "shelve, don't delete" precedent as
// GameOverlay.cpp -- until this proves out.
//
// Phase 0 (current): prove real UMG widgets can be constructed from C++ (StaticConstructObject
// via the same primitives HUDControl's Lua uses, just called natively) and toggled visible/hidden
// safely, before anything else is built on top of it. No spawn tree / move panel content yet --
// just an empty shell with a visible marker.

namespace RC::LivingBaseSpawnMenu::InGamePanel
{
    // Called once from SpawnMenuMod::on_unreal_init(). Does NOT construct the widget yet --
    // GameInstance may not be ready this early, and the widget is cheap to build lazily on first
    // Toggle() instead.
    auto Start() -> void;

    // Builds the panel on first call (if not already built/valid), then flips it
    // visible/hidden. Bound to a toggle key by SpawnMenuMod (register_keydown_event lives on the
    // CppUserModBase instance, not here).
    auto Toggle() -> void;

    // Call once per frame from SpawnMenuMod::on_update() (Phase 2, 2026-08-23) -- polls the Lua
    // status bridge and refreshes Spawn/Replace's enabled state while the panel is visible.
    auto Tick() -> void;
} // namespace RC::LivingBaseSpawnMenu::InGamePanel
