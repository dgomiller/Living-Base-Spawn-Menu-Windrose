#pragma once

// CoordsMenu: a small auxiliary window for precisely editing the target-locked object's exact
// X/Y/Z position AND full X/Y/Z rotation (2026-08-18 -- was Yaw-only) by typing numbers, rather
// than nudging with buttons. The three ROTATION fields are Unreal's Roll/Pitch/Yaw under the hood
// (X=Roll, Y=Pitch, Z=Yaw -- Unreal's own FRotator axis convention, matching MoveMenu's identical
// X/Y/Z row labeling), each shown normalized to [0, 360) instead of Unreal's native (-180, 180] --
// RedFalcon found the native range's wrap through 180/-180 confusing in this window (2026-08-16);
// see NormalizeAngle360()/AngleDelta() in CoordsMenu.cpp (generalized from the old Yaw-only
// NormalizeYaw360()/YawDelta() to cover all three rotation fields, not just one) for the
// conversion and the wraparound-safe comparison it requires. Four actions -- Preview (move
// now, stay open), Apply (move now, close), Reset (move back to opening position, stay open),
// Cancel (move back to opening position, close) -- all funnel through Spawner.SetLockedTargetTransform
// on the Lua side via a "COORDS_MOVE:x:y:z:pitch:yaw:roll" request. See main.lua's own
// handleMoveMenuCoords* comments and Spawner.suspendTargetLockDistanceCheck's comment in
// spawner.lua for the full design (this window suspends the target-lock's normal "walked too far,
// release" check for as long as it's open, since a typo'd coordinate is a real risk of that firing
// mid-edit).

namespace RC::LivingBaseSpawnMenu::CoordsMenu
{
    // Opens the window, snapshotting the currently-locked target's transform (via MenuStatus) as
    // both the starting field values and the "revert to this" anchor for Reset/Cancel. Sends
    // ACTION:COORDS_OPEN. Only call while MenuStatus::TargetLabel() is non-empty.
    auto Open() -> void;

    // Draws the window if open; a complete no-op otherwise -- safe to call unconditionally every
    // frame from StandaloneWindow's render loop.
    auto Draw() -> void;
} // namespace RC::LivingBaseSpawnMenu::CoordsMenu
