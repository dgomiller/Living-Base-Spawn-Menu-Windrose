#pragma once

// GameOverlay: hooks the game's real IDXGISwapChain3::Present1 (D3D12, confirmed via the
// Windrose install's own D3D12/x64 PSO cache folder) so our ImGui panel can render directly
// into the same frame the player sees, instead of UE4SS's own separate debug console window
// (register_tab renders into a distinct OS window/thread -- confirmed by reading GUI.hpp/DX11.cpp
// -- which can't do the "see the object while nudging it" design this menu needs).
//
// Phase 4a scope: install the hook and confirm it fires + calls through correctly. No ImGui
// rendering yet -- that's added once the hook itself is proven safe, same "prove each layer"
// discipline as Phase 3.

namespace RC::LivingBaseSpawnMenu::GameOverlay
{
    auto Install() -> bool;

    // Poll-only, read from on_update() (a context already confirmed safe to log from) -- the
    // hook itself only flips a bare atomic flag, no logging/engine calls on the render thread.
    auto HasPresentFired() -> bool;
} // namespace RC::LivingBaseSpawnMenu::GameOverlay
