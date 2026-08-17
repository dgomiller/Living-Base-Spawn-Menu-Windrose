#pragma once

#include <string>

// MenuStatus: reads spawn_menu_status.txt, which LivingBase's main.lua overwrites whenever
// keys-enabled, the world-load restore lock, or the target-lock label actually changes (see that
// file's own "SPAWN MENU STATUS" comment). This is the one Lua -> C++ direction in the whole
// bridge -- every other file here (SpawnMenu.cpp, MoveMenu.cpp) only ever writes REQUESTS for Lua
// to act on. Shared by both SpawnMenu and MoveMenu (and StandaloneWindow, which gates the whole
// window on IsRestoring()) so the file is only ever parsed in one place.

namespace RC::LivingBaseSpawnMenu::MenuStatus
{
    // Re-reads spawn_menu_status.txt if enough time has passed since the last read (self-throttled
    // -- call this once per frame from StandaloneWindow's render loop, no need for callers to pace
    // it themselves).
    auto Poll() -> void;

    auto IsEnabled() -> bool;
    auto IsRestoring() -> bool;
    auto TargetLabel() -> const std::string&;

    // A per-actor-instance unique identity (GetFullName() on the Lua side), NOT the same as
    // TargetLabel() -- the label is a cosmetic display string that two different actors can share
    // (two identically-dressed Senkamati, two identical decor props), so it's not reliable for
    // detecting "did the lock move to a DIFFERENT object". Use this for that; use TargetLabel()
    // only for display.
    auto TargetId() -> const std::string&;

    // Live transform of the currently locked target (all zero/meaningless when TargetLabel() is
    // empty -- callers must check that first). Used by CoordsMenu to populate its fields with a
    // snapshot the instant its window opens.
    auto TargetX() -> float;
    auto TargetY() -> float;
    auto TargetZ() -> float;
    auto TargetYaw() -> float;

    // Bumped by main.lua every time '-' is pressed (see Config.KEYS.toggleWindow's own comment) --
    // StandaloneWindow compares this against the last value it saw each frame and flips its own
    // visibility whenever it changes. A monotonic counter rather than an explicit OPEN/CLOSE state
    // so an out-of-order or missed read can never desync C++'s idea of "open" from Lua's -- C++
    // already owns visibility outright (Lua has no way to query it back), so it only needs to know
    // "something changed," not "what the new state is."
    auto WindowToggleSeq() -> int;

    // Bumped by main.lua every '=' press (see Config.KEYS.releaseMouse's own comment) --
    // StandaloneWindow calls SetForegroundWindow() on itself when this changes, IF currently
    // visible. Same monotonic-counter shape as WindowToggleSeq() and for the same reason: C++
    // doesn't need to know anything except "this happened."
    auto FocusStealSeq() -> int;
} // namespace RC::LivingBaseSpawnMenu::MenuStatus
