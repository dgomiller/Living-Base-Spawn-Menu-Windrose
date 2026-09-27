#pragma once

// TargetListMenu: the "Target List" tab (2026-09-26, RedFalcon's request) -- scans LivingBase's
// own tracked actors (Spawner.spawned) within a chosen radius, filtered by the 4 top-level
// category checkboxes (People/Monsterous/Animals/Decor), lists them nearest-first, and lets a
// specific row be target-locked directly by index -- unlike the numpad/Move-tab "+" lock, which
// only ever grabs whatever's hovered in the camera's own forward cone, this can lock something out
// of view entirely (RedFalcon's own point: "to help locate it if its unknown where it is").
//
// Same file-per-tab convention as SpawnMenu/MoveMenu/CustomMenu; same file-polling bridge (writes
// target_list_request.txt for both "SCAN" and "TARGET" actions, reads target_list_status.txt for
// scan results) -- see this file's own .cpp for the exact wire format. The live "Selected Target"
// box and the continuously-updating distance readout both reuse MenuStatus's existing TARGET_*
// fields (TargetLabel/TargetDistMeters) rather than anything scan-specific, since a Target List
// selection sets Spawner.lockedTarget the same way Num+ does (see Spawner.TargetListSelect's own
// header comment in spawner.lua).
namespace RC::LivingBaseSpawnMenu::TargetListMenu
{
    // Draws into the CURRENT ImGui window/column -- call this from inside an existing
    // ImGui::Begin()/End() pair, same as SpawnMenu::Draw()/MoveMenu::Draw().
    auto Draw() -> void;
} // namespace RC::LivingBaseSpawnMenu::TargetListMenu
