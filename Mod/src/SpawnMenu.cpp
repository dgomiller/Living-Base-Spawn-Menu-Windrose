#include <SpawnMenu.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <MenuStatus.hpp>
#include <StandaloneWindow.hpp>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <imgui.h>

namespace RC::LivingBaseSpawnMenu::SpawnMenu
{
    namespace
    {
        struct MenuNode
        {
            std::string label;
            std::vector<std::unique_ptr<MenuNode>> children;
            bool is_leaf{};
            std::string roster;
            int index{};
        };

        MenuNode g_root;

        // Currently SELECTED leaf (2026-08-16 rework -- clicking used to spawn immediately; now it
        // only selects, and the "Spawn"/"Replace" buttons below act on the selection). g_selected_path
        // is the full breadcrumb ("Senkamati / Warrior / Crew Reskin / Helmet On") shown in the
        // "Selected:" readout -- roster/index is what actually gets sent on Spawn/Replace.
        std::string g_selected_roster;
        int g_selected_index{};
        std::string g_selected_path;
        bool g_has_selection{};

        // A bare relative path resolves against the GAME's own working directory
        // (R5/Binaries/Win64/), not this mod's folder -- confirmed the hard way on the Lua side
        // (spawnmenu_manifest.lua's own header comment has the story). Same convention on the C++
        // side since we're injected into the same process/CWD.
        constexpr const char* INI_PATH = "ue4ss/Mods/LivingBase/spawn_menu.ini";
        constexpr const char* REQUEST_PATH = "ue4ss/Mods/LivingBase/spawn_request.txt";

        auto split(const std::string& s, char delim) -> std::vector<std::string>
        {
            std::vector<std::string> parts;
            std::stringstream ss(s);
            std::string part;
            while (std::getline(ss, part, delim))
            {
                parts.push_back(part);
            }
            return parts;
        }

        auto trim(std::string s) -> std::string
        {
            size_t start = s.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
            {
                return "";
            }
            size_t end = s.find_last_not_of(" \t\r\n");
            return s.substr(start, end - start + 1);
        }

        auto find_or_create_child(MenuNode& parent, const std::string& label) -> MenuNode&
        {
            for (auto& child : parent.children)
            {
                if (child->label == label)
                {
                    return *child;
                }
            }
            parent.children.push_back(std::make_unique<MenuNode>());
            parent.children.back()->label = label;
            return *parent.children.back();
        }

        auto commit_entry(const std::vector<std::string>& path_parts, const std::string& roster, int index) -> void
        {
            if (path_parts.empty() || roster.empty() || index <= 0)
            {
                return;
            }
            MenuNode* current = &g_root;
            for (const auto& part : path_parts)
            {
                current = &find_or_create_child(*current, part);
            }
            current->is_leaf = true;
            current->roster = roster;
            current->index = index;
        }

        // Minimal INI parser matching exactly what spawnmenu_manifest.lua writes: `[dotted.path]`
        // section headers, `key = value` lines for `roster`/`index` (label is redundant with the
        // path's own last segment by construction, so not separately needed here). Deliberately
        // does not try to be a general-purpose INI parser -- only the subset this manifest uses.
        auto parse_ini(const std::string& content) -> void
        {
            g_root = MenuNode{};
            std::vector<std::string> current_path;
            std::string current_roster;
            int current_index = 0;

            auto flush = [&]() { commit_entry(current_path, current_roster, current_index); };

            std::stringstream ss(content);
            std::string line;
            while (std::getline(ss, line))
            {
                line = trim(line);
                if (line.empty() || line[0] == ';' || line[0] == '#')
                {
                    continue;
                }
                if (line.front() == '[' && line.back() == ']')
                {
                    flush();
                    current_path = split(line.substr(1, line.size() - 2), '.');
                    for (auto& part : current_path)
                    {
                        part = trim(part);
                    }
                    current_roster.clear();
                    current_index = 0;
                    continue;
                }
                auto eq = line.find('=');
                if (eq == std::string::npos)
                {
                    continue;
                }
                std::string key = trim(line.substr(0, eq));
                std::string value = trim(line.substr(eq + 1));
                if (key == "roster")
                {
                    current_roster = value;
                }
                else if (key == "index")
                {
                    current_index = std::atoi(value.c_str());
                }
            }
            flush();
        }

        // verb: "SPAWN" or "REPLACE" -- see main.lua's pollSpawnMenuRequest for the grammar this
        // produces ("VERB:ROSTER:INDEX\n") and what each verb does.
        auto write_request(const char* verb, const std::string& roster, int index) -> void
        {
            std::ofstream f(REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] SpawnMenu: failed to write spawn_request.txt\n"));
                return;
            }
            f << verb << ":" << roster << ":" << index << "\n";
        }

