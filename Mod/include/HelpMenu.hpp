#pragma once

// HelpMenu: pure CONTENT for the main window's "Instructions" and "History" tabs --
// "Instructions" renders an external help.txt file, lightly formatted, so it can be edited without
// a rebuild; "History" shows every message that's appeared as an on-screen toast this session, read
// from LivingBase's own spawn_menu_history.txt. Owns no window/tab bar of its own -- StandaloneWindow.cpp
// owns the single outer tab bar (Tools / Instructions / History) and calls these functions inside
// its own BeginTabItem()/EndTabItem() pairs, same division of responsibility SpawnMenu/MoveMenu
// already have with StandaloneWindow.
//
// PIVOT (2026-08-16): originally lived in a true second OS window/thread/D3D11 device
// (HelpWindow.cpp, now deleted) so it wouldn't feel crowded inside the main window's bounds. That
// second thread turned out to be a real stability problem instead -- two independent ImGui
// contexts/D3D11 devices running concurrently in the same process crashed on startup even after
// fixing the obvious GImGui data race (see the deleted ImGuiThreadGuard.hpp's history for the full
// story) -- so RedFalcon asked to fold it back into ordinary tabs in the one already-working window
// rather than keep chasing further races in a two-thread ImGui setup.
namespace RC::LivingBaseSpawnMenu::HelpMenu
{
    // Re-reads help.txt from disk and force-refreshes the history list. Call once at startup.
    auto ReloadNow() -> void;

    // Draws the "Instructions" tab's content -- call from inside an existing
    // BeginTabItem("Instructions")/EndTabItem() pair.
    auto DrawInstructionsTab() -> void;

    // Draws the "History" tab's content -- call from inside an existing
    // BeginTabItem("History")/EndTabItem() pair. Self-throttle-polls spawn_menu_history.txt on its
    // own, so History stays live even without another ReloadNow() call.
    auto DrawHistoryTab() -> void;
} // namespace RC::LivingBaseSpawnMenu::HelpMenu
