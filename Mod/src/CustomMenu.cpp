#include <CustomMenu.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <MenuStatus.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <imgui.h>

namespace RC::LivingBaseSpawnMenu::CustomMenu
{
    namespace
    {
        // Same "ue4ss/Mods/LivingBase/..." CWD-relative convention as every other request file in
        // this bridge -- see SpawnMenu.cpp's own REQUEST_PATH comment for why a bare relative path
        // is wrong here.
        constexpr const char* COLOR_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_color_request.txt";
        constexpr const char* COLOR_READ_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_color_read_request.txt";
        constexpr const char* COLOR_STATUS_PATH = "ue4ss/Mods/LivingBase/custom_color_status.txt";

        // The real 24-entry CRV_CharacterClothPalette, decoded 2026-09-07 (see
        // WINDROSE_MODDING_NOTES.md's cloth-palette addendum and the published "Cloth Color
        // Palette" artifact -- FModel's own "Save Properties" JSON export, since the offline binary
        // parser that worked for hair/eye doesn't apply to this asset class). Each entry is a real
        // 3-stop gradient (Time 0 / 0.5 / 1.0), not a flat color -- values here are the same
        // linear->sRGB gamma-corrected floats (0..1) as that artifact's own hex swatches, just
        // pre-converted for ImGui instead of stored as hex strings. KEEP IN SYNC BY HAND with
        // config.lua's Config.CPD_CLOTH_COLOR_NAMES if that list is ever revised -- this C++ DLL
        // has no way to read config.lua at build time, same "manually kept in sync" situation
        // MoveMenu.cpp's own PRECISION_LABELS/PRECISION_SCALES are already in with main.lua.
        struct ClothColor
        {
            const char* name;
            float stops[3][3]; // [stop 0/1/2][R,G,B], 0..1, sRGB
        };
        constexpr ClothColor kClothColors[24] = {
            {"Harp", {{0.4039f, 0.3961f, 0.3529f}, {0.5608f, 0.5882f, 0.6000f}, {0.7804f, 0.8745f, 0.8510f}}},
            {"IceBerg", {{0.4902f, 0.5255f, 0.6157f}, {0.6667f, 0.7137f, 0.7373f}, {0.7137f, 0.7333f, 0.7373f}}},
            {"Ivory", {{0.4000f, 0.2980f, 0.2275f}, {0.4863f, 0.4863f, 0.4235f}, {0.7020f, 0.6353f, 0.4667f}}},
            {"BlueCharcoal", {{0.0000f, 0.0000f, 0.0000f}, {0.1529f, 0.1412f, 0.1882f}, {0.2745f, 0.3216f, 0.4118f}}},
            {"BlackOlive", {{0.0902f, 0.0941f, 0.0980f}, {0.1451f, 0.1529f, 0.1412f}, {0.1529f, 0.1490f, 0.1176f}}},
            {"WoodBark", {{0.0980f, 0.0157f, 0.0157f}, {0.1490f, 0.1020f, 0.1020f}, {0.2902f, 0.2431f, 0.2431f}}},
            {"Crimson", {{0.2667f, 0.0902f, 0.1490f}, {0.6118f, 0.0000f, 0.0000f}, {0.7647f, 0.3216f, 0.2667f}}},
            {"Carmine", {{0.1608f, 0.1647f, 0.2314f}, {0.4706f, 0.0039f, 0.1373f}, {0.3412f, 0.2588f, 0.1882f}}},
            {"Bordeaux", {{0.1490f, 0.0588f, 0.2314f}, {0.3294f, 0.1098f, 0.1647f}, {0.4000f, 0.1882f, 0.2353f}}},
            {"PaleOrange", {{0.5176f, 0.2039f, 0.1255f}, {0.6157f, 0.3333f, 0.1137f}, {0.5529f, 0.4627f, 0.3686f}}},
            {"YellowGreen", {{0.4980f, 0.1882f, 0.1961f}, {0.7098f, 0.4941f, 0.1961f}, {0.4235f, 0.4549f, 0.1843f}}},
            {"PaleGold", {{0.4824f, 0.3294f, 0.2235f}, {0.7098f, 0.5843f, 0.2902f}, {0.7373f, 0.6824f, 0.4196f}}},
            {"EmeraldGreen", {{0.2000f, 0.1255f, 0.1137f}, {0.1098f, 0.2941f, 0.0275f}, {0.2471f, 0.3922f, 0.2118f}}},
            {"ColdGreen", {{0.2118f, 0.1961f, 0.3216f}, {0.1961f, 0.3373f, 0.1725f}, {0.2235f, 0.4392f, 0.4000f}}},
            {"OliveGreen", {{0.2000f, 0.3216f, 0.2627f}, {0.2510f, 0.3255f, 0.1373f}, {0.3647f, 0.3020f, 0.1451f}}},
            {"LightBlue", {{0.2157f, 0.2745f, 0.4745f}, {0.2902f, 0.3294f, 0.4706f}, {0.6078f, 0.6353f, 0.7608f}}},
            {"NavyBlue", {{0.1412f, 0.1059f, 0.1725f}, {0.1333f, 0.2000f, 0.2941f}, {0.3059f, 0.3647f, 0.3137f}}},
            {"OceanBlue", {{0.1882f, 0.2510f, 0.2941f}, {0.2863f, 0.4863f, 0.5373f}, {0.3882f, 0.5529f, 0.5686f}}},
            {"Purple", {{0.1686f, 0.1922f, 0.3490f}, {0.2941f, 0.1922f, 0.3882f}, {0.7765f, 0.4353f, 0.7882f}}},
            {"Violet", {{0.0863f, 0.0549f, 0.1490f}, {0.1725f, 0.1451f, 0.2667f}, {0.2863f, 0.2667f, 0.3647f}}},
            {"Lilac", {{0.4353f, 0.0941f, 0.1765f}, {0.6275f, 0.4627f, 0.7255f}, {0.7373f, 0.5059f, 0.5490f}}},
            {"ChocolateBrown", {{0.2196f, 0.0941f, 0.1608f}, {0.2471f, 0.1059f, 0.0275f}, {0.3608f, 0.1882f, 0.1176f}}},
            {"BrownLeather", {{0.1451f, 0.1765f, 0.2196f}, {0.2196f, 0.0784f, 0.0745f}, {0.4000f, 0.2588f, 0.1647f}}},
            {"BrownCopper", {{0.1765f, 0.1725f, 0.0549f}, {0.2471f, 0.1333f, 0.0588f}, {0.3294f, 0.2706f, 0.1176f}}},
        };
        constexpr int kClothColorCount = 24;

