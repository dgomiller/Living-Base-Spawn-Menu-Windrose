#include <SpawnMenuMod.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <StandaloneWindow.hpp>

// PIVOT (2026-08-16): the raw D3D12 Present-hook overlay (GameOverlay.cpp/hpp) is shelved, not
// deleted -- it's real, hard-won findings (x64Detour over VFuncSwapHook for Steam-overlay safety,
// CreateSwapChainForHwnd interception for the real queue/swapchain, and the conclusive live-catch
// diagnosis that Windrose's NVIDIA Streamline DLSS-G swapchain wrapper conflicts with a naive
// raw-back-buffer overlay: DXGI_ERROR_DEVICE_REMOVED at Streamline's own present call). Confirmed
// even a plain -slnoswapchainprovider launch flag doesn't clear it, so this needs real Streamline
// SDK integration (their own kFeatureImGUI hook point) to do safely -- future work, not abandoned.
//
// register_tab (UE4SS's shared console) was tried next and confirmed working/gameplay-safe, but
// puts our content inside UE4SS's own devtools window alongside its other tabs (Live View, Lua
// debugger, etc.) -- not a clean player-facing window. Landed on StandaloneWindow instead: our
// own dedicated Win32+D3D11+ImGui window, on its own thread, sharing nothing with either
// Windrose's D3D12 pipeline or UE4SS's own GUI thread/context -- see StandaloneWindow.cpp for the
// implementation and its own notes on ImGui context independence.

namespace RC::LivingBaseSpawnMenu
{
    SpawnMenuMod::SpawnMenuMod()
    {
        ModName = STR("LivingBaseSpawnMenu");
        ModAuthors = STR("RedFalcon");
        ModDescription = STR("Category-based spawn/movement menu companion for LivingBase.");
        ModVersion = STR("3.0.0");

        Output::send<LogLevel::Normal>(STR("[LivingBaseSpawnMenu] constructed\n"));
    }

    auto SpawnMenuMod::on_unreal_init() -> void
    {
        Output::send<LogLevel::Normal>(STR("[LivingBaseSpawnMenu] on_unreal_init fired\n"));
        StandaloneWindow::Start();
    }

    auto SpawnMenuMod::on_update() -> void
    {
        if (!m_logged_first_update)
        {
            m_logged_first_update = true;
            Output::send<LogLevel::Normal>(STR("[LivingBaseSpawnMenu] on_update firing\n"));
        }
    }
} // namespace RC::LivingBaseSpawnMenu
