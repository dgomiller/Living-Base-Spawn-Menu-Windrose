#pragma once

// StandaloneWindow: our own dedicated Win32 + D3D11 + ImGui window, independent of both
// Windrose's own D3D12 swapchain (see GameOverlay.hpp for why that route is shelved -- NVIDIA
// Streamline DLSS-G conflict) and UE4SS's own shared DebuggingGUI console (register_tab puts our
// content in the same window as UE4SS's other dev tools, not a clean player-facing window).
//
// Runs on its own dedicated thread with its own message pump, device, and ImGui context --
// completely separate from the game's rendering and from UE4SS's own GUI thread, so it carries
// none of the D3D12/Streamline risk the shelved GameOverlay approach did.

namespace RC::LivingBaseSpawnMenu::StandaloneWindow
{
    auto Start() -> void;
    auto Stop() -> void;

    // Hands OS input focus back to whatever window had it immediately before this window last
    // stole focus (open via '-', or an explicit '=' steal) -- in practice, the game itself, since
    // stealing focus is the only way this window's ImGui content can be clicked at all. Called by
    // SpawnMenu after a Spawn/Replace request is sent, so placing an item doesn't leave the player
    // stuck alt-tabbed into this window. A no-op if nothing was ever stolen from, or if that window
    // is no longer valid.
    auto ReturnFocusToGame() -> void;
} // namespace RC::LivingBaseSpawnMenu::StandaloneWindow
