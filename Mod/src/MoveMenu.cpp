#include <MoveMenu.hpp>

#include <CoordsMenu.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <MenuStatus.hpp>
#include <StandaloneWindow.hpp>

#include <fstream>
#include <string>
#include <utility>

#include <imgui.h>

namespace RC::LivingBaseSpawnMenu::MoveMenu
{
    namespace
    {
        // Same "ue4ss/Mods/LivingBase/..." CWD-relative convention as SpawnMenu.cpp's REQUEST_PATH
        // -- see that file's own comment for why a bare relative path is wrong here.
        constexpr const char* MOVE_REQUEST_PATH = "ue4ss/Mods/LivingBase/move_request.txt";

        // APPENDS one line per repeat-tick rather than overwriting. A held button can fire many
        // times between two of main.lua's polls; overwriting would collapse all but the last tick
        // into a single nudge (last-write-wins), silently losing most of the held duration -- main.lua
        // reads and SUMS every queued line each pass instead, so nothing is lost regardless of poll
        // timing relative to the repeat rate.
        auto queueLine(const std::string& line) -> void
        {
            std::ofstream f(MOVE_REQUEST_PATH, std::ios::app);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] MoveMenu: failed to write move_request.txt\n"));
                return;
            }
            f << line << "\n";
        }
        auto queueMove(const char* action) -> void { queueLine(std::string("MOVE:") + action); }
        // One-shot commands (toggle/clear/lock/despawn/undo), distinct from MOVE: spatial nudges --
        // see main.lua's own drainMoveMenuQueue for how these two line shapes are told apart and
        // handled differently (one-shot fires immediately, MOVE: accumulates for the throttled
        // flush).
        auto queueAction(const char* name) -> void { queueLine(std::string("ACTION:") + name); }
        auto queuePrecision(float scale) -> void { queueLine("PRECISION:" + std::to_string(scale)); }

        auto HoverTooltip(const char* text) -> void
        {
            if (text && ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", text);
            }
        }

        // A held-repeat button: fires once immediately on press, then repeats at ImGui's own
        // configured key-repeat delay/rate (io.KeyRepeatDelay/KeyRepeatRate) for as long as it's
        // held -- this IS the fix for the "single keypress nudge" problem, since a UI button held
        // down never has an event silently dropped the way this game's keydown handling does.
        auto repeatButton(const char* label, const char* action, float w, float h, const char* tooltip = nullptr) -> void
        {
            ImGui::PushButtonRepeat(true);
            if (ImGui::Button(label, ImVec2(w, h)))
            {
                queueMove(action);
            }
            ImGui::PopButtonRepeat();
            HoverTooltip(tooltip);
        }

        // Keyboard shortcuts, live within THIS window only (a genuinely separate native Win32
        // window -- Windows only ever delivers WM_KEYDOWN to whichever window has focus, so this
        // can never fire while the game itself has focus, no game-side key-claiming risk). Same
        // layout as LivingBase's own in-game live-edit keys (arrows = slide, PageUp/PageDown =
        // height, ',' '.' = rotate, Num+ = target lock) so muscle memory carries over -- and since
        // this window's keys go through Windows' own key-repeat (via ImGui_ImplWin32, which
        // forwards real WM_KEYDOWN auto-repeat), they don't have the "many keydowns silently
        // dropped" problem the in-game RegisterKeyBind path has, which is the whole reason this
        // panel exists.
        //
        // ImGui::IsKeyPressed(key, /*repeat=*/true) fires once on press and then repeats at the
        // SAME io.KeyRepeatDelay/KeyRepeatRate cadence PushButtonRepeat's buttons use above -- no
        // separate rate-limiting logic needed here, it's the identical mechanism.
        auto repeatKey(ImGuiKey key, const char* action) -> void
        {
            if (ImGui::IsKeyPressed(key, true))
            {
                queueMove(action);
            }
        }
        // Single-press only (repeat=false) for a TOGGLE, not a nudge -- holding + shouldn't spam
        // the lock on/off.
        auto pressKey(ImGuiKey key, const char* actionName) -> void
        {
            if (ImGui::IsKeyPressed(key, false))
            {
                queueAction(actionName);
            }
        }

        // RotateAxisActions REMOVED (2026-08-24, numpad-only keybind rebuild) -- the old single-
        // axis-cycle concept it served (','/'.'/'/ ') no longer exists; Rotate mode now drives all
        // three axes at once via the numpad's own direction keys (see pollKeyboard below), nothing
        // left to cycle between.

        // Numpad mirror (2026-08-24) -- same keys as LivingBase's own in-game numpad scheme (see
        // config.lua's Config.KEYS header comment), so muscle memory carries over whether the game
        // or this window has focus. The six dual-purpose direction keys move (translate) by
        // default, or rotate per-axis once Numpad 2 toggles Spawner.placementMode to "ROTATE" (via
        // ACTION:MODE_TOGGLE, same Lua-side state the in-game keys flip, so this window and the
        // keyboard can never disagree -- see MenuStatus::PlacementMode()'s own comment). PageUp/
        // PageDown/arrows stay exactly as before -- GUI-only, never added in-game.
        // 5/2 SWAPPED (2026-08-24, RedFalcon's WASD-feel request -- see config.lua's own comment
        // on Config.KEYS.changeMode) -- 8/4/5/6 now form the same plus-shape as W/A/S/D, and Mode
        // Toggle sits on a corner key instead of the one in the middle of the movement cross.
        auto pollKeyboard() -> void
        {
            repeatKey(ImGuiKey_UpArrow, "FWD");
            repeatKey(ImGuiKey_DownArrow, "BACK");
            repeatKey(ImGuiKey_LeftArrow, "LEFT");
            repeatKey(ImGuiKey_RightArrow, "RIGHT");
            repeatKey(ImGuiKey_PageUp, "UP");
            repeatKey(ImGuiKey_PageDown, "DOWN");

            const bool rotateMode = (MenuStatus::PlacementMode() == "ROTATE");
            repeatKey(ImGuiKey_Keypad7, rotateMode ? "ROTX_L" : "UP");
            repeatKey(ImGuiKey_Keypad8, rotateMode ? "ROTY_L" : "FWD");
            repeatKey(ImGuiKey_Keypad9, rotateMode ? "ROTX_R" : "DOWN");
            repeatKey(ImGuiKey_Keypad4, rotateMode ? "ROTZ_L" : "LEFT");
            repeatKey(ImGuiKey_Keypad6, rotateMode ? "ROTZ_R" : "RIGHT");
            repeatKey(ImGuiKey_Keypad5, rotateMode ? "ROTY_R" : "BACK");
            pressKey(ImGuiKey_Keypad2, "MODE_TOGGLE"); // one-shot: holding shouldn't spam-toggle
            pressKey(ImGuiKey_Keypad3, "DESPAWN");
            pressKey(ImGuiKey_KeypadAdd, "TARGET_LOCK"); // unchanged
            pressKey(ImGuiKey_KeypadDivide, "CANCEL_PLACEMENT");
            pressKey(ImGuiKey_KeypadMultiply, "GRAB_TARGET");
            pressKey(ImGuiKey_Keypad0, "CONFIRM_PLACEMENT");
            pressKey(ImGuiKey_KeypadDecimal, "TOGGLE_FREEBUILD");
            // Keypad1 (Release Cursor): the reverse of what it does in-game -- this window ALREADY
            // has OS focus while receiving these keypresses, so "steal focus for the window" is a
            // no-op on itself; hand focus back to the game instead, the only sensible meaning here.
            if (ImGui::IsKeyPressed(ImGuiKey_Keypad1, false))
            {
                StandaloneWindow::ReturnFocusToGame();
            }
            // F4: convenience alias for Despawn, next to SpawnMenu's own F2/F3 (Spawn/Replace).
            pressKey(ImGuiKey_F4, "DESPAWN");
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false) && ImGui::GetIO().KeyCtrl)
            {
                queueAction("UNDO");
            }
            // Numpad Subtract (Open/close the window) is handled in StandaloneWindow.cpp, not here
            // -- it has to work even while this window is CLOSED, which is outside this file's
            // reach entirely.
        }

        // Shrinks `text` to fit `maxWidth`, appending "..." if it had to cut anything, and always
        // shows the FULL text in a hover tooltip -- RedFalcon's request (2026-08-16): target names
        // can be long, don't let one wrap/stretch the panel, just truncate + mouseover for the rest.
        auto DrawTruncatedText(const std::string& text, float maxWidth) -> void
        {
            std::string display = text;
            if (!display.empty() && ImGui::CalcTextSize(display.c_str()).x > maxWidth)
            {
                constexpr const char* ellipsis = "...";
                while (!display.empty() && ImGui::CalcTextSize((display + ellipsis).c_str()).x > maxWidth)
                {
                    display.pop_back();
                }
                display += ellipsis;
            }
            ImGui::TextUnformatted(display.c_str());
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", text.empty() ? "(no target locked)" : text.c_str());
            }
        }

        // 6-level precision scheme, REBASED 2026-08-16 -- see main.lua's own PRECISION_LEVELS
        // comment for the full story: what used to be the "1/4" step is now the "1x (normal)"
        // baseline, with two extra fine steps below it and a "4x" added at the top so the OLD full
        // step is still reachable. Keep this array in exact sync with that Lua table.
        constexpr const char* PRECISION_LABELS[6] = {"1/8", "1/4", "1/2", "1x (normal)", "2x", "4x"};
        constexpr float PRECISION_SCALES[6] = {0.03125f, 0.0625f, 0.125f, 0.25f, 0.5f, 1.0f};
        int g_precision_idx = 3; // default: "1x (normal)"
    } // namespace

    auto Draw() -> void
    {
        pollKeyboard();

        const float avail = ImGui::GetContentRegionAvail().x;
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float cellW = (avail - gap * 2.0f) / 3.0f; // 3 equal columns
        const float wide2 = cellW * 2.0f + gap;          // 2 columns + the gap between them
        const float wide3 = avail;                        // full width
        constexpr float cellH = 28.0f;

        // Target readout: what Num+ (or the toggle-lock in-window keybind) currently has locked --
        // see MenuStatus.hpp/main.lua's "SPAWN MENU STATUS" comment for how this gets here.
        ImGui::TextUnformatted("Selected Target:");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBgHovered]);
        ImGui::BeginChild("##target_label", ImVec2(wide3, cellH), true, ImGuiWindowFlags_NoScrollbar);
        DrawTruncatedText(MenuStatus::TargetLabel(), wide3 - 16.0f);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        // Wrap (0.0f = wrap at the end of this child's own content region) instead of clip -- this
        // line got cut off mid-word at the panel's old width (confirmed live 2026-08-16); wrapping
        // is robust regardless of exactly how wide the panel ends up, unlike a fixed-width guess.
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Press Num + to select/release target lock");
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        ImGui::Separator();

        // Floor Clipping (2026-08-24, numpad-only keybind rebuild -- repurposes this same UI slot,
        // was "In-Game Keys"). The whole "In-Game Keys"/modEnabled concept it used to control is
        // gone: key availability is purely "is this window open" now, so there's nothing left for
        // a separate enable/disable toggle to do. This slot now shows/controls Spawner.
        // _placementFreeBuild instead (MenuStatus::IsFreeBuild()), same underlying toggle as the
        // in-game Numpad '.' key and F8 before it.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Floor Clipping");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(wide2);
        bool freebuild = MenuStatus::IsFreeBuild();
        if (ImGui::Checkbox("##floor_clipping", &freebuild))
        {
            queueAction("TOGGLE_FREEBUILD");
        }
        HoverTooltip("Toggle floor-lock off/on globally for placement -- mirrors the in-game Numpad '.' key.");

        ImGui::Spacing();
        ImGui::Separator();

        // Everything below acts on a placed object -- grey it all out ONLY while LivingBase's
        // world-load restore lock is active (main.lua's restoreLockActive) -- deliberately NOT
        // gated on the In-Game Keys checkbox above anymore (see that checkbox's own comment for
        // why), so this whole panel stays usable even with In-Game Keys off.
        ImGui::BeginDisabled(MenuStatus::IsRestoring());

        // Movement (slide/height/rotate/flip) additionally requires a locked target -- RedFalcon's
        // request (2026-08-16): this window is a genuinely separate native Win32 window, so while
        // it has keyboard focus the player can't move their own character with WASD either, and a
        // D-pad nudge with nothing target-locked was confusing (looks clickable, does nothing).
        // Gating the whole block up front instead of piecemeal makes "you need Num + first" obvious
        // at a glance rather than discovered button-by-button.
        const bool hasTarget = !MenuStatus::TargetLabel().empty();
        ImGui::BeginDisabled(!hasTarget);

        // Move/Rotate mode indicator (2026-08-24, numpad-only keybind rebuild): drives this row's
        // "Move" label AND the Rotate section's own X/Y/Z row highlight below -- computed once,
        // shared by both, so they can never show conflicting state.
        const bool rotateModeActive = (MenuStatus::PlacementMode() == "ROTATE");

        // Slide: a true D-pad cross (Forward/Backward centered over the gap between Left/Right),
        // matching RedFalcon's own mockup layout.
        ImGui::Dummy(ImVec2(cellW, cellH));
        ImGui::SameLine();
        repeatButton("Forward", "FWD", cellW, cellH, "Slide forward (Numpad 8)");

        repeatButton("Left", "LEFT", cellW, cellH, "Slide left (Numpad 4)");
        ImGui::SameLine();
        // "Move" mode-indicator label sits in the D-pad's otherwise-empty center cell (Coords
        // relocated below, next to Down) -- styled like the Rotate section's own inert axis
        // labels, and lit with INVERTED polarity relative to those: bright here in Move mode, dark
        // in Rotate mode (exactly the opposite of X/Y/Z below) -- together, exactly one side is
        // ever lit, an unambiguous mode indicator.
        if (!rotateModeActive)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_CheckMark]);
        }
        ImGui::BeginDisabled();
        ImGui::Button("Move", ImVec2(cellW, cellH));
        ImGui::EndDisabled();
        if (!rotateModeActive)
        {
            ImGui::PopStyleColor();
        }
        HoverTooltip(!rotateModeActive ? "Move mode is active -- Numpad 2 to switch to Rotate" : nullptr);
        ImGui::SameLine();
        repeatButton("Right", "RIGHT", cellW, cellH, "Slide right (Numpad 6)");

        ImGui::Dummy(ImVec2(cellW, cellH));
        ImGui::SameLine();
        repeatButton("Backward", "BACK", cellW, cellH, "Slide backward (Numpad 5)");

        ImGui::Spacing();

        // Height: Up/Down, plus Coords relocated here (2026-08-24, numpad rebuild -- was in the
        // D-pad's center cell, now occupied by the "Move" mode-indicator label above).
        repeatButton("Up", "UP", cellW, cellH, "Raise (PageUp / Numpad 7 in Move mode)");
        ImGui::SameLine();
        repeatButton("Down", "DOWN", cellW, cellH, "Lower (PageDown / Numpad 9 in Move mode)");
        ImGui::SameLine();
        // CoordsMenu::Open() reads MenuStatus's target snapshot, which is meaningless with nothing
        // locked -- covered by the hasTarget gate this whole block is already inside.
        if (ImGui::Button("Coords", ImVec2(cellW, cellH)))
        {
            CoordsMenu::Open();
        }
        HoverTooltip(hasTarget ? "Edit the target's exact X/Y/Z/Rotation" : "Target-lock something first (Num +)");

        ImGui::Spacing();
        ImGui::Separator();

        // Rotate: full 3-axis control (2026-08-18, replacing the old single-axis Rot L/R + Flip
        // 180). One row per axis, "<-" / axis letter / "->", X/Y/Z = Roll/Pitch/Yaw (Unreal's own
        // FRotator convention). The buttons themselves are always mouse-clickable regardless of
        // mode -- clicking one sends the exact same MOVE:ROTx_L/R request whether Move or Rotate
        // mode is active. Numpad 2 (or this window's own Keypad2 mirror) toggles Rotate mode,
        // which drives all three rows' direction keys (7/8/9/4/6/5) at once -- no per-axis
        // selection anymore, so all three rows light up TOGETHER now instead of just one.
        ImGui::TextUnformatted("Rotate");
        ImGui::SameLine();
        ImGui::TextDisabled("(Numpad 2 switches Move/Rotate)");
        {
            // Middle cell is a plain (non-interactive) label, not a real button -- BeginDisabled
            // keeps it visibly inert (ImGui's own standard "not clickable" look, used elsewhere in
            // this codebase for the same reason) while still letting a pushed background color
            // show through dimmed, which is what actually communicates "Rotate mode is active" here.
            auto axisRow = [&](const char* label, const char* leftAction, const char* rightAction)
            {
                // "##" + leftAction/rightAction (2026-08-24, RedFalcon's bug report: ImGui's own
                // "3 visible items with conflicting ID" popup on hover) -- repeatButton's ID comes
                // straight from its label (plain ImGui::Button(label, ...)), and all three axis
                // rows called this with the literal same "<-"/"->" strings, so X/Y/Z's left buttons
                // all collided on one ID (and same for the right buttons). leftAction/rightAction
                // are already unique per axis ("ROTX_L"/"ROTY_L"/"ROTZ_L" etc.), so reusing them as
                // the `##`-suffix disambiguates the ID without changing what's actually displayed
                // (text after `##` is ID-only, never shown).
                repeatButton(("<-##" + std::string(leftAction)).c_str(), leftAction, cellW, cellH);
                ImGui::SameLine();
                if (rotateModeActive)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_CheckMark]);
                }
                ImGui::BeginDisabled();
                ImGui::Button(label, ImVec2(cellW, cellH));
                ImGui::EndDisabled();
                if (rotateModeActive)
                {
                    ImGui::PopStyleColor();
                }
                HoverTooltip(rotateModeActive ? "Rotate mode is active -- Numpad 2 to switch to Move" : nullptr);
                ImGui::SameLine();
                repeatButton(("->##" + std::string(rightAction)).c_str(), rightAction, cellW, cellH);
            };
            axisRow("X", "ROTX_L", "ROTX_R");
            axisRow("Y", "ROTY_L", "ROTY_R");
            axisRow("Z", "ROTZ_L", "ROTZ_R");
        }

        ImGui::EndDisabled(); // !hasTarget

        ImGui::Spacing();
        ImGui::Separator();

        // Precision: how big a step Up/Down/slide take per nudge (rotate is unaffected). This
        // slider is the ONLY way to change it now (2026-08-24, numpad-only keybind rebuild -- the
        // old in-game Num- precision cycle is gone, Num- is the window Open/Close key now) -- see
        // handleMoveMenuPrecision's own comment in main.lua.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Precision");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(wide2);
        int idx = g_precision_idx;
        if (ImGui::SliderInt("##precision", &idx, 0, 5, PRECISION_LABELS[idx]))
        {
            g_precision_idx = idx;
            queuePrecision(PRECISION_SCALES[idx]);
        }
        HoverTooltip("How far Up/Down/slide move per nudge (doesn't affect rotate)");

        ImGui::Spacing();
        ImGui::Separator();

        // Despawn/Undo: matches the numpad's 3 (despawn in front) / this window's own Ctrl+Z
        // (restore last despawn -- no in-game key anymore, GUI-only) -- distinct from Delete All
        // below, which clears EVERYTHING. Despawn requires a locked target (RedFalcon, 2026-08-16
        // -- it only ever acts on the targeted object, same reasoning as the movement block and
        // Replace above); Undo doesn't, since it operates on the last despawn regardless of what's
        // currently locked.
        ImGui::BeginDisabled(!hasTarget);
        if (ImGui::Button("Despawn", ImVec2(cellW, cellH)))
        {
            queueAction("DESPAWN");
        }
        ImGui::EndDisabled();
        HoverTooltip(hasTarget ? "Despawn the targeted object (Numpad 3 / F4)" : "Target-lock something first (Num +)");
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(cellW, cellH));
        ImGui::SameLine();
        if (ImGui::Button("Undo", ImVec2(cellW, cellH)))
        {
            queueAction("UNDO");
        }
        HoverTooltip("Restore the last despawn (Ctrl+Z)");

        // Delete All: kept at the bottom, deliberately separated from everything else above, and
        // gated behind a real confirmation popup -- this destroys every actor LivingBase has
        // placed, so a stray click here should be as hard to trigger by accident as the keyboard's
        // own two-press DEL confirm is. Inside the !enabled disable (RedFalcon, 2026-08-16 --
        // originally left outside like Tools Active, but that made it the one button in this whole
        // panel that stayed clickable while everything else was greyed out, which read as
        // inconsistent rather than intentional).
        ImGui::Dummy(ImVec2(0.0f, 16.0f));
        if (ImGui::Button("Delete All", ImVec2(wide3, cellH)))
        {
            ImGui::OpenPopup("Confirm Delete All");
        }

        ImGui::EndDisabled(); // !enabled

        if (ImGui::BeginPopupModal("Confirm Delete All", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Despawn EVERYTHING LivingBase has placed?");
            ImGui::TextDisabled("This cannot be undone.");
            ImGui::Separator();
            if (ImGui::Button("Yes, delete everything", ImVec2(200.0f, 0.0f)))
            {
                queueAction("CLEAR_ALL");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
} // namespace RC::LivingBaseSpawnMenu::MoveMenu
