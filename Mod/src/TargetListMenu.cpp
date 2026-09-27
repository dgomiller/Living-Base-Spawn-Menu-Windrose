#include <TargetListMenu.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <MenuStatus.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <imgui.h>

namespace RC::LivingBaseSpawnMenu::TargetListMenu
{
    namespace
    {
        // Same "ue4ss/Mods/LivingBase/..." CWD-relative convention every other file in this bridge
        // uses -- see SpawnMenu.cpp's own REQUEST_PATH comment for why a bare relative path is wrong.
        constexpr const char* REQUEST_PATH = "ue4ss/Mods/LivingBase/target_list_request.txt";
        constexpr const char* STATUS_PATH = "ue4ss/Mods/LivingBase/target_list_status.txt";

        struct ResultRow
        {
            std::string label;
            float distM{};
            std::string category;
        };

        std::vector<ResultRow> g_results;
        bool g_hasScanned = false;

        // Category filters -- all checked by default (RedFalcon's own spec).
        bool g_wantPeople = true;
        bool g_wantMonsterous = true;
        bool g_wantAnimals = true;
        bool g_wantDecor = true;

        // Scan Radius dropdown -- fixed 5-step list in meters, default 10m (RedFalcon's own spec:
        // "3, 5, 8, 10, 15 with default set to 10"). Converted to Unreal Units on the LUA side
        // (Spawner.ScanTargetList), not here -- this side only ever sends the plain meter value,
        // same "C++ never touches game state/units directly" split every other request in this
        // bridge follows.
        constexpr int kRadiusOptionsM[] = {3, 5, 8, 10, 15};
        int g_radiusIdx = 3; // index of "10"

        auto HoverTooltip(const char* text) -> void
        {
            if (text && ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", text);
            }
        }

        // Duplicated one-shot truncate-write helper (2026-09-26) -- same "small helper, not worth a
        // shared header" tolerance SpawnMenu.cpp's write_request/MoveMenu.cpp's queueLine already
        // established for this bridge. Both SCAN and TARGET actions go through this SAME request
        // file/verb grammar -- see main.lua's BeltStrapPolls.targetList for the exact parser.
        auto writeRequest(const std::string& line) -> void
        {
            std::ofstream f(REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] TargetListMenu: failed to write target_list_request.txt\n"));
                return;
            }
            f << line << "\n";
        }

        auto requestScan() -> void
        {
            std::ostringstream line;
            line << "SCAN:" << kRadiusOptionsM[g_radiusIdx]
                 << ":" << (g_wantPeople ? 1 : 0)
                 << ":" << (g_wantMonsterous ? 1 : 0)
                 << ":" << (g_wantAnimals ? 1 : 0)
                 << ":" << (g_wantDecor ? 1 : 0);
            writeRequest(line.str());
        }

        auto requestTarget(int index) -> void
        {
            writeRequest("TARGET:" + std::to_string(index));
        }

        auto requestMark(bool on) -> void
        {
            writeRequest(std::string("MARK:") + (on ? "1" : "0"));
        }

        // "Mark Target" checkbox (2026-09-27, RedFalcon: a highlight-material toggle for whatever's
        // currently locked -- "if checked and targeted, do the thing ... if untargeted return to
        // normal"). All the actual apply/restore/reconcile logic lives Lua-side
        // (Spawner.SetMarkTargetEnabled/UpdateMarkTargetHighlight) since it has to keep tracking
        // whatever's locked even as the lock changes out from under this checkbox (Target List row,
        // Numpad+, tab close, etc.) -- this side is just a dumb toggle that fires one request on
        // change, same "C++ never touches game state directly" split as everything else here.
        bool g_markTarget = false;