        // The 7-row list. `key` is the exact string sent over custom_color_request.txt/read back
        // from custom_color_status.txt -- KEEP IN SYNC BY HAND with config.lua's
        // Config.CUSTOM_TAB_CLOTH_CATEGORIES (same key strings; bodyPart resolution itself stays
        // Lua's own concern, see this file's own header comment). `slotCount` is how many of the 3
        // CPD slots this category actually shows a separate swatch for (2026-09-08, RedFalcon: "so
        // each color # has its own swatch button where applicable") -- 3 for the pieces that
        // genuinely use all 3 slots, 1 for Waist/Cape (see Config.CPD_BODYPART_COLOR_INFO's own
        // notes on why those two only have ONE real slot). `soloSlot` is which of the 3 raw wire
        // slots a slotCount==1 row's single swatch actually reads/writes -- Waist only reads
        // Color3; Cape's real slot varies by piece, so Color1 is just this row's own representative
        // choice (write-time still fans it out to all 3 uniformly, matching the existing "write the
        // same value to all 3 slots to be safe" rule).
        struct Category
        {
            const char* key;
            const char* label;
            int slotCount;
            int soloSlot; // only meaningful when slotCount == 1
        };
        constexpr Category kCategories[7] = {
            {"TORSO", "Torso", 3, 0}, {"LEGS", "Legs", 3, 0},  {"WAIST", "Waist", 1, 2},
            {"HANDS", "Hands", 3, 0}, {"FEET", "Feet", 3, 0}, {"HAT", "Hat", 3, 0},
            {"CAPE", "Cape", 1, 0},
        };
        constexpr int kCategoryCount = 7;

