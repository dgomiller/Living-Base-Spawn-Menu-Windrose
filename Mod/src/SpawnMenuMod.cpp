#include <SpawnMenuMod.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <InGamePanel.hpp>
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

        InGamePanel::Start();
        // TEMP Phase 0 spike key. F9 collided with ModManager's own MenuKey (confirmed live
        // 2026-08-22) -- LivingBaseEnhanced's own config.lua already documents staying off the
        // F-row entirely for exactly this reason. SCROLL_LOCK (tried next) produced zero log
        // output at all when pressed -- Toggle() was never entered, so the key press never
        // reached this handler (most likely no physical Scroll Lock key on the test machine, or a
        // toggle-key quirk in the input hook -- not investigated further, just avoided). Plain
        // letter keys DO work via this native register_keydown_event API despite
        // LivingBaseEnhanced's own docs noting they don't bind via UE4SS's separate LUA
        // RegisterKeyBind -- confirmed by UE4SS's own internal usage (UE4SSProgram.cpp:
        // Ctrl+Y, Ctrl+NumPad9). 'P' is unused by every mod in this install (audited
        // ModManager=F9, ConsoleEnabler=Tilde/F10, LivingBaseEnhanced=numpad/OEM/arrows/F5-F8/
        // INS/DEL/HOME/END/PAUSE, nothing else binds a bare letter). Not the final keybind; Phase
        // 4 folds real activation into the window-open/close flow the rest of this mod already
        // uses.
        register_keydown_event(Input::Key::P, [] { InGamePanel::Toggle(); });
        // NOTE (2026-08-23): register_keydown_event(Input::Key::LEFT_MOUSE_BUTTON, ...) was tried
        // here for the in-game panel's click detection and REMOVED -- confirmed live it never
        // fires either, because UE4SS's own Win32AsyncInputSource polls GetAsyncKeyState(key) for
        // EVERY subscribed key including mouse buttons (same underlying call a raw poll already
        // proved this game doesn't expose mouse state through). See InGamePanel.cpp's file header
        // for the click-detection mechanism actually in use now.
    }

    auto SpawnMenuMod::on_update() -> void
    {
        if (!m_logged_first_update)
        {
            m_logged_first_update = true;
            Output::send<LogLevel::Normal>(STR("[LivingBaseSpawnMenu] on_update firing\n"));
        }
        InGamePanel::Tick();
    }
} // namespace RC::LivingBaseSpawnMenu
