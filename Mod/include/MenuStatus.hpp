#pragma once

#include <string>

// MenuStatus: reads spawn_menu_status.txt, which LivingBase's main.lua overwrites whenever
// keys-enabled, the world-load restore lock, or the target-lock label actually changes (see that
// file's own "SPAWN MENU STATUS" comment). This used to be the ONLY Lua <-> C++ direction in the
// whole bridge (every other file here -- SpawnMenu.cpp, MoveMenu.cpp -- only ever writes REQUESTS
// for Lua to act on) until PublishWindowVisible below (2026-08-20) added the first C++ -> Lua leg,
// for the hover-highlight feature: Lua needs to know whether this window is actually open so it
// only runs its own raycast while it matters (see that function's own comment for why C++, not
// Lua, has to be the one to answer this). Shared by both SpawnMenu and MoveMenu (and
// StandaloneWindow, which gates the whole window on IsRestoring()) so the file is only ever parsed
// in one place.

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
    // Pitch/Roll (2026-08-18, full 3-axis rotation): Unreal's own FRotator components, same as
    // TargetYaw() -- Pitch rotates around the Y axis, Roll around X, matching MoveMenu/CoordsMenu's
    // own X=Roll/Y=Pitch/Z=Yaw row convention (see MoveMenu.cpp's own comment on that mapping).
    auto TargetPitch() -> float;
    auto TargetRoll() -> float;

    // Which axis the in-game ','/'.' keys (and this window's own ','/'.' shortcut) currently
    // rotate -- "X", "Y", or "Z" -- cycled by '/' in either place via the SAME Lua-side state
    // (main.lua's rotateAxis), so keyboard and GUI can never disagree about which one is active.
    auto RotateAxis() -> const std::string&;

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

    // The FIRST C++ -> Lua leg of this bridge (2026-08-20). Writes the window's real
    // IsWindowVisible(hwnd) state to spawn_menu_window_state.txt so Lua's hover-highlight loop can
    // gate its own raycast on "is this window actually open" instead of the unrelated In-Game-Keys
    // toggle -- RedFalcon's own point: only spend that raycast's cost when the window driving the
    // feature is even up. Self-throttled to WRITE ONLY ON AN ACTUAL CHANGE (not every poll) --
    // same "don't do needless file I/O every frame" discipline as everything else in this bridge.
    // Call once per frame from StandaloneWindow's render loop, same as Poll() above; cheap no-op
    // when nothing changed.
    auto PublishWindowVisible(bool visible) -> void;
} // namespace RC::LivingBaseSpawnMenu::MenuStatus