        // g_selected[i][s]: -1 = nothing picked yet for that (category, slot). Persists for as long
        // as the DLL is loaded (same lifetime as MoveMenu.cpp's g_precision_idx) -- picking a color
        // doesn't apply it by itself, only the Apply button does, so this is deliberately "sticky"
        // across frames/tabs rather than reset on every draw. Only index 0 is meaningful for a
        // slotCount==1 row.
        int g_selected[kCategoryCount][3] = {};

        // Read Current: request/response is asynchronous (main.lua polls every 400ms), so this
        // tracks a pending read across frames rather than blocking. Timeout is generous (2s) --
        // this only ever fails to resolve if LivingBase itself isn't loaded/running, in which case
        // spinning forever would be a worse symptom than just giving up quietly.
        bool g_readPending = false;
        std::chrono::steady_clock::time_point g_readRequestedAt{};
        constexpr auto kReadTimeout = std::chrono::seconds(2);

        auto HoverTooltip(const char* text) -> void
        {
            if (text && ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", text);
            }
        }

        // Same truncate-with-full-text-tooltip behavior as MoveMenu.cpp's own DrawTruncatedText --
        // duplicated locally rather than shared, matching this codebase's existing convention of
        // small per-file ImGui helpers (MoveMenu.cpp/SpawnMenu.cpp each keep their own too).
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

        // Draws a clickable rect showing the named palette entry's real 3-stop gradient (or a
        // muted "nothing picked" look for paletteIdx < 0), and returns true the frame it's
        // clicked. Two AddRectFilledMultiColor halves (stop0->stop1, stop1->stop2) approximate the
        // same 3-stop horizontal gradient bar the published artifact itself uses.
        auto GradientSwatchButton(const char* imguiId, int paletteIdx, ImVec2 size) -> bool
        {
            ImGui::PushID(imguiId);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 topLeft = pos;
            ImVec2 botRight = ImVec2(pos.x + size.x, pos.y + size.y);

            if (paletteIdx >= 0 && paletteIdx < kClothColorCount)
            {
                const ClothColor& c = kClothColors[paletteIdx];
                auto toU32 = [](const float rgb[3]) {
                    return ImGui::ColorConvertFloat4ToU32(ImVec4(rgb[0], rgb[1], rgb[2], 1.0f));
                };
                ImU32 c0 = toU32(c.stops[0]);
                ImU32 c1 = toU32(c.stops[1]);
                ImU32 c2 = toU32(c.stops[2]);
                float midX = pos.x + size.x * 0.5f;
                // AddRectFilledMultiColor(p_min, p_max, col_upr_left, col_upr_right, col_bot_right, col_bot_left)
                dl->AddRectFilledMultiColor(topLeft, ImVec2(midX, botRight.y), c0, c1, c1, c0);
                dl->AddRectFilledMultiColor(ImVec2(midX, topLeft.y), botRight, c1, c2, c2, c1);
            }
            else
            {
                dl->AddRectFilled(topLeft, botRight, IM_COL32(58, 54, 48, 255));
                // A faint diagonal hint that this swatch is empty, not just a very dark color.
                dl->AddLine(topLeft, botRight, IM_COL32(90, 84, 74, 200), 1.0f);
            }
            dl->AddRect(topLeft, botRight, IM_COL32(0, 0, 0, 130));

            bool clicked = ImGui::InvisibleButton("##swatch", size);
            ImGui::PopID();
            return clicked;
        }

