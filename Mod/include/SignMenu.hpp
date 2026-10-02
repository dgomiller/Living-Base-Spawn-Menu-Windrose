#pragma once

// SignMenu: the "Signs" tab (2026-09-29, RedFalcon's request) -- edit the text on a sign (up to four
// rows) in place of its picture. Same file-per-tab convention and file-polling bridge as the other
// tabs: writes sign_request.txt (verb on line 1: TARGET / APPLY / CLEAR; APPLY is followed by the
// four text lines) and reads sign_status.txt (SIGN_SEQ, SIGN_HAS_TARGET, SIGN_NAME, SIGN_L1..L4).
// All sign logic (targeting, drawing the text, saving per world, pruning destroyed signs) lives in
// the Lua mod's signs.lua -- this side is only the editor UI.
namespace RC::LivingBaseSpawnMenu::SignMenu
{
    // Draws into the CURRENT ImGui window -- call from inside an existing tab item.
    auto Draw() -> void;

    // True once each time the in-game Delete key was pressed (Lua bumps SIGN_TAB_SEQ in
    // sign_status.txt). Call every frame, OUTSIDE Draw() (the tab's own Draw only runs while it is the
    // active tab), and use it to force the Signs tab active via ImGuiTabItemFlags_SetSelected. The
    // first value read after start-up is only a baseline, never a request.
    auto ConsumeTabRequest() -> bool;
} // namespace RC::LivingBaseSpawnMenu::SignMenu
