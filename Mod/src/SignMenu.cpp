#include <SignMenu.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <MenuStatus.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <imgui.h>

namespace RC::LivingBaseSpawnMenu::SignMenu
{
    namespace
    {
        // Same "ue4ss/Mods/LivingBase/..." CWD-relative convention every other bridge file uses.
        constexpr const char* REQUEST_PATH = "ue4ss/Mods/LivingBase/sign_request.txt";
        constexpr const char* STATUS_PATH = "ue4ss/Mods/LivingBase/sign_status.txt";

        // "Special Items" dropdown (2026-09-29, RedFalcon): things worth spawning from the Signs tab. Index order MUST match
        // main.lua's SPAWN_MENU_SPECIAL_ITEMS. Just the Sign Post for now.
        constexpr const char* kSpecialItems[] = {"Sign Post",          "Wall Flag 1",        "Wall Flag 2", "Wall Flag 3", "Wall Flag 4",
                                                 "Board 1 (One line)", "Board 2 (One line)", "Board 3 (One line)", "Obelisk"};
        int g_specialIdx = 0;
        constexpr const char* SPAWN_REQUEST_PATH = "ue4ss/Mods/LivingBase/spawn_request.txt";

        // Same request the Spawn tab writes ("SPAWN:<ROSTER>:<1-based index>"), so placement afterwards is exactly the regular
        // spawn flow: ghost preview follows the camera; Confirm (Numpad 0 / the Spawn tab's Confirm) places it, Cancel drops it.
        auto requestSpawnSpecial(int index) -> void
        {
            std::ofstream f(SPAWN_REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] SignMenu: failed to write spawn_request.txt\n"));
                return;
            }
            f << "SPAWN:SPECIAL_ITEMS:" << (index + 1) << "\n";
        }

        // Font dropdown (2026-09-29): keys are what Lua saves per sign (signs.lua's Signs.FONTS -- keep in step).
        // Greengoth is the default (RedFalcon); "Built-in" is the game's own text font.
        constexpr const char* kFontKeys[] = {"builtin", "greengoth", "oleo"};
        constexpr const char* kFontLabels[] = {"Built-in", "Greengoth", "Script (Oleo)"};
        int g_fontIdx = 1;

        constexpr int kRows = 4;
        constexpr int kMaxChars = 48; // hard per-row capacity of the buffer
        // Live per-row limit (RedFalcon, 2026-09-30: "limit it to the characters that fit that width -- 48 is too many
        // letters to be legible anyway"). Re-measured every frame from the text box's own width and font by Draw(), so it
        // stays exact at any GUI scale; never more than kMaxChars.
        int g_maxChars = kMaxChars;
        // Per-object line limit (SIGN_MAX_ROWS from the status file): 4 normally, 1 for the one-line boards. Never above kRows.
        int g_maxRows = kRows;

        // One multiline box holding all rows joined by '\n'; capped to kRows lines of kMaxChars each by
        // textEditFilter below.
        constexpr int kTextBuf = kRows * (kMaxChars + 1) + 8;
        char g_text[kTextBuf] = {};
        bool g_hasTarget = false;
        std::string g_signName;
        int g_seenSeq = -1;
        // Ink colour (0..1 floats for ImGui); default matches signs.lua's dark-brown ink (25,15,5).
        constexpr float kDefaultColor[3] = {25.0f / 255.0f, 15.0f / 255.0f, 5.0f / 255.0f};
        float g_color[3] = {kDefaultColor[0], kDefaultColor[1], kDefaultColor[2]};
        // "Last used" swatch at the end of the presets: white until a colour has been applied to a sign,
        // then the most recent one (lets you copy a colour from one sign to another).
        float g_lastColor[3] = {1.0f, 1.0f, 1.0f};
        bool g_glow = false; // "Glow": unlit/emissive text material so the text ignores darkness
        bool g_pickerDirty = false; // the colour popup changed g_color; applied once the popup closes

        auto colorByte(float v) -> int
        {
            return std::clamp(static_cast<int>(v * 255.0f + 0.5f), 0, 255);
        }

        int g_tabSeq = -1;     // latest SIGN_TAB_SEQ read from sign_status.txt (-1 = not read yet)
        int g_seenTabSeq = -1; // baseline / last value already acted on
        double g_lastPoll = -1.0;

