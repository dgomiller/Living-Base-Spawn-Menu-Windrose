#include <StandaloneWindow.hpp>

#include <BarbieMenu.hpp>
#include <CoordsMenu.hpp>
#include <CustomMenu.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <HelpMenu.hpp>
#include <ImageLoader.hpp>
#include <MenuStatus.hpp>
#include <MoveMenu.hpp>
#include <SpawnMenu.hpp>
#include <SignMenu.hpp>
#include <TargetListMenu.hpp>

#include <atomic>
#include <chrono>
#include <fstream>
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
        // Shown in the native title bar (wide) and the ImGui root window's own title (narrow)
        // below -- keep both in sync with each other AND with LivingBase/mod.txt's own version
        // number (2026-08-24, RedFalcon's request) -- this companion mod doesn't track a separate
        // version of its own, it ships alongside LivingBase.
        constexpr const wchar_t* WINDOW_TITLE_W = L"Living Base Enhanced - v3.0.7";
        // "###LivingBaseMain" = a fixed ImGui window ID (2026-10-01): the text before it is what is shown, but the saved position/size no longer resets
        // every time the version in the title changes. (The native Win32 title above has no ### part.)
        constexpr const char* WINDOW_TITLE = "Living Base Enhanced - v3.0.7###LivingBaseMain";

        std::thread g_thread;
        std::atomic_bool g_stop_requested{};

        // Whatever window had OS focus immediately before we last stole it (see the two
        // SetForegroundWindow(hwnd) call sites below) -- in practice, the game itself, since
        // stealing focus is the only way this window's content can be clicked. Read/written only
        // from this window's own thread (both the capture points and ReturnFocusToGame() itself,
        // the latter called synchronously from SpawnMenu::Draw() while it's running on this same
        // thread) -- no cross-thread synchronization needed.
        HWND g_previous_foreground_window{};

        // GUI scale (2026-09-29, RedFalcon: "a gui resize option... dropdown on the top right alongside
        // Shade: that does .5, 1, 1.5, 2, 3 and the windowsize and all the objects in it resize the
        // same amount"). Implemented as ImGui's own hi-DPI mechanism rather than touching every
        // hard-coded pixel size across the tab files: the frame runs at a LOGICAL size
        // (client / scale) with io.DisplayFramebufferScale = scale, so every widget, and the font
        // rasterizer (ImGui 1.92 derives glyph density from that framebuffer scale, so text stays
        // crisp), scales together. Mouse coordinates are divided by the same factor in WndProc.
        // Persisted across launches in a tiny sidecar file.
        float g_uiScale = 1.0f;
        constexpr const char* UI_SCALE_PATH = "ue4ss/Mods/LivingBase/spawn_menu_ui_scale.txt";
        constexpr float kUiScaleValues[5] = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f};
        constexpr const char* kUiScaleLabels[5] = {"0.5x", "1x", "1.5x", "2x", "3x"};

        auto LoadUiScale() -> void
        {
            std::ifstream f(UI_SCALE_PATH);
            float v = 0.0f;
            if (f && (f >> v))
            {
                for (float allowed : kUiScaleValues)
                {
                    if (v == allowed)
                    {
                        g_uiScale = v;
                    }
                }
            }
        }

        auto SaveUiScale() -> void
        {
            std::ofstream f(UI_SCALE_PATH, std::ios::trunc);
            if (f)
            {
                f << g_uiScale << "\n";
            }
        }

        // Resizes the native window by newS/oldS, capped to the monitor's work area (RedFalcon: "cap
        // the height to the current screen size, that way they can use scroll if they want" -- the
        // tabs already scroll their own content when the window is shorter than it needs). While
        // shaded, height is instead the 32-logical-px strip at the new scale, and the remembered
        // un-shaded height is rescaled so Expand restores the right size.
        // Window size is always derived from the remembered 1x size (g_baseW/H), NOT from the
        // window's current size -- when a previous scale got capped to the monitor, the current size
        // no longer equals base*scale, and scaling from it made 3x -> 1x land at the wrong ratio
        // (RedFalcon's report). g_baseW/H track user drag-resizes via the check in the render loop.
        // Tick count of the last close attempt refused because a time change was running (0 = none) -- drives the
        // "can't close yet" notice next to the Hide GUI button.
        ULONGLONG g_closeBlockedAt = 0;
        int g_baseW = 780;
        int g_baseH = 620;
        int g_lastW = 0;
        int g_lastH = 0;

        auto ApplyWindowScale(HWND hwnd, float newS, bool shaded) -> void
        {
            RECT r{};
            GetWindowRect(hwnd, &r);
            int w = static_cast<int>(g_baseW * newS);
            int h = static_cast<int>(g_baseH * newS);
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{sizeof(MONITORINFO)};
            GetMonitorInfoW(mon, &mi);
            const int workW = mi.rcWork.right - mi.rcWork.left;
            const int workH = mi.rcWork.bottom - mi.rcWork.top;
            if (shaded)
            {
                RECT client{0, 0, w, static_cast<int>(32.0f * newS)};
                AdjustWindowRectEx(&client, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_TOPMOST);
                h = client.bottom - client.top;
            }
            else if (h > workH)
            {
                h = workH;
            }
            if (w > workW)
            {
                w = workW;
            }
            int x = r.left;
            int y = r.top;
            if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
            if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
            if (x < mi.rcWork.left) x = mi.rcWork.left;
            if (y < mi.rcWork.top) y = mi.rcWork.top;
            SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER);
            RECT after{};
            GetWindowRect(hwnd, &after);
            g_lastW = after.right - after.left;
            g_lastH = after.bottom - after.top;
        }

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
            // Client-area mouse coordinates arrive in physical pixels; the frame runs at logical
            // size (see g_uiScale's comment), so hand ImGui the logical position.
            if (msg == WM_MOUSEMOVE && g_uiScale != 1.0f)
            {
                const int mx = static_cast<int>(static_cast<short>(LOWORD(lparam)) / g_uiScale);
                const int my = static_cast<int>(static_cast<short>(HIWORD(lparam)) / g_uiScale);
                lparam = MAKELPARAM(static_cast<WORD>(mx), static_cast<WORD>(my));
            }
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
                // A time change is still running: the window must stay open until it finishes (closing it
                // mid-change left the day cycle racing with nothing to stop it). Swallow the close.
                if (MenuStatus::TimeBusy())
                {
                    g_closeBlockedAt = GetTickCount64();
                    return 0;
                }
                // Hide instead of destroying -- matches the "toggle it closed, keep playing,
                // toggle it back open" workflow this window is meant for, not a one-shot app.
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }

        auto ThreadMain() -> void
        {
            // DIAGNOSTIC STARTUP DELAY (2026-09-25, RedFalcon: F12/FPS-counter target this window
            // instead of the game, and it "doesnt matter if its open or closed... i think its the
            // timing of the hook maybe, like its running before the actual game registers so its
            // treated as the primary window"). Hover-driven focus (see the render loop below) made
            // ZERO difference, and RedFalcon separately confirmed the misbehavior happens even while
            // this window is fully HIDDEN -- ruling out both focus AND visibility as the cause. The
            // one thing that's true regardless of Show/HideWindow: this thread's own D3D11 device
            // starts calling Present() every loop iteration the INSTANT CreateDeviceD3D succeeds
            // below, likely well before Windrose's own real D3D12 swapchain exists (this thread starts
            // from SpawnMenuMod::on_unreal_init, which can fire quite early). If Steam's overlay hook
            // attaches to whichever swapchain in this process calls Present() FIRST (a known
            // multi-swapchain heuristic issue), this thread would win that race every time, and no
            // amount of focus/visibility juggling afterward could ever undo it. Testing that theory
            // with a blunt delay before this thread creates its own device/swapchain at all, so the
            // game's real one gets a real head start. If this fixes it, replace the fixed delay with
            // something that actually detects the real game window/swapchain instead of guessing a
            // duration.
            std::this_thread::sleep_for(std::chrono::seconds(15));

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
            // tab bar itself. Bumped again to 780x620 (2026-09-23, RedFalcon: "once the window
            // shade button was added, it pushed everything down... part half of the despawn and
            // undo buttons are cut off") -- the Shade/DLSS-indicator row added 2026-09-21 (see its
            // own comment just below) ate into the same fixed client height without this ever being
            // grown to compensate, clipping the Tools tab's bottom row. Still just a starting size,
            // the native window's own resize border works normally from here if this needs further
            // tuning.
            // WS_EX_NOACTIVATE + hover-driven focus REVERTED (2026-09-25, same day) -- RedFalcon's
            // own F12/screenshot-steals-the-gui report turned out NOT to be an OS-focus/activation
            // problem at all (confirmed live: hover-driven focus made zero difference, and the
            // misbehavior happened even with this window fully HIDDEN). Root cause was this thread's
            // own D3D11 device/swapchain simply starting to Present() before Windrose's own real
            // swapchain existed, winning whatever "which swapchain is the game" race Steam's overlay
            // hook runs once at startup -- fixed with a startup delay in ThreadMain, see that
            // function's own comment. Since OS focus was never the problem, the plain
            // always-steal-focus-on-open behavior (see the '-' toggle block below) is restored as-is.
            HWND hwnd = CreateWindowExW(WS_EX_TOPMOST,
                                         wc.lpszClassName,
                                         WINDOW_TITLE_W,
                                         WS_OVERLAPPEDWINDOW,
                                         100,
                                         100,
                                         780,
                                         620,
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

            // Restore the last-used GUI scale (see g_uiScale) and size the window to match.
            LoadUiScale();
            if (g_uiScale != 1.0f)
            {
                ApplyWindowScale(hwnd, g_uiScale, false);
            }
            {
                RECT initial{};
                GetWindowRect(hwnd, &initial);
                g_lastW = initial.right - initial.left;
                g_lastH = initial.bottom - initial.top;
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

            // Must run AFTER the D3D11 device exists (ImageLoader::GetOrLoad needs it to create
            // each swatch's texture/SRV) -- BarbieMenu.cpp's thumbnails load lazily on first Draw(),
            // this just hands over the device to load them with.
            ImageLoader::Init(g_device.Get());

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

                // Publish real visibility back to Lua (2026-08-20) -- self-throttled internally
                // (only writes on an actual change), so cheap to call every frame regardless, same
                // as Poll() above. Deliberately OUTSIDE the toggle_seq-changed block below: that
                // block only runs when '-' was pressed, but this needs to reflect the CURRENT state
                // at all times (e.g. the window's own [X] close button, unrelated to that key,
                // still needs to reach Lua).
                MenuStatus::PublishWindowVisible(IsWindowVisible(hwnd) != FALSE);

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
                // game focus at all. RE-CONFIRMED 2026-09-25: a same-day hover-driven-focus/
                // WS_EX_NOACTIVATE experiment (in response to an F12/FPS-counter report) was reverted
                // after live testing showed OS focus was never the actual cause -- see ThreadMain's
                // own startup-delay comment for the real root cause and fix. This plain
                // always-steal-on-open behavior is back to how it always was.
                {
                    static int last_seen_toggle_seq = 0;
                    int toggle_seq = MenuStatus::WindowToggleSeq();
                    if (toggle_seq != last_seen_toggle_seq)
                    {
                        last_seen_toggle_seq = toggle_seq;
                        if (IsWindowVisible(hwnd))
                        {
                            if (MenuStatus::TimeBusy())
                            {
                                g_closeBlockedAt = GetTickCount64(); // Lua's '-' key: refused while time is changing
                            }
                            else
                            {
                                ShowWindow(hwnd, SW_HIDE);
                            }
                        }
                        else
                        {
                            ShowWindow(hwnd, SW_SHOW);
                            // Remember who had focus before we take it -- see ReturnFocusToGame()'s
                            // own comment (2026-08-23).
                            g_previous_foreground_window = GetForegroundWindow();
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
                            // Same capture as the '-' open branch above (2026-08-23).
                            g_previous_foreground_window = GetForegroundWindow();
                            SetForegroundWindow(hwnd);
                        }
                    }
                }

                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                // Run the frame at logical size and let the framebuffer scale blow it back up --
                // see g_uiScale's comment. Must happen after the backend sets io.DisplaySize and
                // before ImGui::NewFrame() reads it.
                // Applied unconditionally, INCLUDING at 1x: DisplayFramebufferScale persists across
                // frames (the backend only resets DisplaySize), so skipping it at 1x left the
                // previous scale's value in place and text stayed enlarged (RedFalcon's report).
                {
                    ImGuiIO& scaleIo = ImGui::GetIO();
                    scaleIo.DisplaySize = ImVec2(scaleIo.DisplaySize.x / g_uiScale, scaleIo.DisplaySize.y / g_uiScale);
                    scaleIo.DisplayFramebufferScale = ImVec2(g_uiScale, g_uiScale);
                }
                ImGui::NewFrame();

                // Numpad '-' (2026-08-24, numpad-only keybind rebuild -- was the plain '-'/Minus
                // key) ALSO closes this window directly while IT (not the game) has OS focus -- the
                // WINDOW_TOGGLE bridge above only ever fires from Lua's RegisterKeyBind, which is a
                // GAME-input hook and never receives a keypress while a different top-level window
                // (this one) has focus. Once SetForegroundWindow() above hands this window focus on
                // open, the game stops seeing Numpad '-' entirely, so without this check it could
                // only ever open, never close. Checked via ImGui's own key state instead, same
                // pattern MoveMenu.cpp's pollKeyboard() already uses for its arrow-key shortcuts --
                // no round trip through Lua needed for this direction.
                if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false))
                {
                    if (MenuStatus::TimeBusy())
                    {
                        g_closeBlockedAt = GetTickCount64(); // refused while a time change is running
                    }
                    else
                    {
                        ShowWindow(hwnd, SW_HIDE);
                    }
                }

                // F1/F5/F6/F10 tab shortcuts (2026-08-24, numpad-only keybind rebuild; F6 added
                // 2026-09-08 for Custom) -- read once
                // per frame, before the tab bar, and applied via ImGuiTabItemFlags_SetSelected on
                // the matching BeginTabItem call below (ImGui's own documented way to force a tab
                // active programmatically). F2/F3/F4 (Spawn/Replace/Despawn) live in SpawnMenu.cpp/
                // MoveMenu.cpp instead -- they act on content INSIDE the Tools tab, not the tab bar
                // itself, so they belong with the buttons they mirror, not here.
                // Tools was F9 originally -- RedFalcon found that collides with another mod's own
                // ModMenu window shortcut (it popped up INSIDE this window on F9), so moved to F5,
                // which nothing else in this bridge uses.
                const ImGuiTabItemFlags toolsTabFlags =
                        ImGui::IsKeyPressed(ImGuiKey_F5, false) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                // F6 (2026-09-08, new "Custom" tab): next free function key after Spawn/Replace/
                // Despawn (F2/F3/F4) and Tools (F5) -- F9 stays permanently avoided (see the Tools
                // comment above for why).
                const ImGuiTabItemFlags customTabFlags =
                        ImGui::IsKeyPressed(ImGuiKey_F7, false) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                const ImGuiTabItemFlags instructionsTabFlags =
                        ImGui::IsKeyPressed(ImGuiKey_F1, false) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                const ImGuiTabItemFlags historyTabFlags =
                        ImGui::IsKeyPressed(ImGuiKey_F11, false) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                // F7 (2026-09-21, new "Photo Mode" tab: Camera + Lights) -- F7/F8 were retired from
                // their old grab-target/free-build meanings by the 2026-08-24 numpad rebuild (see
                // config.lua's own "was F7"/"was F8" comments), so both are genuinely free; F12 is
                // Steam's own screenshot hotkey (config.lua's own comment) and stays avoided.
                const ImGuiTabItemFlags photoModeTabFlags =
                        ImGui::IsKeyPressed(ImGuiKey_F10, false) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                // F8 (2026-09-26, new "Target List" tab) -- next free function key after Photo
                // Mode's F7; F9 stays permanently avoided (see the Tools comment above for why),
                // F11/F12 are the OS/Steam's own screenshot hotkeys.
                const ImGuiTabItemFlags targetListTabFlags =
                        ImGui::IsKeyPressed(ImGuiKey_F6, false) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                // "Signs" (2026-09-29): switched to whenever the in-game Delete key is pressed -- Lua
                // bumps SIGN_TAB_SEQ in sign_status.txt (signs.lua's Signs.KeyPressed). Must be polled
                // here, every frame, since SignMenu::Draw only runs while its own tab is active.
                const ImGuiTabItemFlags signsTabFlags =
                        (SignMenu::ConsumeTabRequest() | ImGui::IsKeyPressed(ImGuiKey_F8, false)) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;

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
                ImGui::Begin(WINDOW_TITLE, nullptr, kRootWindowFlags);

                // Windowshade toggle (2026-09-21, RedFalcon: "add a windowshade mode button to the
                // entire menu window so that only the title bar is visible") -- this window is a
                // REAL native WS_OVERLAPPEDWINDOW (see its own CreateWindowExW comment above), and
                // ImGui's content area is pinned to exactly match its client rect (io.DisplaySize,
                // set just above) -- so "windowshade" here means actually resizing the NATIVE window
                // down to just its own title bar via SetWindowPos, not an ImGui-level collapse (this
                // window deliberately has ImGuiWindowFlags_NoCollapse, since ImGui's own title bar is
                // suppressed entirely in favor of the real OS one). A thin 32px client strip is kept
                // even while shaded -- just enough room for this SAME button to stay visible/
                // clickable, since a fully zero-height client area would leave no way to un-shade.
                static bool g_windowShaded = false;
                // Track user drag-resizes as the remembered 1x size (g_baseW/H), so scale changes
                // always derive from it -- see ApplyWindowScale's comment. Skipped while shaded or
                // minimized (their rects aren't the real un-shaded size).
                if (!g_windowShaded && !IsIconic(hwnd))
                {
                    RECT cur{};
                    GetWindowRect(hwnd, &cur);
                    const int curW = cur.right - cur.left;
                    const int curH = cur.bottom - cur.top;
                    if (curW != g_lastW || curH != g_lastH)
                    {
                        g_baseW = static_cast<int>(curW / g_uiScale);
                        g_baseH = static_cast<int>(curH / g_uiScale);
                        g_lastW = curW;
                        g_lastH = curH;
                    }
                }
                // Relabeled "Show GUI"/"Hide GUI" (2026-09-29, RedFalcon: clearer for end users than
                // "Shade"/"Expand").
                if (ImGui::SmallButton(g_windowShaded ? "Show GUI" : "Hide GUI"))
                {
                    if (!g_windowShaded)
                    {
                        RECT rect{};
                        GetWindowRect(hwnd, &rect);
                        const int width = rect.right - rect.left;
                        // AdjustWindowRectEx: the correct Win32 way to turn "N pixels of CLIENT
                        // area" into the full outer window size for THIS window's real style/border,
                        // rather than hand-guessing caption/border metrics.
                        RECT client{0, 0, width, static_cast<int>(32.0f * g_uiScale)};
                        AdjustWindowRectEx(&client, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_TOPMOST);
                        SetWindowPos(hwnd, nullptr, rect.left, rect.top, width, client.bottom - client.top, SWP_NOZORDER);
                        RECT shadedRect{};
                        GetWindowRect(hwnd, &shadedRect);
                        g_lastW = shadedRect.right - shadedRect.left;
                        g_lastH = shadedRect.bottom - shadedRect.top;
                    }
                    else
                    {
                        ApplyWindowScale(hwnd, g_uiScale, false);
                    }
                    g_windowShaded = !g_windowShaded;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", g_windowShaded
                        ? "Show the full GUI again."
                        : "Collapse the GUI down to just the title bar.");
                }

                // Time change running: say so, and say why a close attempt did nothing (bright for 4s after one).
                if (MenuStatus::TimeBusy())
                {
                    ImGui::SameLine();
                    const bool justBlocked = g_closeBlockedAt != 0 && (GetTickCount64() - g_closeBlockedAt) < 4000;
                    ImGui::TextColored(justBlocked ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) : ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                                       justBlocked ? "Can't close yet - time change running" : "Time change running...");
                }

                // GUI scale dropdown, top right alongside Shade (see g_uiScale's comment).
                {
                    static int s_scaleIdx = -1;
                    if (s_scaleIdx < 0)
                    {
                        s_scaleIdx = 1;
                        for (int i = 0; i < 5; ++i)
                        {
                            if (kUiScaleValues[i] == g_uiScale) s_scaleIdx = i;
                        }
                    }
                    constexpr float kScaleComboW = 72.0f;
                    // "GUI Scale" label to the left of the dropdown (2026-09-29, RedFalcon: clarity for
                    // end users). Vertically aligned to the combo's frame padding.
                    const float labelW = ImGui::CalcTextSize("GUI Scale").x;
                    const float comboX = ImGui::GetWindowWidth() - kScaleComboW - ImGui::GetStyle().WindowPadding.x;
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(comboX - labelW - ImGui::GetStyle().ItemSpacing.x);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("GUI Scale");
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(comboX);
                    ImGui::SetNextItemWidth(kScaleComboW);
                    if (ImGui::BeginCombo("##ui_scale", kUiScaleLabels[s_scaleIdx]))
                    {
                        for (int i = 0; i < 5; ++i)
                        {
                            if (ImGui::Selectable(kUiScaleLabels[i], i == s_scaleIdx) && kUiScaleValues[i] != g_uiScale)
                            {
                                ApplyWindowScale(hwnd, kUiScaleValues[i], g_windowShaded);
                                g_uiScale = kUiScaleValues[i];
                                s_scaleIdx = i;
                                SaveUiScale();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("GUI size -- scales the window and everything in it.");
                    }
                }

                // Four top-level tabs: Tools (the original spawn tree + move panel content), Custom
                // (2026-09-08, RedFalcon's per-category cloth-color panel -- see CustomMenu.hpp),
                // Instructions, and History. PIVOT (2026-08-16): Instructions/History used to live
                // in a true separate OS window/thread/D3D11 device (HelpWindow.cpp, opened via a
                // "Help" button) so they wouldn't feel crowded alongside Tools -- that second
                // thread turned out to be a real stability problem (two ImGui contexts/D3D11
                // devices racing at startup crashed the game even after fixing the obvious GImGui
                // race), so RedFalcon asked to fold everything back into ordinary tabs in this one
                // already-working window instead of continuing to chase races in a two-thread ImGui
                // setup. See HelpMenu.hpp for the fuller history.
                //
                // Skipped entirely while windowshaded (2026-09-21) -- there's no room to draw any of
                // this in the 32px strip the native window shrinks to anyway, and skipping avoids
                // ImGui laying out (and clipping) a full tab bar's worth of content into almost no
                // space every frame.
                if (!g_windowShaded) {
                if (ImGui::BeginTabBar("##spawnmenu_tabs"))
                {
                    // Renamed from "Tools" (2026-09-23, RedFalcon's own mockup). This root window
                    // has ImGuiWindowFlags_NoSavedSettings set (see kRootWindowFlags above), so
                    // there's no .ini-persisted tab state keyed by the old label to worry about --
                    // a plain rename is safe.
                    if (ImGui::BeginTabItem("Spawn / Move", nullptr, toolsTabFlags))
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
                    // "Target List" (2026-09-26, RedFalcon: scan this mod's own tracked actors by
                    // radius + category checkboxes, list nearest-first, target one directly instead
                    // of the usual hover/probe pick). Self-contained single Draw() (unlike the
                    // Spawn/Move tab's two-panel split, which StandaloneWindow itself lays out) --
                    // TargetListMenu.cpp owns its own left/right BeginChild split internally.
                    if (ImGui::BeginTabItem("Target List", nullptr, targetListTabFlags))
                    {
                        TargetListMenu::Draw();
                        ImGui::EndTabItem();
                    }
                    // Renamed from "Custom" (2026-09-23, RedFalcon's own mockup).
                    if (ImGui::BeginTabItem("Customize", nullptr, customTabFlags))
                    {
                        // Selected Target MOVED to the very top of the whole tab (2026-09-12,
                        // RedFalcon: "let's move selected target to the top of the custom tab") --
                        // it used to open CustomMenu::Draw() itself, which visually put it AFTER all
                        // of BarbieMenu's own content below. DrawTargetHeader() is that same block,
                        // extracted so it can run first regardless of which panel logically owns the
                        // rest of what follows.
                        //
                        // Kept OUTSIDE the scrolling child below (2026-09-16, RedFalcon: "Is it
                        // possible to keep the tabs and the target section visible on scroll so they
                        // are always accessible?") -- everything in a single ImGui window shares ONE
                        // scroll offset, so once this tab's own content (Spawn/Body/Clothes/Belts and
                        // Straps) grew tall enough to need scrolling, the WHOLE window scrolled,
                        // taking the tab bar and this target header with it. Wrapping just the
                        // content below in its own BeginChild (same technique the "Tools" tab already
                        // uses for its own two panes, sized to fill the exact remaining height of
                        // this fixed-size root window) gives it an independent scroll region, so the
                        // tab bar and this header never move.
                        CustomMenu::DrawTargetHeader();
                        ImGui::Spacing();
                        const float customContentHeight = ImGui::GetContentRegionAvail().y;
                        ImGui::BeginChild("##custom_tab_scroll", ImVec2(0.0f, customContentHeight), false);
                        // "Spawn" -- BarbieMenu's own Body Type/Origin grids + Spawn button, hidden
                        // by default (2026-09-16, RedFalcon: "First Section = Spawn - Hide by
                        // default") -- Barbie is about PLACING a brand-new NPC (no target needed),
                        // everything below is about EDITING an already-target-locked one.
                        if (ImGui::CollapsingHeader("Spawn"))
                        {
                            BarbieMenu::Draw();
                        }
                        CustomMenu::Draw();
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    // "Signs" (2026-09-29, RedFalcon: put text on a sign instead of its picture).
                    // Function-key shortcut F8 (see signsTabFlags above); the tab is also reached by
                    // clicking it, and the in-game Delete key selects a sign from Lua's side.
                    if (ImGui::BeginTabItem("Signs / Labels", nullptr, signsTabFlags))
                    {
                        SignMenu::Draw();
                        ImGui::EndTabItem();
                    }
                    // "Photo Mode" (2026-09-21, RedFalcon: "I want a new Photo Mode tab to have the
                    // camera and lights in it") -- both live in CustomMenu.cpp (it already owns all
                    // the request/status-file plumbing each needs) but are drawn from this own tab,
                    // not folded into the "Custom" tab above. Originally stacked Camera above Lights
                    // (2026-09-22); changed to SIDE BY SIDE (2026-09-23, RedFalcon: "review the
                    // mockup for photo mode, as the plan is to have camera on the left and lighting
                    // on the right so it fits in the window without scrolling" -- matches the
                    // original mockup's own left/right layout, and stacking the two sections
                    // vertically had grown taller than the window). Each half is its own BeginChild
                    // (not just a BeginGroup) specifically so ImGui::GetContentRegionAvail() inside
                    // DrawCameraSectionImpl/DrawLightsSectionImpl -- both of which size their own
                    // rows/sliders off it -- correctly reports the HALF-width column instead of the
                    // whole tab's width. Height 0 fills whatever's left of the tab's own area (which
                    // the 780x620 window resize above was sized to cover); if either column's real
                    // content still runs taller than that in practice, ImGui's normal child-window
                    // scrollbar is the graceful fallback rather than the whole tab overflowing.
                    if (ImGui::BeginTabItem("Photo Mode", nullptr, photoModeTabFlags))
                    {
                        // Gap widened + columns unequal (2026-09-23, RedFalcon: "add a little more
                        // padding between the camera controls and lighting as they seem a bit
                        // tight. Camera has more room to adjust so shrink its right side a bit") --
                        // Camera's own content is compact/button-based with slack to spare, while
                        // Lights' per-slot label/slider rows were the ones running cramped, so
                        // Lights keeps the larger share of whatever Camera gives up.
                        const float photoModeAvail = ImGui::GetContentRegionAvail().x;
                        constexpr float kPhotoModeGap = 24.0f;
                        const float photoModeUsable = photoModeAvail - kPhotoModeGap;
                        const float photoModeCameraW = photoModeUsable * 0.42f;
                        const float photoModeLightsW = photoModeUsable - photoModeCameraW;
                        ImGui::BeginChild("##photomode_camera_col", ImVec2(photoModeCameraW, 0.0f), false);
                        CustomMenu::DrawCameraSection();
                        ImGui::EndChild();
                        ImGui::SameLine(0.0f, kPhotoModeGap);
                        ImGui::BeginChild("##photomode_lights_col", ImVec2(photoModeLightsW, 0.0f), false);
                        CustomMenu::DrawLightsSection();
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    // 2026-09-23, RedFalcon asked to right-align these two -- tried
                    // ImGuiTabItemFlags_Trailing, but reading Dear ImGui's own TabBarLayout
                    // (imgui_widgets.cpp) confirmed it only keeps trailing tabs grouped at the end
                    // and clamped from overlapping; it does NOT push them into unused bar width when
                    // the tab bar is wider than its tabs (there's no built-in "float right" for a
                    // plain, non-docked BeginTabBar). Doing this for real would mean replacing the
                    // native tab bar with a hand-rolled button strip -- RedFalcon's call: "leave it
                    // as-is" rather than take that tradeoff. Plain left-to-right order, no special
                    // flag.
                    if (ImGui::BeginTabItem("Instructions", nullptr, instructionsTabFlags))
                    {
                        HelpMenu::DrawInstructionsTab();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("History", nullptr, historyTabFlags))
                    {
                        HelpMenu::DrawHistoryTab();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                } // if (!g_windowShaded)
                ImGui::End();

                // CoordsMenu is a secondary window WITHIN this same context/thread (own Begin()/End()
                // pair, complete no-op when not open) -- small and meant to be used right next to
                // the D-pad it was opened from, so sharing this window's screen space is fine.
                CoordsMenu::Draw();
                // Same convention for the Photo Mode tab's own Camera Coords popup (2026-09-22).
                CustomMenu::DrawCameraCoordsPopup();

                ImGui::Render();
                const float clear_color[4] = {0.06f, 0.06f, 0.08f, 1.0f};
                ID3D11RenderTargetView* rtv = g_render_target_view.Get();
                g_device_context->OMSetRenderTargets(1, &rtv, nullptr);
                g_device_context->ClearRenderTargetView(g_render_target_view.Get(), clear_color);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

                g_swap_chain->Present(1, 0);
            }

            // Before the device itself is torn down below -- every cached SRV/texture in
            // ImageLoader was created FROM this device, must not outlive it.
            ImageLoader::ReleaseAll();

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

    // See this function's own declaration comment in the header, and g_previous_foreground_window's
    // comment above for why the capture/restore split works. CONFIRMED WORKING LIVE (2026-08-24):
    // Spawn/Replace correctly hand focus back to the game.
    auto ReturnFocusToGame() -> void
    {
        if (g_previous_foreground_window && IsWindow(g_previous_foreground_window))
        {
            SetForegroundWindow(g_previous_foreground_window);
        }
    }
} // namespace RC::LivingBaseSpawnMenu::StandaloneWindow