        // Shared by every swatch's own picker popup -- a grid of all 24 real colors, closing the
        // popup and writing `outIdx` the moment one is clicked.
        auto DrawColorPickerPopup(const char* popupId, int& outIdx) -> void
        {
            if (!ImGui::BeginPopup(popupId))
            {
                return;
            }
            constexpr float kPickW = 96.0f;
            constexpr float kPickH = 26.0f;
            const float pickAvail = ImGui::GetContentRegionAvail().x;
            const int perRow = std::max(1, static_cast<int>(pickAvail / (kPickW + ImGui::GetStyle().ItemSpacing.x)));
            for (int p = 0; p < kClothColorCount; ++p)
            {
                if (p % perRow != 0)
                {
                    ImGui::SameLine();
                }
                ImGui::PushID(p);
                if (GradientSwatchButton("pick_swatch", p, ImVec2(kPickW, kPickH)))
                {
                    outIdx = p;
                    ImGui::CloseCurrentPopup();
                }
                HoverTooltip(kClothColors[p].name);
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }

        auto writeColorRequest() -> void
        {
            std::ofstream f(COLOR_REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_color_request.txt\n"));
                return;
            }
            auto slotOrDash = [](int v) { return v >= 0 ? std::to_string(v) : std::string("-"); };
            for (int i = 0; i < kCategoryCount; ++i)
            {
                const Category& cat = kCategories[i];
                bool touched = false;
                std::string c1, c2, c3;
                if (cat.slotCount == 1)
                {
                    touched = g_selected[i][0] >= 0;
                    c1 = c2 = c3 = slotOrDash(g_selected[i][0]); // fan out to all 3, as always
                }
                else
                {
                    touched = g_selected[i][0] >= 0 || g_selected[i][1] >= 0 || g_selected[i][2] >= 0;
                    c1 = slotOrDash(g_selected[i][0]);
                    c2 = slotOrDash(g_selected[i][1]);
                    c3 = slotOrDash(g_selected[i][2]);
                }
                if (touched)
                {
                    f << "COLOR:" << cat.key << ":" << c1 << ":" << c2 << ":" << c3 << "\n";
                }
            }
        }

        // Kicks off a Read Current cycle: clears any stale response first (a leftover from a
        // previous click could otherwise be misread as this one's answer the instant it's
        // written), then writes the request file and starts the pending timer.
        auto requestReadCurrent() -> void
        {
            std::remove(COLOR_STATUS_PATH);
            std::ofstream f(COLOR_READ_REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_color_read_request.txt\n"));
                return;
            }
            f.close();
            g_readPending = true;
            g_readRequestedAt = std::chrono::steady_clock::now();
        }

        // Polls for custom_color_status.txt while a read is pending -- called once per Draw(),
        // cheap (a single ifstream open, most frames find nothing since main.lua's own poll is
        // 400ms) regardless of pending state.
        auto pollReadCurrentResult() -> void
        {
            if (!g_readPending)
            {
                return;
            }
            if (std::chrono::steady_clock::now() - g_readRequestedAt > kReadTimeout)
            {
                g_readPending = false;
                Output::send<LogLevel::Warning>(STR("[LivingBaseSpawnMenu] CustomMenu: Read Current timed out (is LivingBase loaded?)\n"));
                return;
            }
            std::ifstream f(COLOR_STATUS_PATH);
            if (!f)
            {
                return; // not written yet -- try again next frame
            }
            std::string line;
            while (std::getline(f, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                std::istringstream ss(line);
                std::string key, v1, v2, v3;
                if (!std::getline(ss, key, ':') || !std::getline(ss, v1, ':') || !std::getline(ss, v2, ':') || !std::getline(ss, v3, ':'))
                {
                    continue;
                }
                for (int i = 0; i < kCategoryCount; ++i)
                {
                    if (key != kCategories[i].key)
                    {
                        continue;
                    }
                    auto apply = [](const std::string& v) { return v == "-" ? -1 : std::atoi(v.c_str()); };
                    if (kCategories[i].slotCount == 1)
                    {
                        // Only this row's own soloSlot reflects the target's real state -- the
                        // other 2 raw wire values exist (every piece has 3 real CPD floats) but
                        // aren't what this single swatch represents.
                        const std::string& solo = kCategories[i].soloSlot == 0 ? v1 : (kCategories[i].soloSlot == 1 ? v2 : v3);
                        g_selected[i][0] = apply(solo);
                    }
                    else
                    {
                        g_selected[i][0] = apply(v1);
                        g_selected[i][1] = apply(v2);
                        g_selected[i][2] = apply(v3);
                    }
                    break;
                }
            }
            g_readPending = false;
        }
    } // namespace

    auto Draw() -> void
    {
        // g_selected's own {} initializer zero-fills (all slots would start at "index 0/Harp
        // picked", not "(none)") -- fix that up to -1 exactly once, lazily, rather than needing a
        // separate explicit init entry point nothing else in this bridge's menu files has either.
        {
            static bool initialized = false;
            if (!initialized)
            {
                initialized = true;
                for (auto& row : g_selected)
                {
                    row[0] = row[1] = row[2] = -1;
                }
            }
        }

        pollReadCurrentResult();

        // Same "Selected Target" readout as MoveMenu.cpp -- see MenuStatus.hpp for how this gets
        // here. The whole point of RedFalcon's own request for this: always obvious which NPC an
        // Apply click is about to affect, matching the Tools tab's own target-lock convention.
        const float avail = ImGui::GetContentRegionAvail().x;
        ImGui::TextUnformatted("Selected Target:");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBgHovered]);
        ImGui::BeginChild("##custom_target_label", ImVec2(avail, 28.0f), true, ImGuiWindowFlags_NoScrollbar);
        DrawTruncatedText(MenuStatus::TargetLabel(), avail - 16.0f);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::TextDisabled("Press Num + (in-game, or from the Tools tab) to select/release target lock");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const bool hasTarget = !MenuStatus::TargetLabel().empty();
        ImGui::BeginDisabled(MenuStatus::IsRestoring() || !hasTarget);

