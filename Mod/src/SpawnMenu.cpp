#include <SpawnMenu.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <MenuStatus.hpp>
#include <StandaloneWindow.hpp>

#include <algorithm>
#include <cctype>
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

        // Public snapshot of "Custom > Poses", rebuilt in Reload() alongside g_root -- see
        // SpawnMenu.hpp's own PoseNode comment for why this is a separate, ImGui-agnostic copy
        // rather than exposing MenuNode/g_root directly.
        PoseNode g_posesTree;

        auto build_pose_node(const MenuNode& src) -> PoseNode
        {
            PoseNode out;
            out.label = src.label;
            out.is_leaf = src.is_leaf;
            out.index = src.index;
            out.children.reserve(src.children.size());
            for (auto& c : src.children)
            {
                out.children.push_back(build_pose_node(*c));
            }
            return out;
        }

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

        auto to_lower(std::string s) -> std::string
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        // Plain case-insensitive substring match -- "contains anywhere" already covers matching
        // both a prefix ("Antagonist") and a mid-word hit ("Plant") for a query like "ant", so no
        // separate reversed/anchored pass is needed.
        auto contains_ci(const std::string& haystack, const std::string& needle) -> bool
        {
            if (needle.empty())
            {
                return true;
            }
            return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
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

        // Confirm/Move buttons (2026-09-23, RedFalcon, final revision: "Under the Spawn Tree, I want
        // the buttons 'Confirm' (0), Spawn, Move and Replace") -- these two are one-shot ACTION lines
        // on the SAME move_request.txt queue MoveMenu.cpp's own numpad mirror already writes to
        // (CONFIRM_PLACEMENT/GRAB_TARGET), so a click here is indistinguishable from pressing
        // Numpad 0/Numpad * to main.lua's own drainMoveMenuQueue. Cancel/Despawn were tried here
        // too in an earlier revision but ended up living in MoveMenu.cpp's own pane instead -- see
        // that file's own comment for the current split. Duplicated helper rather than shared across
        // translation units -- same "small helper, not worth a shared header" tolerance
        // CoordsMenu.cpp's own copy of this exact pattern already established.
        constexpr const char* MOVE_REQUEST_PATH = "ue4ss/Mods/LivingBase/move_request.txt";
        auto queue_move_action(const char* name) -> void
        {
            std::ofstream f(MOVE_REQUEST_PATH, std::ios::app);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] SpawnMenu: failed to write move_request.txt\n"));
                return;
            }
            f << "ACTION:" << name << "\n";
        }

        // Recursive "does this leaf, or any leaf under this branch, match the filter" check --
        // used to decide whether a branch is worth drawing at all when a filter is active, so a
        // branch with zero matching leaves anywhere under it is hidden rather than shown empty.
        auto node_matches_filter(const MenuNode& node, const std::string& filter) -> bool
        {
            if (node.is_leaf && node.children.empty())
            {
                return contains_ci(node.label, filter);
            }
            for (auto& child : node.children)
            {
                if (node_matches_filter(*child, filter))
                {
                    return true;
                }
            }
            return false;
        }

        // draw_node now only SELECTS a leaf (highlights it, records roster/index/full-path) rather
        // than spawning immediately -- the Spawn/Replace buttons in Draw() act on the selection.
        // `path_prefix`: the breadcrumb accumulated so far, purely for the "Selected: ..." readout.
        // `filter`: empty means "show everything" (original behavior); non-empty hides non-matching
        // leaves and, recursively, any branch with no matching leaf anywhere under it.
        auto draw_node(MenuNode& node, const std::string& path_prefix, const std::string& filter) -> void
        {
            std::string full_path = path_prefix.empty() ? node.label : path_prefix + " / " + node.label;

            if (node.is_leaf && node.children.empty())
            {
                if (!filter.empty() && !contains_ci(node.label, filter))
                {
                    return;
                }
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

            if (!filter.empty())
            {
                if (!node_matches_filter(node, filter))
                {
                    return;
                }
                // Auto-expand so a match isn't hidden behind a collapsed branch the user never
                // opened -- filtering already narrowed the tree down to just what's relevant.
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }

            if (ImGui::TreeNode(node.label.c_str()))
            {
                for (auto& child : node.children)
                {
                    draw_node(*child, full_path, filter);
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
            g_posesTree = PoseNode{};
            return;
        }
        std::stringstream buffer;
        buffer << f.rdbuf();
        parse_ini(buffer.str());

        g_posesTree = PoseNode{};
        for (auto& top : g_root.children)
        {
            if (top->label != "Custom")
            {
                continue;
            }
            for (auto& c : top->children)
            {
                if (c->label == "Poses")
                {
                    g_posesTree = build_pose_node(*c);
                }
            }
        }
    }

    auto GetPosesTree() -> const PoseNode&
    {
        return g_posesTree;
    }

    auto ApplyPoseByIndex(int index) -> void
    {
        write_request("REPLACE", "CUSTOM_POSES", index);
    }

    auto Draw() -> void
    {
        // Replaces the old "Refresh" button (2026-09-29, RedFalcon: redundant since Reload()
        // already runs once at window startup and a stale ini mid-session was the only case it
        // ever covered -- see [[project_spawn_menu_ini_stale_indices]] in memory). Case-insensitive
        // substring filter over leaf labels only; matching branches stay visible, non-matching ones
        // collapse away entirely -- see node_matches_filter()/draw_node()'s own comments.
        static char s_filterBuf[128] = "";
        static std::string s_activeFilter;

        // Row sized to exactly match the tree BeginChild's own width just below (2026-09-29,
        // RedFalcon: "I'd like that whole row to be the width of the tree box") -- both sit in the
        // same content region, so computing off GetContentRegionAvail() here naturally lines up with
        // the child's own width-0 ("fill available") sizing without any extra coordination.
        constexpr float kFilterBtnW = 60.0f;
        constexpr float kClearBtnW = 28.0f;
        const float filterAvail = ImGui::GetContentRegionAvail().x;
        const float filterInputW = filterAvail - kFilterBtnW - kClearBtnW - ImGui::GetStyle().ItemSpacing.x * 2.0f;

        ImGui::PushItemWidth(filterInputW);
        bool enterPressed = ImGui::InputText("##spawnmenu_filter", s_filterBuf, sizeof(s_filterBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Filter", ImVec2(kFilterBtnW, 0.0f)) || enterPressed)
        {
            s_activeFilter = s_filterBuf;
        }
        ImGui::SameLine();
        // Red "X" clear button (2026-09-29, RedFalcon's request) -- empties both the textbox and the
        // active filter in one click, same effect as clearing the text and pressing Filter.
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.12f, 0.12f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.16f, 0.16f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.08f, 0.08f, 1.0f));
        if (ImGui::Button("X", ImVec2(kClearBtnW, 0.0f)))
        {
            s_filterBuf[0] = '\0';
            s_activeFilter.clear();
        }
        ImGui::PopStyleColor(3);
        ImGui::Separator();

        // Restore-lock gate lives HERE, inside this function, rather than as one blanket
        // BeginDisabled wrapped around the whole panel from StandaloneWindow.cpp -- same
        // per-panel pattern MoveMenu.cpp uses for its own Tools Active toggle.
        ImGui::BeginDisabled(MenuStatus::IsRestoring());
        if (g_root.children.empty())
        {
            ImGui::TextDisabled("(no entries -- check spawn_menu.ini exists)");
        }
        else
        {
            // Tree in a scrollable child region so the button bar below always stays visible
            // regardless of how deep the current category is expanded. No separate "Selected: ..."
            // text row (dropped 2026-08-16, RedFalcon: the tree's own highlighted row already shows
            // the selection -- a second text copy was redundant). Bottom margin widened from -44 to
            // -52 (2026-09-23) to fit the button row's own new height (kActionBtnH=28, up from the
            // buttons' old ~20px default) without cramping, then to -88 (2026-09-29) when the button
            // area grew from one row to two (see the button block's own comment below) -- adds
            // roughly one more kActionBtnH row plus its own item spacing on top of the old margin.
            ImGui::BeginChild("##spawnmenu_tree", ImVec2(0.0f, -88.0f), true);
            for (auto& child : g_root.children)
            {
                // "Custom" (Poses/Skin Tones/Hair/Clothes) hidden here (2026-09-16, RedFalcon:
                // "now that this step is done, the entire Custom branch of the tools tree is no
                // longer needed") -- the Custom tab's own dedicated widgets (Body/Hair/Clothes/
                // Belts and Straps/Poses and Actions) now cover everything this branch used to
                // exist for. The underlying spawn_menu.ini generation is deliberately UNTOUCHED --
                // GetPosesTree() (used by CustomMenu.cpp's own Poses and Actions windowshade) still
                // reads "Custom > Poses" straight out of g_root regardless of whether it's drawn
                // here, so only the VISUAL tree entry is removed, not the data source.
                if (child->label == "Custom")
                {
                    continue;
                }
                draw_node(*child, "", s_activeFilter);
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

            // 2-row button block (2026-09-29, RedFalcon: "move the bottom buttons for the spawn
            // tab... make two rows. First button is 'Confirm' have it take up both rows and make it
            // green... top row will be Spawn, Move, Replace, and below those are Cancel, Despawn and
            // Undo. Make them as wide as the spawn box"). SUPERSEDES the previous 4-button single-row
            // layout: Cancel/Despawn/Undo move IN here from MoveMenu.cpp's own pane (see that file's
            // own comment marking their removal) rather than staying duplicated in both places.
            // kActionBtnH matches MoveMenu.cpp's own cellH (28.0f) exactly -- duplicated rather than
            // shared across translation units, same tolerance as MOVE_REQUEST_PATH just above. avail
            // is the tree child's own just-ended width (that BeginChild used width 0 = "fill the
            // pane"), so the 4-column split here (Confirm + 3 action columns) naturally spans exactly
            // the tree's own width with no extra math needed -- same formula the old 4-equal-button
            // row already used, just repurposed as Confirm's column plus the 3 columns the two action
            // rows below share.
            constexpr float kActionBtnH = 28.0f;
            const float avail = ImGui::GetContentRegionAvail().x;
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float btnW = (avail - gap * 3.0f) / 4.0f;
            const float rowGap = ImGui::GetStyle().ItemSpacing.y;
            const float confirmH = kActionBtnH * 2.0f + rowGap;

            // Explicit cursor placement (2026-09-29, RedFalcon: "buttons under the spawn box are
            // still wrong" -- screenshot showed a gap between the two action rows). ImGui's line
            // height is the TALLEST item on a row, so with the tall Confirm on row 1, the next
            // auto-placed line always started below Confirm's bottom edge -- leaving a full-row gap
            // under Spawn/Move/Replace no matter how spacing was tuned. Every button here is instead
            // positioned directly via SetCursorPos from one shared origin, so Cancel/Despawn/Undo sit
            // exactly one rowGap under Spawn/Move/Replace, beside Confirm's lower half.
            const ImVec2 blockOrigin = ImGui::GetCursorPos();
            auto colX = [&](int col) { return blockOrigin.x + static_cast<float>(col) * (btnW + gap); };

            // "Confirm" -- same action as Numpad 0 (CONFIRM_PLACEMENT): only meaningful while a
            // placement/relocate preview is actively following the camera. Green (RedFalcon's
            // request, "make it green to stand out") regardless of enabled state -- BeginDisabled
            // already dims it enough on its own when nothing's being placed, same as every other
            // conditionally-enabled button on this row.
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.55f, 0.13f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.17f, 0.68f, 0.17f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.42f, 0.10f, 1.0f));
            ImGui::BeginDisabled(!MenuStatus::IsPlacementActive());
            if (ImGui::Button("Confirm", ImVec2(btnW, confirmH)))
            {
                queue_move_action("CONFIRM_PLACEMENT");
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(MenuStatus::IsPlacementActive() ? "Lock the currently-previewed object in place (Numpad 0)" : "Nothing is currently being placed.");
            }
            ImGui::EndDisabled();
            ImGui::PopStyleColor(3);

            ImGui::SetCursorPos(ImVec2(colX(1), blockOrigin.y));
            ImGui::BeginDisabled(!g_has_selection);
            if (ImGui::Button("Spawn", ImVec2(btnW, kActionBtnH)))
            {
                doSpawn();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(g_has_selection ? "Place a new copy of: %s (F2)" : "Select an entry in the tree first.", g_selected_path.c_str());
            }
            ImGui::EndDisabled();

            // "Move" -- same action as Numpad * (GRAB_TARGET): start relocating whatever's
            // currently target-locked. Needs a locked target, same reasoning as Replace below.
            ImGui::SetCursorPos(ImVec2(colX(2), blockOrigin.y));
            ImGui::BeginDisabled(!hasTarget);
            if (ImGui::Button("Move", ImVec2(btnW, kActionBtnH)))
            {
                queue_move_action("GRAB_TARGET");
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(hasTarget ? "Start relocating the target-locked object (Numpad *)" : "Target-lock something first (Num +).");
            }
            ImGui::EndDisabled();

            ImGui::SetCursorPos(ImVec2(colX(3), blockOrigin.y));
            ImGui::BeginDisabled(!g_has_selection || !hasTarget);
            if (ImGui::Button("Replace", ImVec2(btnW, kActionBtnH)))
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

            // Second row -- Cancel/Despawn/Undo, moved in from MoveMenu.cpp (2026-09-29, see this
            // block's own header comment). Placed one rowGap below row 1, in columns 1-3 beside
            // Confirm's lower half.
            const float row2Y = blockOrigin.y + kActionBtnH + rowGap;
            ImGui::SetCursorPos(ImVec2(colX(1), row2Y));
            ImGui::BeginDisabled(!MenuStatus::IsPlacementActive());
            if (ImGui::Button("Cancel", ImVec2(btnW, kActionBtnH)))
            {
                queue_move_action("CANCEL_PLACEMENT");
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(MenuStatus::IsPlacementActive() ? "Cancel the active placement/relocation (Numpad /)" : "Nothing is currently being placed.");
            }

            ImGui::SetCursorPos(ImVec2(colX(2), row2Y));
            ImGui::BeginDisabled(!hasTarget);
            if (ImGui::Button("Despawn", ImVec2(btnW, kActionBtnH)))
            {
                queue_move_action("DESPAWN");
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(hasTarget ? "Despawn the targeted object (Numpad 3 / F4)" : "Target-lock something first (Num +)");
            }

            ImGui::SetCursorPos(ImVec2(colX(3), row2Y));
            if (ImGui::Button("Undo", ImVec2(btnW, kActionBtnH)))
            {
                queue_move_action("UNDO");
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Restore the last despawn (Ctrl+Z)");
            }
            // Move the cursor below the whole block and register it with a zero-size Dummy so the
            // window's content bounds account for it (SetCursorPos alone doesn't extend them).
            ImGui::SetCursorPos(ImVec2(blockOrigin.x, blockOrigin.y + confirmH));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }
        ImGui::EndDisabled(); // MenuStatus::IsRestoring()
    }
} // namespace RC::LivingBaseSpawnMenu::SpawnMenu