        auto HoverTooltip(const char* text) -> void
        {
            if (text && ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", text);
            }
        }

        auto writeRequest(const std::string& body) -> void
        {
            std::ofstream f(REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] SignMenu: failed to write sign_request.txt\n"));
                return;
            }
            f << body;
        }

        auto requestTarget() -> void { writeRequest("TARGET\n"); }
        auto requestClear() -> void { writeRequest("CLEAR\n"); }
        auto requestReset() -> void { writeRequest("RESET\n"); }
        auto requestApply() -> void
        {
            // Split the box into exactly kRows rows for the wire (one line each).
            std::string rows[kRows];
            int r = 0;
            for (const char* p = g_text; *p && r < kRows; ++p)
            {
                if (*p == '\n')
                {
                    ++r;
                }
                else if (*p != '\r')
                {
                    rows[r] += (*p == '\t') ? ' ' : *p;
                }
            }
            // Remember the colour going onto this sign for the "last used" swatch.
            g_lastColor[0] = g_color[0];
            g_lastColor[1] = g_color[1];
            g_lastColor[2] = g_color[2];
            char colorLine[48];
            std::snprintf(colorLine, sizeof(colorLine), "COLOR:%d,%d,%d\n", colorByte(g_color[0]), colorByte(g_color[1]),
                          colorByte(g_color[2]));
            std::string body = std::string("APPLY\n") + colorLine + (g_glow ? "GLOW:1\n" : "GLOW:0\n") +
                               "FONT:" + kFontKeys[g_fontIdx] + "\n";
            for (const std::string& row : rows)
            {
                body += row + "\n";
            }
            writeRequest(body);
        }

        // Keeps the multiline box to kRows lines and kMaxChars per line. Runs on every edit, so typing,
        // pasting and Enter can never exceed 4 lines: anything past the 4th line is dropped.
        auto textEditFilter(ImGuiInputTextCallbackData* d) -> int
        {
            // HARD LIMIT at input time (RedFalcon: "limit each line instead of trimming"): a typed or
            // pasted character is simply refused once its line is full, and Enter is refused on line 4.
            // Nothing is written and then cut back. Returning 1 from a CharFilter discards the char.
            if (d->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
            {
                int lineCount = 1;
                int lineStart = 0;
                for (int i = 0; i < d->BufTextLen; ++i)
                {
                    if (d->Buf[i] == '\n')
                    {
                        ++lineCount;
                        if (i < d->CursorPos)
                        {
                            lineStart = i + 1;
                        }
                    }
                }
                int lineEnd = lineStart;
                while (lineEnd < d->BufTextLen && d->Buf[lineEnd] != '\n')
                {
                    ++lineEnd;
                }
                const int lineLen = lineEnd - lineStart;
                const bool hasSelection = d->SelectionStart != d->SelectionEnd;
                if (d->EventChar == '\n' || d->EventChar == '\r')
                {
                    return (lineCount >= g_maxRows && !hasSelection) ? 1 : 0;
                }
                if (d->EventChar == '\t')
                {
                    return 1;
                }
                return (lineLen >= g_maxChars && !hasSelection) ? 1 : 0;
            }
            // Safety net (e.g. a paste that replaced a selection): same limits, applied as an edit.
            if (d->EventFlag != ImGuiInputTextFlags_CallbackEdit)
            {
                return 0;
            }
            std::string norm;
            int lines = 1;
            int col = 0;
            for (int i = 0; i < d->BufTextLen; ++i)
            {
                char c = d->Buf[i];
                if (c == '\r')
                {
                    continue;
                }
                if (c == '\n')
                {
                    if (lines >= g_maxRows)
                    {
                        break;
                    }
                    norm += '\n';
                    ++lines;
                    col = 0;
                    continue;
                }
                if (c == '\t')
                {
                    c = ' ';
                }
                if (col >= g_maxChars)
                {
                    continue;
                }
                norm += c;
                ++col;
            }
            if (norm != std::string(d->Buf, d->BufTextLen))
            {
                const int cur = d->CursorPos;
                d->DeleteChars(0, d->BufTextLen);
                d->InsertChars(0, norm.c_str());
                d->CursorPos = std::min(cur, static_cast<int>(norm.size()));
                d->SelectionStart = d->SelectionEnd = d->CursorPos;
            }
            return 0;
        }

        // sign_status.txt is rewritten (not deleted) by the Lua side whenever the selection or its
        // text changes, so it is polled a few times a second and only re-read into the edit boxes
        // when SIGN_SEQ changes (a NEW sign was selected) -- otherwise typing would be overwritten.
        auto pollStatus() -> void
        {
            const double now = ImGui::GetTime();
            if (g_lastPoll >= 0.0 && now - g_lastPoll < 0.25)
            {
                return;
            }
            g_lastPoll = now;

            std::ifstream f(STATUS_PATH);
            if (!f)
            {
                return;
            }
            int seq = -1;
            bool hasTarget = false;
            bool haveColor = false;
            bool glowFromStatus = false;
            int fontFromStatus = 1;
            int maxRowsFromStatus = kRows;
            float colorFromStatus[3] = {kDefaultColor[0], kDefaultColor[1], kDefaultColor[2]};
            std::string name;
            std::string rows[kRows];
            std::string line;
            while (std::getline(f, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                auto eq = line.find('=');
                if (eq == std::string::npos)
                {
                    continue;
                }
                const std::string key = line.substr(0, eq);
                const std::string val = line.substr(eq + 1);
                if (key == "SIGN_SEQ")
                {
                    seq = std::atoi(val.c_str());
                }
                else if (key == "SIGN_FONT")
                {
                    for (int i = 0; i < static_cast<int>(std::size(kFontKeys)); ++i)
                    {
                        if (val == kFontKeys[i])
                        {
                            fontFromStatus = i;
                        }
                    }
                }
                else if (key == "SIGN_GLOW")
                {
                    glowFromStatus = (val == "1");
                }
                else if (key == "SIGN_COLOR")
                {
                    int r = 0, g = 0, b = 0;
                    if (std::sscanf(val.c_str(), "%d,%d,%d", &r, &g, &b) == 3)
                    {
                        colorFromStatus[0] = r / 255.0f;
                        colorFromStatus[1] = g / 255.0f;
                        colorFromStatus[2] = b / 255.0f;
                        haveColor = true;
                    }
                }
                else if (key == "SIGN_MAX_ROWS")
                {
                    maxRowsFromStatus = std::clamp(std::atoi(val.c_str()), 1, kRows);
                }
                else if (key == "SIGN_TAB_SEQ")
                {
                    g_tabSeq = std::atoi(val.c_str());
                }
                else if (key == "SIGN_HAS_TARGET")
                {
                    hasTarget = (val == "1");
                }
                else if (key == "SIGN_NAME")
                {
                    name = val;
                }
                else if (key.size() == 7 && key.compare(0, 6, "SIGN_L") == 0 && key[6] >= '1' && key[6] <= '4')
                {
                    rows[key[6] - '1'] = val;
                }
            }
            g_hasTarget = hasTarget;
            g_signName = name;
            g_maxRows = maxRowsFromStatus;
            if (seq != g_seenSeq)
            {
                g_seenSeq = seq;
                // Join the rows with newlines, dropping trailing empty rows.
                int last = kRows - 1;
                while (last > 0 && rows[last].empty())
                {
                    --last;
                }
                std::string joined;
                for (int i = 0; i <= last; ++i)
                {
                    joined += rows[i];
                    if (i < last)
                    {
                        joined += '\n';
                    }
                }
                std::snprintf(g_text, sizeof(g_text), "%s", joined.c_str());
                g_glow = glowFromStatus;
                g_fontIdx = fontFromStatus;
                // The picker follows the newly selected sign's own ink colour.
                for (int c = 0; c < 3; ++c)
                {
                    g_color[c] = haveColor ? colorFromStatus[c] : kDefaultColor[c];
                }
            }
        }
    } // namespace

    auto ConsumeTabRequest() -> bool
    {
        pollStatus();
        if (g_tabSeq < 0)
        {
            return false;
        }
        if (g_seenTabSeq < 0)
        {
            g_seenTabSeq = g_tabSeq; // first read: baseline only
            return false;
        }
        if (g_tabSeq != g_seenTabSeq)
        {
            g_seenTabSeq = g_tabSeq;
            return true;
        }
        return false;
    }

    auto Draw() -> void
    {
        pollStatus();

        ImGui::BeginDisabled(MenuStatus::IsRestoring());
        const ImGuiStyle& style = ImGui::GetStyle();
        const float sp = style.ItemSpacing.x;
        const float W = ImGui::GetContentRegionAvail().x;

        // ---- description -------------------------------------------------------------------------------------------------
        ImGui::TextWrapped("Aim at a text object and press Delete or click on \"Select Object\". Click Apply to assign the text, "
                           "Clear to clear the text, and Reset to remove all customizations.");
        ImGui::TextWrapped("Valid objects include Signs, Wooden Chests, Bale, Box, Barrel, and Sack. Other options may be spawned below.");
        ImGui::Spacing();

        // ---- selection ---------------------------------------------------------------------------------------------------
        ImGui::TextUnformatted("Selected Object");
        const float selectBtnW = 110.0f;
        const float labelBoxW = W - selectBtnW - sp;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, style.Colors[ImGuiCol_FrameBgHovered]);
        ImGui::BeginChild("##sign_target_label", ImVec2(labelBoxW, 28.0f), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::TextUnformatted(g_hasTarget ? g_signName.c_str() : "(no object selected)");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        // Toggle, same idiom as the Move tab's +/- target lock: selects the object you are aiming at, or releases it.
        if (ImGui::Button(g_hasTarget ? "Deselect" : "Select Object", ImVec2(selectBtnW, 28.0f)))
        {
            requestTarget();
        }
        HoverTooltip(g_hasTarget ? "Release the selected object (same as pressing Delete in game)."
                                 : "Select the object you are aiming at (same as pressing Delete in game).");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ---- text (left) + font / colour (right) -------------------------------------------------------------------------
        ImGui::BeginDisabled(!g_hasTarget);
        if (g_maxRows == 1)
        {
            ImGui::TextUnformatted("Text (one line)");
        }
        else
        {
            ImGui::Text("Text (up to %d lines)", g_maxRows);
        }

        const float x0 = ImGui::GetCursorPosX();
        const float y0 = ImGui::GetCursorPosY();
        const float lineH = ImGui::GetTextLineHeight();
        const float frameH = ImGui::GetFrameHeight();
        const float boxW = std::floor(W * 0.41f);
        const float boxH = lineH * kRows + style.FramePadding.y * 2.0f + 2.0f;
        const float counterW = 46.0f;

        // Characters that fit on one row of the box (widest glyph, so a full row can never wrap or scroll).
        {
            const float inner = boxW - style.FramePadding.x * 2.0f - 2.0f;
            const float glyph = std::max(1.0f, ImGui::CalcTextSize("M").x);
            g_maxChars = std::clamp(static_cast<int>(inner / glyph), 8, kMaxChars);
        }

        ImGui::InputTextMultiline("##sign_text", g_text, sizeof(g_text), ImVec2(boxW, boxH),
                                  ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackEdit |
                                      ImGuiInputTextFlags_NoHorizontalScroll,
                                  textEditFilter);

        // Per-row counters beside the box, one per line (n/limit), red when a row is full.
        {
            int counts[kRows] = {};
            int lines = 1;
            for (const char* p = g_text; *p; ++p)
            {
                if (*p == '\n')
                {
                    ++lines;
                }
                else if (lines <= kRows)
                {
                    ++counts[lines - 1];
                }
            }
            const ImVec4 warn(1.0f, 0.35f, 0.35f, 1.0f);
            const ImVec4 dim = style.Colors[ImGuiCol_TextDisabled];
            for (int i = 0; i < g_maxRows; ++i)
            {
                ImGui::SetCursorPos(ImVec2(x0 + boxW + 6.0f, y0 + style.FramePadding.y + 1.0f + i * lineH));
                ImGui::PushStyleColor(ImGuiCol_Text, counts[i] >= g_maxChars ? warn : dim);
                ImGui::Text("%d/%d", counts[i], g_maxChars);
                ImGui::PopStyleColor();
            }
        }

        // Right column, same top as the box.
        const float rx = x0 + boxW + counterW + 6.0f;
        const float rightW = (x0 + W) - rx;

        // Row 1: Font [dropdown] [Apply] [Clear] [Reset]
        {
            ImGui::SetCursorPos(ImVec2(rx, y0));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Font");
            ImGui::SameLine();
            const float btnW = 58.0f;
            const float fontLabelW = ImGui::CalcTextSize("Font").x;
            const float comboW = std::max(60.0f, rightW - fontLabelW - 3.0f * btnW - 4.0f * sp);
            ImGui::SetNextItemWidth(comboW);
            if (ImGui::BeginCombo("##sign_font", kFontLabels[g_fontIdx]))
            {
                for (int i = 0; i < static_cast<int>(std::size(kFontLabels)); ++i)
                {
                    const bool selected = (g_fontIdx == i);
                    if (ImGui::Selectable(kFontLabels[i], selected))
                    {
                        g_fontIdx = i;
                        requestApply();
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            HoverTooltip("The font for this object's text. Greengoth and Script need the optional font pak; without it they show the built-in font.");
            ImGui::SameLine();
            if (ImGui::Button("Apply", ImVec2(btnW, frameH)))
            {
                requestApply();
            }
            HoverTooltip("Put this text on the selected object.");
            ImGui::SameLine();
            if (ImGui::Button("Clear", ImVec2(btnW, frameH)))
            {
                g_text[0] = '\0';
                requestClear();
            }
            HoverTooltip("Erase the text. Colour, font and glow are kept.");
            ImGui::SameLine();
            if (ImGui::Button("Reset", ImVec2(btnW, frameH)))
            {
                g_text[0] = '\0';
                for (int c = 0; c < 3; ++c)
                {
                    g_color[c] = kDefaultColor[c];
                }
                g_glow = false;
                g_fontIdx = 1;
                requestReset();
            }
            HoverTooltip("Remove the text and every customization (colour, font, glow) and return the object to its default.");
        }

        // Rows 2 and 3: colour. The preset swatches run from the picker's left edge to the buttons' right edge; the picker and
        // the last-used swatch are each two swatches wide; "Add Glow" sits over the last swatch.
        const float y1 = y0 + frameH + style.ItemSpacing.y;
        const float labelW = ImGui::CalcTextSize("Text Color").x + sp;
        const float sx0 = rx + labelW;
        const float rowW = (rx + rightW) - sx0;
        const float sw = std::max(12.0f, std::floor((rowW - 9.0f * sp) / 10.0f));
        const float twoW = 2.0f * sw + sp;
        {
            ImGui::SetCursorPos(ImVec2(rx, y1));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Text Color");

            // Same control the Photo Mode Lights use: a swatch opening ImGui's standard picker. Applied when released, so
            // dragging does not rebuild the text every frame. Bright border so it stands out.
            ImGui::SetCursorPos(ImVec2(sx0, y1));
            // A plain ColorButton of exactly twoW x frameH (ColorEdit3 ignores the width for its preview swatch) that opens a
            // popup with the full picker. The colour is applied when the popup closes, so dragging does not rebuild the text.
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.95f, 0.85f, 0.45f, 1.0f));
            const bool pickerClicked = ImGui::ColorButton("##sign_color", ImVec4(g_color[0], g_color[1], g_color[2], 1.0f),
                                                          ImGuiColorEditFlags_NoTooltip, ImVec2(twoW, frameH));
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            if (pickerClicked)
            {
                ImGui::OpenPopup("##sign_color_popup");
            }
            HoverTooltip("Click for the full colour picker. Applies when you close it.");
            if (ImGui::BeginPopup("##sign_color_popup"))
            {
                if (ImGui::ColorPicker3("##sign_picker", g_color, ImGuiColorEditFlags_PickerHueWheel))
                {
                    g_pickerDirty = true;
                }
                ImGui::EndPopup();
            }
            else if (g_pickerDirty)
            {
                g_pickerDirty = false;
                requestApply();
            }

            // Last-used swatch: white by default, then the colour most recently applied, so it can be carried to another object.
            // White 1px outline, same as the preset swatches, so dark colours do not sink into the dark background.
            ImGui::SetCursorPos(ImVec2(sx0 + twoW + sp, y1));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            if (ImGui::ColorButton("##sw_last", ImVec4(g_lastColor[0], g_lastColor[1], g_lastColor[2], 1.0f),
                                   ImGuiColorEditFlags_NoTooltip, ImVec2(twoW, frameH)))
            {
                g_color[0] = g_lastColor[0];
                g_color[1] = g_lastColor[1];
                g_color[2] = g_lastColor[2];
                requestApply();
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            HoverTooltip("Last colour applied (white until you apply one).");

            // Add Glow (was "Ignore lighting"): text on its own lighting channel so world light and shadow do not reach it.
            const float cbX = sx0 + 9.0f * (sw + sp);
            const float glowLabelW = ImGui::CalcTextSize("Add Glow").x;
            ImGui::SetCursorPos(ImVec2(cbX - sp - glowLabelW, y1));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Add Glow");
            ImGui::SetCursorPos(ImVec2(cbX, y1));
            if (ImGui::Checkbox("##sign_glow", &g_glow))
            {
                requestApply();
            }
            HoverTooltip("Text keeps its own colour and brightness in shadow and at night. Saved with the object.");
        }
        {
            struct Swatch
            {
                const char* id;
                float rgb[3];
                const char* tip;
            };
            static const Swatch swatches[] = {
                {"##sw_red", {0.80f, 0.10f, 0.10f}, "Red"},
                {"##sw_orange", {0.95f, 0.50f, 0.10f}, "Orange"},
                {"##sw_yellow", {0.95f, 0.85f, 0.15f}, "Yellow"},
                {"##sw_green", {0.15f, 0.60f, 0.20f}, "Green"},
                {"##sw_blue", {0.15f, 0.35f, 0.85f}, "Blue"},
                {"##sw_purple", {0.55f, 0.20f, 0.70f}, "Purple"},
                {"##sw_black", {0.03f, 0.03f, 0.03f}, "Black"},
                {"##sw_white", {0.95f, 0.95f, 0.92f}, "White"},
                {"##sw_lbrown", {0.60f, 0.42f, 0.25f}, "Light brown"},
                {"##sw_dbrown", {25.0f / 255.0f, 15.0f / 255.0f, 5.0f / 255.0f}, "Dark brown (default)"},
            };
            const float y2 = y1 + frameH + style.ItemSpacing.y;
            int k = 0;
            // Same thin white outline as the last-used box, so every colour reads against the dark background.
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            for (const Swatch& swt : swatches)
            {
                ImGui::SetCursorPos(ImVec2(sx0 + k * (sw + sp), y2));
                if (ImGui::ColorButton(swt.id, ImVec4(swt.rgb[0], swt.rgb[1], swt.rgb[2], 1.0f), ImGuiColorEditFlags_NoTooltip,
                                       ImVec2(sw, sw)))
                {
                    g_color[0] = swt.rgb[0];
                    g_color[1] = swt.rgb[1];
                    g_color[2] = swt.rgb[2];
                    requestApply();
                }
                HoverTooltip(swt.tip);
                ++k;
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            // Continue below whichever column is taller (the box, or the swatch row).
            const float bottom = std::max(y0 + boxH, y2 + sw);
            ImGui::SetCursorPos(ImVec2(x0, bottom + style.ItemSpacing.y));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }
        ImGui::EndDisabled(); // !g_hasTarget

        // ---- additional items --------------------------------------------------------------------------------------------
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("Additional Items");
        {
            const float spawnBtnW = 80.0f;
            ImGui::SetNextItemWidth(boxW); // same right edge as the text box above
            if (ImGui::BeginCombo("##sign_special_items", kSpecialItems[g_specialIdx]))
            {
                for (int i = 0; i < static_cast<int>(std::size(kSpecialItems)); ++i)
                {
                    const bool selected = (g_specialIdx == i);
                    if (ImGui::Selectable(kSpecialItems[i], selected))
                    {
                        g_specialIdx = i;
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Spawn", ImVec2(spawnBtnW, 0.0f)))
            {
                requestSpawnSpecial(g_specialIdx);
            }
            HoverTooltip("Spawn the selected item. It follows your view like a normal spawn -- confirm with Numpad 0 (or the Confirm button on the Spawn tab).");
        }

        ImGui::EndDisabled(); // MenuStatus::IsRestoring()
    }
} // namespace RC::LivingBaseSpawnMenu::SignMenu