        // draw_node now only SELECTS a leaf (highlights it, records roster/index/full-path) rather
        // than spawning immediately -- the Spawn/Replace buttons in Draw() act on the selection.
        // `path_prefix`: the breadcrumb accumulated so far, purely for the "Selected: ..." readout.
        auto draw_node(MenuNode& node, const std::string& path_prefix) -> void
        {
            std::string full_path = path_prefix.empty() ? node.label : path_prefix + " / " + node.label;

            if (node.is_leaf && node.children.empty())
            {
                bool is_selected = g_has_selection && g_selected_roster == node.roster && g_selected_index == node.index;
                if (ImGui::Selectable(node.label.c_str(), is_selected))
                {
                    g_selected_roster = node.roster;
                    g_selected_index = node.index;
                    g_selected_path = full_path;
                    g_has_selection = true;
                }
                return;
            }

            if (ImGui::TreeNode(node.label.c_str()))
            {
                for (auto& child : node.children)
                {
                    draw_node(*child, full_path);
                }
                ImGui::TreePop();
            }
        }
    } // namespace

    auto Reload() -> void
    {
        std::ifstream f(INI_PATH);
        if (!f)
        {
            Output::send<LogLevel::Warning>(STR("[LivingBaseSpawnMenu] SpawnMenu: spawn_menu.ini not found yet\n"));
            g_root = MenuNode{};
            return;
        }
        std::stringstream buffer;
        buffer << f.rdbuf();
        parse_ini(buffer.str());
    }

    auto Draw() -> void
    {
        if (ImGui::Button("Refresh"))
        {
            Reload();
        }
        ImGui::Separator();

        // Restore-lock gate lives HERE, inside this function, rather than as one blanket
        // BeginDisabled wrapped around the whole panel from StandaloneWindow.cpp -- same
        // per-panel pattern MoveMenu.cpp uses for its own Tools Active toggle.
        ImGui::BeginDisabled(MenuStatus::IsRestoring());
        if (g_root.children.empty())
        {
            ImGui::TextDisabled("(no entries -- check spawn_menu.ini exists and Refresh)");
        }
        else
        {
            // Tree in a scrollable child region so the Spawn/Replace bar below always stays
            // visible regardless of how deep the current category is expanded. No separate
            // "Selected: ..." text row (dropped 2026-08-16, RedFalcon: the tree's own highlighted
            // row already shows the selection -- a second text copy was redundant).
            ImGui::BeginChild("##spawnmenu_tree", ImVec2(0.0f, -44.0f), true);
            for (auto& child : g_root.children)
            {
                draw_node(*child, "");
            }
            ImGui::EndChild();

            ImGui::Separator();

            // Factored out (2026-08-24, numpad-only keybind rebuild) so the F2/F3 shortcuts below
            // and the buttons themselves share one body -- keyboard and mouse can never disagree
            // about what "Spawn"/"Replace" actually does.
            // Selections under the "Custom" top-level branch (Poses/Skin Tones/Hair, and whatever
            // else lands there later) apply to a target that's ALREADY on screen rather than
            // placing something new to go look at -- RedFalcon's request (2026-08-28): stay in
            // this window after one of those so several can be tried in a row without the focus
            // hop each time. Checked against the SAME breadcrumb the "Selected: ..." tooltip
            // already uses (g_selected_path's first segment), not the roster name -- this way any
            // future roster added under Custom picks up the same behavior automatically, with
            // nothing to keep in sync on the Lua side.
            auto isCustomSelection = [&]() -> bool
            {
                return g_selected_path.rfind("Custom / ", 0) == 0 || g_selected_path == "Custom";
            };
            auto doSpawn = [&]()
            {
                write_request("SPAWN", g_selected_roster, g_selected_index);
                // Hand focus back to the game (2026-08-23, RedFalcon's request) -- placing an item
                // shouldn't leave the player stuck alt-tabbed into this window. See
                // StandaloneWindow::ReturnFocusToGame()'s own comment. Skipped for Custom selections,
                // see the comment above.
                if (!isCustomSelection())
                {
                    StandaloneWindow::ReturnFocusToGame();
                }
            };
            auto doReplace = [&]()
            {
                write_request("REPLACE", g_selected_roster, g_selected_index);
                if (!isCustomSelection())
                {
                    StandaloneWindow::ReturnFocusToGame();
                }
            };

            // Replace additionally requires a locked target -- RedFalcon's request (2026-08-16):
            // unlike Spawn (places a brand new object regardless of any target), Replace swaps
            // whatever's currently target-locked, which is meaningless with nothing locked.
            const bool hasTarget = !MenuStatus::TargetLabel().empty();

            // F2/F3 shortcuts (2026-08-24, numpad-only keybind rebuild) -- same guard conditions as
            // the buttons below, so a stray press can't spawn/replace with nothing selected.
            if (g_has_selection && ImGui::IsKeyPressed(ImGuiKey_F2, false))
            {
                doSpawn();
            }
            if (g_has_selection && hasTarget && ImGui::IsKeyPressed(ImGuiKey_F3, false))
            {
                doReplace();
            }

            ImGui::BeginDisabled(!g_has_selection);
            if (ImGui::Button("Spawn", ImVec2(100.0f, 0.0f)))
            {
                doSpawn();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(g_has_selection ? "Place a new copy of: %s (F2)" : "Select an entry in the tree first.", g_selected_path.c_str());
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(!g_has_selection || !hasTarget);
            if (ImGui::Button("Replace", ImVec2(100.0f, 0.0f)))
            {
                doReplace();
            }
            if (ImGui::IsItemHovered())
            {
                const char* msg = !g_has_selection ? "Select an entry in the tree first."
                        : !hasTarget              ? "Target-lock something first (Num +)."
                                                   : "Swap the targeted/locked object for: %s (F3)";
                ImGui::SetTooltip(msg, g_selected_path.c_str());
            }
            ImGui::EndDisabled();
        }
        ImGui::EndDisabled(); // MenuStatus::IsRestoring()
    }
} // namespace RC::LivingBaseSpawnMenu::SpawnMenu
