#include <MoveMenu.hpp>

#include <CoordsMenu.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <MenuStatus.hpp>

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

        // Rotation went from one axis (Z/yaw only) to three (X/Y/Z = Roll/Pitch/Yaw, 2026-08-18) --
        // ',' '.' stay the only two rotate keys rather than growing to six, by rotating whichever
        // axis is currently SELECTED. '/' (the plain key next to '.', not Num /, which stays bound
        // to the in-game 45-degree-rotate key and is untouched by this) cycles the selection
        // X -> Y -> Z -> X, same as the in-game '/' shortcut (Config.KEYS.toggleRotateAxis) --
        // deliberately NOT a local toggle here: both '/' presses send the SAME "ACTION:
        // ROTATE_AXIS_CYCLE" request, which main.lua's cycleRotateAxis() applies to ONE piece of
        // shared Lua state (also toast-confirmed there), read back via MenuStatus::RotateAxis() --
        // so the keyboard and this window can never disagree about which axis is active.
        auto RotateAxisActions(const std::string& axis) -> std::pair<const char*, const char*>
        {
            if (axis == "X") { return {"ROTX_L", "ROTX_R"}; }
            if (axis == "Y") { return {"ROTY_L", "ROTY_R"}; }
            return {"ROTZ_L", "ROTZ_R"};
        }

        auto pollKeyboard() -> void
        {
            repeatKey(ImGuiKey_UpArrow, "FWD");
            repeatKey(ImGuiKey_DownArrow, "BACK");
            repeatKey(ImGuiKey_LeftArrow, "LEFT");
            repeatKey(ImGuiKey_RightArrow, "RIGHT");
            repeatKey(ImGuiKey_PageUp, "UP");
            repeatKey(ImGuiKey_PageDown, "DOWN");
            pressKey(ImGuiKey_Slash, "ROTATE_AXIS_CYCLE"); // one-shot: holding shouldn't spin through axes
            auto [rotL, rotR] = RotateAxisActions(MenuStatus::RotateAxis());
            repeatKey(ImGuiKey_Comma, rotL);
            repeatKey(ImGuiKey_Period, rotR);
            pressKey(ImGuiKey_KeypadAdd, "TARGET_LOCK");
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

        // In-Game Keys (renamed from "Tools Active" 2026-08-16, SCOPE NARROWED the same day --
        // RedFalcon: "I want to split ingame keyboard keys from the GUI window... disabling in-game
        // keys should disable ONLY the keys in the game"). This toggle now ONLY gates LivingBase's
        // own keyboard keys (placement/live-edit/cycle/clear, main.lua's modGate) -- it no longer
        // has any effect on this panel's own buttons below, which stay usable regardless (the GUI
        // is the primary intended workflow now; main.lua's GUI-side handlers use restoreGate, not
        // modGate, precisely so they're independent of this flag). Still shown/toggleable here
        // purely as a convenience mirror of the in-game Insert key, same reasoning as always for why
        // it stays clickable outside the disabled block: there'd be no way to turn keys back on
        // otherwise.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("In-Game Keys");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(wide2);
        bool enabled = MenuStatus::IsEnabled();
        if (ImGui::Checkbox("##ingame_keys", &enabled))
        {
            queueAction("TOGGLE_ENABLE");
        }
        HoverTooltip("LivingBase's own keyboard keys on or off (placement/live-edit/cycle/clear) -- mirrors the in-game Insert key. Does NOT affect this panel's own buttons. Off by default each session.");

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

        // Slide: a true D-pad cross (Forward/Backward centered over the gap between Left/Right),
        // matching RedFalcon's own mockup layout.
        ImGui::Dummy(ImVec2(cellW, cellH));
        ImGui::SameLine();
        repeatButton("Forward", "FWD", cellW, cellH, "Slide forward (Up arrow)");

        repeatButton("Left", "LEFT", cellW, cellH, "Slide left (Left arrow)");
        ImGui::SameLine();
        // Coords sits in the D-pad's otherwise-empty center cell -- already covered by the
        // hasTarget gate above (CoordsMenu::Open() reads MenuStatus's target snapshot, which is
        // meaningless with nothing locked).
        if (ImGui::Button("Coords", ImVec2(cellW, cellH)))
        {
            CoordsMenu::Open();
        }
        HoverTooltip(hasTarget ? "Edit the target's exact X/Y/Z/Rotation" : "Target-lock something first (Num +)");
        ImGui::SameLine();
        repeatButton("Right", "RIGHT", cellW, cellH, "Slide right (Right arrow)");

        ImGui::Dummy(ImVec2(cellW, cellH));
        ImGui::SameLine();
        repeatButton("Backward", "BACK", cellW, cellH, "Slide backward (Down arrow)");

        ImGui::Spacing();

        // Height: Up/Down, on their own row now that Rotate (below) needs its own 3 rows.
        repeatButton("Up", "UP", cellW, cellH, "Raise (PageUp)");
        ImGui::SameLine();
        repeatButton("Down", "DOWN", cellW, cellH, "Lower (PageDown)");

        ImGui::Spacing();
        ImGui::Separator();

        // Rotate: full 3-axis control (2026-08-18, replacing the old single-axis Rot L/R + Flip
        // 180 -- RedFalcon: "not useful as much" once every axis is directly reachable, and props
        // that can rest at any angle -- a coin, an ingot, a dropped weapon -- need more than yaw,
        // unlike a statue/NPC). One row per axis, "<-" / axis letter / "->", X/Y/Z = Roll/Pitch/Yaw
        // (Unreal's own FRotator convention). Each button sends the exact same MOVE:ROTx_L/R
        // request the keyboard's ','/'.' send once pointed at that axis (see pollKeyboard's own
        // comment) -- clicking here and using the keyboard are just two paths to the same action.
        ImGui::TextUnformatted("Rotate");
        ImGui::SameLine();
        ImGui::TextDisabled("(keyboard: , . rotates axis  |  / switches which)");
        {
            // Middle cell is a plain (non-interactive) label, not a real button -- BeginDisabled
            // keeps it visibly inert (ImGui's own standard "not clickable" look, used elsewhere in
            // this codebase for the same reason) while still letting a pushed background color
            // show through dimmed, which is what actually communicates "active axis" here.
            auto axisRow = [&](const char* label, const char* leftAction, const char* rightAction, const std::string& current)
            {
                const bool isKeyboardAxis = (MenuStatus::RotateAxis() == current);
                repeatButton("<-", leftAction, cellW, cellH);
                ImGui::SameLine();
                if (isKeyboardAxis)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_CheckMark]);
                }
                ImGui::BeginDisabled();
                ImGui::Button(label, ImVec2(cellW, cellH));
                ImGui::EndDisabled();
                if (isKeyboardAxis)
                {
                    ImGui::PopStyleColor();
                }
                HoverTooltip(isKeyboardAxis ? "This is the axis ','/'.' currently rotate -- press '/' to switch" : nullptr);
                ImGui::SameLine();
                repeatButton("->", rightAction, cellW, cellH);
            };
            axisRow("X", "ROTX_L", "ROTX_R", "X");
            axisRow("Y", "ROTY_L", "ROTY_R", "Y");
            axisRow("Z", "ROTZ_L", "ROTZ_R", "Z");
        }

        ImGui::EndDisabled(); // !hasTarget

        ImGui::Spacing();
        ImGui::Separator();

        // Precision: how big a step Up/Down/slide take per nudge (rotate is unaffected). Shared
        // state with the keyboard's own Num- cycle -- see handleMoveMenuPrecision's own comment in
        // main.lua for why the two can't drift far even though they track separate cursor state.
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

        // Despawn/Undo: matches the keyboard's Num9 (despawn in front) / Num0 (restore last
        // despawn) -- distinct from Delete All below, which clears EVERYTHING. Despawn requires a
        // locked target (RedFalcon, 2026-08-16 -- it only ever acts on the targeted object, same
        // reasoning as the movement block and Replace above); Undo doesn't, since it operates on
        // the last despawn regardless of what's currently locked.
        ImGui::BeginDisabled(!hasTarget);
        if (ImGui::Button("Despawn", ImVec2(cellW, cellH)))
        {
            queueAction("DESPAWN");
        }
        ImGui::EndDisabled();
        HoverTooltip(hasTarget ? "Despawn the targeted object (Num9)" : "Target-lock something first (Num +)");
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(cellW, cellH));
        ImGui::SameLine();
        if (ImGui::Button("Undo", ImVec2(cellW, cellH)))
        {
            queueAction("UNDO");
        }
        HoverTooltip("Restore the last despawn (Num0)");

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
