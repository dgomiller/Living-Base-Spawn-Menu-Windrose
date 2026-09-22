#pragma once

// BarbieMenu: the two thumbnail-only picker grids designed 2026-09-08 (see
// project_livingbase_spawn_menu memory) and captured 2026-09-11 -- a Body Type grid (14 cells: 7
// shapes x Male/Female) and an Origin grid (8 families x Male/Female, sex columns are preview-only,
// either selects the same origin value). Selecting one cell from each, then pressing Spawn,
// combines both and writes a request LivingBase's own main.lua picks up -- same file-bridge
// pattern as every other panel in this mod (see SpawnMenu.cpp's own header comment), never touches
// game state directly. Lives inside the "Custom" tab, alongside CustomMenu's cloth-color panel.
namespace RC::LivingBaseSpawnMenu::BarbieMenu
{
    // Draws both grids + the Spawn button into the CURRENT ImGui window -- call this from inside an
    // existing ImGui::Begin()/End() pair, same as any other panel's Draw().
    auto Draw() -> void;

    // Full Body/Face View/orbit rotate buttons for whatever's currently target-locked (2026-09-16,
    // moved out of Draw() -- RedFalcon: "Move the camera buttons to the right of the target window"
    // -- this now lives in CustomMenu::DrawTargetHeader() instead, positioned there so "Face View"
    // lines up with "Read Current"). Left in BarbieMenu.cpp/this namespace rather than a shared
    // header because its own state (g_zoomMode, kPreviewSize, the write-request helpers) is all
    // file-local here and none of it is needed anywhere else.
    auto DrawCameraControls() -> void;
} // namespace RC::LivingBaseSpawnMenu::BarbieMenu
