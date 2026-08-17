#include <StandaloneWindow.hpp>

#include <CoordsMenu.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <HelpMenu.hpp>
#include <MenuStatus.hpp>
#include <MoveMenu.hpp>
#include <SpawnMenu.hpp>

#include <atomic>
#include <thread>

#include <d3d11.h>
#include <wrl/client.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

using Microsoft::WRL::ComPtr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace RC::LivingBaseSpawnMenu::StandaloneWindow
{
    namespace
    {
        std::thread g_thread;
        std::atomic_bool g_stop_requested{};

        ComPtr<ID3D11Device> g_device;
        ComPtr<ID3D11DeviceContext> g_device_context;
        ComPtr<IDXGISwapChain> g_swap_chain;
        ComPtr<ID3D11RenderTargetView> g_render_target_view;
        UINT g_resize_width{};
        UINT g_resize_height{};

        auto CreateRenderTarget() -> void
        {
            ComPtr<ID3D11Texture2D> back_buffer;
            g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
            g_device->CreateRenderTargetView(back_buffer.Get(), nullptr, &g_render_target_view);
        }

        auto CleanupRenderTarget() -> void
        {
            g_render_target_view.Reset();
        }

        auto CreateDeviceD3D(HWND hwnd) -> bool
        {
            DXGI_SWAP_CHAIN_DESC sd{};
            sd.BufferCount = 2;
            sd.BufferDesc.Width = 0;
            sd.BufferDesc.Height = 0;
            sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.BufferDesc.RefreshRate.Numerator = 60;
            sd.BufferDesc.RefreshRate.Denominator = 1;
            sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.OutputWindow = hwnd;
            sd.SampleDesc.Count = 1;
            sd.SampleDesc.Quality = 0;
            sd.Windowed = TRUE;
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

            D3D_FEATURE_LEVEL feature_level{};
            const D3D_FEATURE_LEVEL feature_levels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
            HRESULT hr = D3D11CreateDeviceAndSwapChain(
                    nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, feature_levels, 2, D3D11_SDK_VERSION, &sd, &g_swap_chain, &g_device, &feature_level, &g_device_context);
            if (hr == DXGI_ERROR_UNSUPPORTED)
            {
                hr = D3D11CreateDeviceAndSwapChain(
                        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, feature_levels, 2, D3D11_SDK_VERSION, &sd, &g_swap_chain, &g_device, &feature_level, &g_device_context);
            }
            if (FAILED(hr))
            {
                return false;
            }

            CreateRenderTarget();
            return true;
        }

        LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
        {
            if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
            {
                return true;
            }

            switch (msg)
            {
            case WM_SIZE:
                if (wparam != SIZE_MINIMIZED)
                {
                    g_resize_width = LOWORD(lparam);
                    g_resize_height = HIWORD(lparam);
                }
                return 0;
            case WM_SYSCOMMAND:
                if ((wparam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
                {
                    return 0;
                }
                break;
            case WM_CLOSE:
                // Hide instead of destroying -- matches the "toggle it closed, keep playing,
                // toggle it back open" workflow this window is meant for, not a one-shot app.
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }

        auto ThreadMain() -> void
        {
            WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
            wc.style = CS_CLASSDC;
            wc.lpfnWndProc = WndProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"LivingBaseSpawnMenu_StandaloneWindow";
            RegisterClassExW(&wc);

            // WS_EX_TOPMOST: keep this window above the game (and everything else) at all times --
            // RedFalcon's request, since it's meant to stay visible/usable while playing rather than
            // getting buried behind the game window the moment it loses focus.
            // Default size (2026-08-16): 700x360 opened too short (cut off the move panel's
            // content); a first attempt at 780x640 overcorrected and opened too tall; 560 was a
            // re-derived estimate from the move panel's own content stack. Bumped to 780x590 when
            // Instructions/History were folded in as tabs alongside Tools (2026-08-16 pivot away
            // from a separate Help window, see HelpMenu.hpp) -- the extra ~30px accounts for the
            // tab bar itself. Still just a starting size, the native window's own resize border
            // works normally from here if this needs further tuning.
            HWND hwnd = CreateWindowExW(WS_EX_TOPMOST,
                                         wc.lpszClassName,
                                         L"LivingBase Spawn Menu",
                                         WS_OVERLAPPEDWINDOW,
                                         100,
                                         100,
                                         780,
                                         590,
                                         nullptr,
                                         nullptr,
                                         wc.hInstance,
                                         nullptr);

            if (!CreateDeviceD3D(hwnd))
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] StandaloneWindow: CreateDeviceD3D failed\n"));
                DestroyWindow(hwnd);
                UnregisterClassW(wc.lpszClassName, wc.hInstance);
                return;
            }

            // Starts HIDDEN (2026-08-16, RedFalcon's request) -- toggled open/closed via '-' from
            // ANYWHERE (in-game, always active regardless of the In-Game Keys toggle -- see
            // main.lua's Config.KEYS.toggleWindow), not shown by default the way it used to be.
            // See the WindowToggleSeq() check in the render loop below for the actual open/close.
            ShowWindow(hwnd, SW_HIDE);
            UpdateWindow(hwnd);

            // Deliberately our OWN ImGui context, never UE4SS's shared one (no
            // UE4SS_ENABLE_IMGUI() here) -- this runs on its own thread with its own device, so it
            // stays fully independent of UE4SS's own GUI thread. This is now the ONLY thread in
            // this DLL that ever touches ImGui (Instructions/History are tabs in this same window
            // as of the 2026-08-16 pivot, not a second OS window/thread/D3D11 device the way
            // HelpWindow.cpp used to be) -- so SetCurrentContext() only needs to run once here,
            // there's no other thread left to race GImGui against.
            IMGUI_CHECKVERSION();
            ImGuiContext* imgui_context = ImGui::CreateContext();
            ImGui::SetCurrentContext(imgui_context);
            ImGui::StyleColorsDark();

            // Light styling pass over the stock dark theme -- softer rounded corners/edges, a bit
            // more breathing room, and a warm gold accent (fitting the pirate setting) on
            // interactive elements instead of ImGui's default flat blue. Deliberately modest: this
            // is still a plain ImGui tool window, not a custom-drawn game menu -- see this project's
            // own notes on what ImGui can/can't reasonably become.
            {
                ImGuiStyle& style = ImGui::GetStyle();
                style.WindowRounding = 6.0f;
                style.ChildRounding = 4.0f;
                style.FrameRounding = 4.0f;
                style.PopupRounding = 4.0f;
                style.ScrollbarRounding = 6.0f;
                style.GrabRounding = 4.0f;
                style.TabRounding = 4.0f;
                style.WindowPadding = ImVec2(10.0f, 10.0f);
                style.FramePadding = ImVec2(8.0f, 4.0f);
                style.ItemSpacing = ImVec2(8.0f, 6.0f);

                ImVec4* colors = style.Colors;
                const ImVec4 gold = ImVec4(0.72f, 0.53f, 0.15f, 1.00f);
                const ImVec4 goldHover = ImVec4(0.85f, 0.64f, 0.20f, 1.00f);
                const ImVec4 goldActive = ImVec4(0.60f, 0.44f, 0.10f, 1.00f);
                // FrameBg (slider/input/checkbox backgrounds) was left at stock ImGui dark-blue,
                // which clashed against the gold SliderGrab above (a two-tone blue-track/gold-nub
                // look, confirmed live 2026-08-16) -- warm dark-brown instead, consistent with the
                // rest of the theme.
                colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.11f, 0.06f, 1.00f);
                colors[ImGuiCol_FrameBgHovered] = ImVec4(gold.x, gold.y, gold.z, 0.35f);
                colors[ImGuiCol_FrameBgActive] = ImVec4(gold.x, gold.y, gold.z, 0.55f);
                colors[ImGuiCol_Header] = ImVec4(gold.x, gold.y, gold.z, 0.45f);
                colors[ImGuiCol_HeaderHovered] = ImVec4(goldHover.x, goldHover.y, goldHover.z, 0.65f);
                colors[ImGuiCol_HeaderActive] = ImVec4(goldActive.x, goldActive.y, goldActive.z, 0.80f);
                colors[ImGuiCol_Button] = ImVec4(gold.x, gold.y, gold.z, 0.55f);
                colors[ImGuiCol_ButtonHovered] = goldHover;
                colors[ImGuiCol_ButtonActive] = goldActive;
                colors[ImGuiCol_CheckMark] = goldHover;
                colors[ImGuiCol_SliderGrab] = gold;
                colors[ImGuiCol_SliderGrabActive] = goldHover;
                colors[ImGuiCol_Tab] = ImVec4(gold.x, gold.y, gold.z, 0.35f);
                colors[ImGuiCol_TabHovered] = goldHover;
                colors[ImGuiCol_TabSelected] = ImVec4(gold.x, gold.y, gold.z, 0.60f);
                colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.12f, 0.06f, 1.00f);
                colors[ImGuiCol_ResizeGrip] = ImVec4(gold.x, gold.y, gold.z, 0.30f);
                colors[ImGuiCol_ResizeGripHovered] = goldHover;
                colors[ImGuiCol_ResizeGripActive] = goldActive;
            }

            ImGui_ImplWin32_Init(hwnd);
            ImGui_ImplDX11_Init(g_device.Get(), g_device_context.Get());

            SpawnMenu::Reload();
            HelpMenu::ReloadNow();

            Output::send<LogLevel::Normal>(STR("[LivingBaseSpawnMenu] StandaloneWindow: running\n"));

            while (!g_stop_requested.load())
            {
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                    if (msg.message == WM_QUIT)
                    {
                        g_stop_requested.store(true);
                    }
                }
                if (g_stop_requested.load())
                {
                    break;
                }

                if (g_resize_width != 0 && g_resize_height != 0)
                {
                    CleanupRenderTarget();
                    g_swap_chain->ResizeBuffers(0, g_resize_width, g_resize_height, DXGI_FORMAT_UNKNOWN, 0);
                    g_resize_width = 0;
                    g_resize_height = 0;
                    CreateRenderTarget();
                }

                // Self-throttled (at most every 300ms, see MenuStatus.cpp) -- cheap to call every
                // frame regardless.
                MenuStatus::Poll();

                // '-' toggles this window open/closed WHILE PLAYING (see MenuStatus::WindowToggleSeq()'s
                // own comment) -- a changed sequence number means flip visibility, regardless of what
                // it changed TO, since C++ owns the actual open/closed state and Lua has no way to
                // query it back. Runs even while hidden, since this whole loop (message pump +
                // MenuStatus::Poll()) keeps going regardless of ShowWindow state.
                // SetForegroundWindow() on the show branch ONLY (2026-08-16, RedFalcon: "let's let the
                // window steal focus on spawn as likely that would be wanted") -- a real, accepted
                // tradeoff: it steals OS focus from the game, so a second '-' press while still
                // playing (game no longer focused) can't reach RegisterKeyBind to close it again that
                // way -- covered instead by the local ImGui key check right below, which needs no
                // game focus at all.
                {
                    static int last_seen_toggle_seq = 0;
                    int toggle_seq = MenuStatus::WindowToggleSeq();
                    if (toggle_seq != last_seen_toggle_seq)
                    {
                        last_seen_toggle_seq = toggle_seq;
                        if (IsWindowVisible(hwnd))
                        {
                            ShowWindow(hwnd, SW_HIDE);
                        }
                        else
                        {
                            ShowWindow(hwnd, SW_SHOW);
                            SetForegroundWindow(hwnd);
                        }
                    }
                }

                // '=' steals focus onto this window, full stop (2026-08-16, RedFalcon: "just have it
                // steal focus on = press and that's it" -- simplified from an earlier version that
                // also toggled the player controller's mouse cursor/camera-look). Only grabs focus if
                // already visible; stealing focus for a hidden window would just be confusing.
                {
                    static int last_seen_focus_steal_seq = 0;
                    int focus_steal_seq = MenuStatus::FocusStealSeq();
                    if (focus_steal_seq != last_seen_focus_steal_seq)
                    {
                        last_seen_focus_steal_seq = focus_steal_seq;
                        if (IsWindowVisible(hwnd))
                        {
                            SetForegroundWindow(hwnd);
                        }
                    }
                }

                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();

                // '-' ALSO closes this window directly while IT (not the game) has OS focus -- the
                // WINDOW_TOGGLE bridge above only ever fires from Lua's RegisterKeyBind, which is a
                // GAME-input hook and never receives a keypress while a different top-level window
                // (this one) has focus. Once SetForegroundWindow() above hands this window focus on
                // open, the game stops seeing '-' entirely, so without this check '-' could only ever
                // open, never close. Checked via ImGui's own key state instead, same pattern
                // MoveMenu.cpp's pollKeyboard() already uses for its arrow-key shortcuts -- no round
                // trip through Lua needed for this direction.
                if (ImGui::IsKeyPressed(ImGuiKey_Minus, false))
                {
                    ShowWindow(hwnd, SW_HIDE);
                }

                // Pin the ImGui content window to exactly fill the native OS window's client area,
                // with none of ImGui's own title bar/resize border/drag handling -- the native
                // window (its own real Win32 title bar, minimize/close buttons, drag-to-move,
                // resize) already provides all of that, so drawing a second ImGui-level window on
                // top of it produced a "window inside a window" look (two nested title bars/
                // borders). One layer of chrome now, not two.
                ImGuiIO& io = ImGui::GetIO();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(io.DisplaySize);
                constexpr ImGuiWindowFlags kRootWindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoBringToFrontOnFocus;
                ImGui::Begin("LivingBase Spawn Menu", nullptr, kRootWindowFlags);
                // Three top-level tabs: Tools (the original spawn tree + move panel content),
                // Instructions, and History. PIVOT (2026-08-16): Instructions/History used to live
                // in a true separate OS window/thread/D3D11 device (HelpWindow.cpp, opened via a
                // "Help" button) so they wouldn't feel crowded alongside Tools -- that second
                // thread turned out to be a real stability problem (two ImGui contexts/D3D11
                // devices racing at startup crashed the game even after fixing the obvious GImGui
                // race), so RedFalcon asked to fold everything back into ordinary tabs in this one
                // already-working window instead of continuing to chase races in a two-thread ImGui
                // setup. See HelpMenu.hpp for the fuller history.
                if (ImGui::BeginTabBar("##spawnmenu_tabs"))
                {
                    if (ImGui::BeginTabItem("Tools"))
                    {
                        // Side-by-side layout: spawn tree on the left, held-repeat move buttons on
                        // the right -- two independent BeginChild panes, NOT ImGui's old Columns()
                        // API. Columns() gave the move panel a fixed 180px REMAINDER
                        // (window_width - 180) while the tree got everything else, which on a wide
                        // window meant the tree got far more space than its content needed and the
                        // move panel felt cramped -- and separately, SpawnMenu::Draw()'s own inner
                        // BeginChild (sized to fill ITS column's available height) stretched to the
                        // full window height when the native window was resized tall, which pushed
                        // its OWN trailing Selected/Spawn/Replace row down to match, while
                        // MoveMenu::Draw() (no such stretching child) stacked its content at the
                        // natural top instead -- together making the move buttons look "far below"
                        // the spawn section (confirmed live 2026-08-16). Two explicit same-height
                        // children sidestep both: the move pane gets a real fixed width instead of
                        // a leftover, and passing an explicit height (not an auto-fill "0") to both
                        // makes them agree on where content starts.
                        // Widened from 280 (2026-08-16) -- the target-lock hint text was clipping
                        // at that width (confirmed live). MoveMenu.cpp's own hint text now wraps
                        // instead of clipping regardless of the exact width chosen, but 320 gives
                        // it -- and the Despawn/Undo row -- enough room to sit on one line rather
                        // than wrapping unnecessarily.
                        constexpr float kMovePanelWidth = 320.0f;
                        const float contentHeight = ImGui::GetContentRegionAvail().y;

                        // The world-load restore-lock grey-out (main.lua's restoreLockActive, the
                        // same flag every keyboard key is already gated on) lives INSIDE each
                        // panel's own Draw() instead of one blanket BeginDisabled wrapped around
                        // both here -- see SpawnMenu.cpp's and MoveMenu.cpp's own Draw() for where
                        // each one applies it.
                        // ImGuiChildFlags_AlwaysUseWindowPadding: a borderless child gets ZERO
                        // WindowPadding by default in this ImGui version (only bordered children
                        // pad automatically) -- without this flag, content sits flush against each
                        // child's own edge, which is exactly why the move panel's right-hand
                        // buttons looked cut off against the window border with no breathing room
                        // (confirmed live 2026-08-16) even though the panel itself had the width it
                        // needed.
                        constexpr ImGuiChildFlags kPaddedChild = ImGuiChildFlags_AlwaysUseWindowPadding;
                        ImGui::BeginChild("##spawnmenu_left", ImVec2(-kMovePanelWidth, contentHeight), kPaddedChild);
                        SpawnMenu::Draw();
                        ImGui::EndChild();

                        ImGui::SameLine();

                        ImGui::BeginChild("##spawnmenu_right", ImVec2(kMovePanelWidth, contentHeight), kPaddedChild);
                        MoveMenu::Draw();
                        ImGui::EndChild();

                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Instructions"))
                    {
                        HelpMenu::DrawInstructionsTab();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("History"))
                    {
                        HelpMenu::DrawHistoryTab();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::End();

                // CoordsMenu is a secondary window WITHIN this same context/thread (own Begin()/End()
                // pair, complete no-op when not open) -- small and meant to be used right next to
                // the D-pad it was opened from, so sharing this window's screen space is fine.
                CoordsMenu::Draw();

                ImGui::Render();
                const float clear_color[4] = {0.06f, 0.06f, 0.08f, 1.0f};
                ID3D11RenderTargetView* rtv = g_render_target_view.Get();
                g_device_context->OMSetRenderTargets(1, &rtv, nullptr);
                g_device_context->ClearRenderTargetView(g_render_target_view.Get(), clear_color);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

                g_swap_chain->Present(1, 0);
            }

            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext(imgui_context);

            CleanupRenderTarget();
            g_swap_chain.Reset();
            g_device_context.Reset();
            g_device.Reset();

            DestroyWindow(hwnd);
            UnregisterClassW(wc.lpszClassName, wc.hInstance);

            Output::send<LogLevel::Normal>(STR("[LivingBaseSpawnMenu] StandaloneWindow: stopped\n"));
        }
    } // namespace

    auto Start() -> void
    {
        g_stop_requested.store(false);
        g_thread = std::thread(ThreadMain);
    }

    auto Stop() -> void
    {
        g_stop_requested.store(true);
        if (g_thread.joinable())
        {
            g_thread.join();
        }
    }
} // namespace RC::LivingBaseSpawnMenu::StandaloneWindow