        // Same "+/-" lock idiom MoveMenu.cpp's own Selected Target row uses (RedFalcon: "add the +/-
        // button next to selected target, like on the other screens") -- duplicated request-writing
        // rather than shared (this bridge's own established tolerance), but reuses move_request.txt
        // and the exact same "ACTION:TARGET_LOCK" verb Lua already handles (main.lua's
        // handleMoveMenuTargetLock, the same one Numpad+ and the Move tab's own button send) so no
        // Lua-side change is needed for this button specifically.
        constexpr const char* MOVE_REQUEST_PATH = "ue4ss/Mods/LivingBase/move_request.txt";
        auto queueMoveAction(const char* name) -> void
        {
            std::ofstream f(MOVE_REQUEST_PATH, std::ios::app);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] TargetListMenu: failed to write move_request.txt\n"));
                return;
            }
            f << "ACTION:" << name << "\n";
        }

        // pollScanStatus() -- one-shot read-then-delete, same convention as CustomMenu.cpp's own
        // pollSocketAccStatus: main.lua's BeltStrapPolls.targetList writes this file exactly once,
        // right after a scan finishes, so there's nothing to compare against -- any time it exists
        // at all, it's a fresh result set waiting to be picked up. Cheap to call every frame (a
        // failed ifstream open on a file that doesn't exist yet is the common case).
        auto pollScanStatus() -> void
        {
            std::ifstream f(STATUS_PATH);
            if (!f)
            {
                return;
            }
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            f.close();
            std::remove(STATUS_PATH);

            std::vector<ResultRow> parsed;
            std::istringstream stream(content);
            std::string line;
            while (std::getline(stream, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (line.rfind("COUNT=", 0) == 0)
                {
                    continue; // informational only -- parsed.size() is the real count
                }
                if (line.rfind("ITEM_", 0) != 0)
                {
                    continue;
                }
                auto eq = line.find('=');
                if (eq == std::string::npos)
                {
                    continue;
                }
                // "<label>|<distMeters>|<category>"
                std::string payload = line.substr(eq + 1);
                auto p1 = payload.find('|');
                if (p1 == std::string::npos)
                {
                    continue;
                }
                auto p2 = payload.find('|', p1 + 1);
                if (p2 == std::string::npos)
                {
                    continue;
                }
                ResultRow row;
                row.label = payload.substr(0, p1);
                row.distM = std::strtof(payload.substr(p1 + 1, p2 - p1 - 1).c_str(), nullptr);
                row.category = payload.substr(p2 + 1);
                parsed.push_back(std::move(row));
            }
            g_results = std::move(parsed);
            g_hasScanned = true;
        }

        // Flat list, nearest-first (already sorted on the Lua side) -- deliberately NOT grouped
        // into per-category tree branches: RedFalcon's own spec asks for the checkboxes to FILTER
        // what the scan returns, and the populated list to be "ordered by closest to farthest,"
        // which is a single ordering, not a per-category one. Each leaf mirrors CustomMenu.cpp's
        // own Poses-tree row idiom exactly (RedFalcon: "There will be + next to the items in the
        // list, like in the poses tree") -- a small "+" button that acts immediately, followed by
        // the name, rather than SpawnMenu's own select-then-confirm Selectable rows.
        auto drawResultsList() -> void
        {
            if (!g_hasScanned)
            {
                ImGui::TextDisabled("(no scan yet -- set your filters and radius, then press Scan)");
                return;
            }
            if (g_results.empty())
            {
                ImGui::TextDisabled("(nothing found in range with the current filters)");
                return;
            }
            for (int i = 0; i < static_cast<int>(g_results.size()); ++i)
            {
                const ResultRow& row = g_results[i];
                ImGui::PushID(i);
                if (ImGui::SmallButton("+"))
                {
                    requestTarget(i);
                }
                HoverTooltip("Target-lock this actor");
                ImGui::SameLine();
                ImGui::Text("%s -- %.1fm (%s)", row.label.c_str(), row.distM, row.category.c_str());
                ImGui::PopID();
            }
        }

        // Right pane: Selected Target box (same box+lock-button idiom MoveMenu.cpp's own "Selected
        // Target" row uses -- duplicated rather than shared, same tolerance as everything else in
        // this bridge), category filter checkboxes, Scan Radius dropdown + Scan button, and the
        // large live distance readout.
        auto drawRightPanel() -> void
        {
            const float avail = ImGui::GetContentRegionAvail().x;
            const bool hasTarget = !MenuStatus::TargetLabel().empty();

            ImGui::TextUnformatted("Selected Target:");
            const float lockBtnW = 28.0f;
            const float labelBoxW = avail - lockBtnW - ImGui::GetStyle().ItemSpacing.x;
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBgHovered]);
            ImGui::BeginChild("##targetlist_target_label", ImVec2(labelBoxW, 28.0f), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextUnformatted(hasTarget ? MenuStatus::TargetLabel().c_str() : "(nothing targeted)");
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button(hasTarget ? "-" : "+", ImVec2(lockBtnW, 28.0f)))
            {
                queueMoveAction("TARGET_LOCK");
            }
            HoverTooltip(hasTarget ? "Release target lock" : "Lock the currently hovered target");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextUnformatted("Categories");
            ImGui::Checkbox("People", &g_wantPeople);
            ImGui::Checkbox("Monsterous", &g_wantMonsterous);
            ImGui::Checkbox("Animals", &g_wantAnimals);
            ImGui::Checkbox("Decor", &g_wantDecor);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextUnformatted("Scan Radius");
            ImGui::SetNextItemWidth(avail * 0.5f);
            char radiusLabel[16];
            std::snprintf(radiusLabel, sizeof(radiusLabel), "%dm", kRadiusOptionsM[g_radiusIdx]);
            if (ImGui::BeginCombo("##targetlist_radius", radiusLabel))
            {
                for (int i = 0; i < static_cast<int>(std::size(kRadiusOptionsM)); ++i)
                {
                    char optLabel[16];
                    std::snprintf(optLabel, sizeof(optLabel), "%dm", kRadiusOptionsM[i]);
                    const bool isSelected = (g_radiusIdx == i);
                    if (ImGui::Selectable(optLabel, isSelected))
                    {
                        g_radiusIdx = i;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            const bool noCategorySelected = !g_wantPeople && !g_wantMonsterous && !g_wantAnimals && !g_wantDecor;
            ImGui::BeginDisabled(noCategorySelected);
            if (ImGui::Button("Scan"))
            {
                requestScan();
            }
            ImGui::EndDisabled();
            HoverTooltip(noCategorySelected ? "Check at least one category first." : "Scan for tracked actors in range.");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Checkbox("Mark Target", &g_markTarget))
            {
                requestMark(g_markTarget);
            }
            HoverTooltip("Highlights the currently locked target with a marker material while checked. No effect with nothing targeted.");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Large updating distance readout (RedFalcon: "when an item is selected after
            // scanning, it will display in larger text an updating distance number ... to help
            // locate it if its unknown where it is") -- reuses MenuStatus::TargetDistMeters(),
            // which main.lua's own 300ms status loop already recomputes for WHATEVER is currently
            // locked, so this stays live regardless of whether the lock came from this tab's "+"
            // row, the Move tab's own Num+, or anywhere else.
            if (hasTarget)
            {
                ImGui::SetWindowFontScale(1.6f);
                ImGui::Text("%.1f m", MenuStatus::TargetDistMeters());
                ImGui::SetWindowFontScale(1.0f);
            }
            else
            {
                ImGui::TextDisabled("(target something to see live distance)");
            }
        }
    } // namespace

    auto Draw() -> void
    {
        pollScanStatus();

        ImGui::BeginDisabled(MenuStatus::IsRestoring());

        constexpr float kRightPanelWidth = 300.0f;
        const float contentHeight = ImGui::GetContentRegionAvail().y;

        ImGui::BeginChild("##targetlist_left", ImVec2(-kRightPanelWidth, contentHeight), true);
        drawResultsList();
        ImGui::EndChild();

        ImGui::SameLine();

        constexpr ImGuiChildFlags kPaddedChild = ImGuiChildFlags_AlwaysUseWindowPadding;
        ImGui::BeginChild("##targetlist_right", ImVec2(kRightPanelWidth, contentHeight), kPaddedChild);
        drawRightPanel();
        ImGui::EndChild();

        ImGui::EndDisabled(); // MenuStatus::IsRestoring()
    }
} // namespace RC::LivingBaseSpawnMenu::TargetListMenu