        if (ImGui::Button(g_readPending ? "Reading..." : "Read Current", ImVec2(150.0f, 0.0f)))
        {
            requestReadCurrent();
        }
        HoverTooltip("Populate every swatch below from the target's OWN current colors.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        constexpr float kSwatchW = 75.0f; // half of the original 150 (RedFalcon, 2026-09-08)
        constexpr float kSwatchH = 26.0f;
        constexpr float kLabelW = 70.0f;
        const float swatchGap = ImGui::GetStyle().ItemSpacing.x;

        bool anySelected = false;
        for (int i = 0; i < kCategoryCount; ++i)
        {
            const Category& cat = kCategories[i];
            ImGui::PushID(i);

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(cat.label);
            ImGui::SameLine(kLabelW);

            const int slots = cat.slotCount;
            for (int s = 0; s < slots; ++s)
            {
                if (s > 0)
                {
                    ImGui::SameLine(0.0f, swatchGap);
                }
                ImGui::PushID(s);
                const int slotIdx = (slots == 1) ? 0 : s;
                if (GradientSwatchButton("row_swatch", g_selected[i][slotIdx], ImVec2(kSwatchW, kSwatchH)))
                {
                    ImGui::OpenPopup("##color_picker");
                }
                if (g_selected[i][slotIdx] >= 0)
                {
                    HoverTooltip(kClothColors[g_selected[i][slotIdx]].name);
                    anySelected = true;
                }
                else
                {
                    HoverTooltip("Click to choose a color");
                }
                DrawColorPickerPopup("##color_picker", g_selected[i][slotIdx]);
                ImGui::PopID();
            }

            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Clear Selections", ImVec2(150.0f, 0.0f)))
        {
            for (auto& row : g_selected)
            {
                row[0] = row[1] = row[2] = -1;
            }
        }
        HoverTooltip("Reset every row above to (none) without touching the target's current colors.");

        ImGui::SameLine();
        ImGui::BeginDisabled(!anySelected);
        if (ImGui::Button("Apply", ImVec2(150.0f, 0.0f)))
        {
            writeColorRequest();
        }
        ImGui::EndDisabled();
        HoverTooltip(anySelected ? "Apply every picked color above to the target, in one click."
                                  : "Pick at least one color above first.");

        ImGui::EndDisabled(); // IsRestoring() || !hasTarget

        if (!hasTarget)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Target-lock something first (Num +) to enable this panel.");
        }
    }
} // namespace RC::LivingBaseSpawnMenu::CustomMenu
