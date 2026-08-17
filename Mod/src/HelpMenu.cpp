#include <HelpMenu.hpp>

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <imgui.h>

namespace RC::LivingBaseSpawnMenu::HelpMenu
{
    namespace
    {
        // Lives alongside the DLL, in this mod's OWN folder -- not LivingBase's -- since this is
        // documentation for the window itself, not for LivingBase's keyboard controls (which
        // already have their own README.md).
        constexpr const char* HELP_TEXT_PATH = "ue4ss/Mods/LivingBaseSpawnMenu/help.txt";
        constexpr const char* HISTORY_PATH = "ue4ss/Mods/LivingBase/spawn_menu_history.txt";
        constexpr auto HISTORY_POLL_INTERVAL = std::chrono::milliseconds(500);

        std::string g_help_text;

        std::vector<std::string> g_history;
        std::chrono::steady_clock::time_point g_last_history_poll{};

        auto LoadHelpText() -> void
        {
            std::ifstream f(HELP_TEXT_PATH);
            if (!f)
            {
                g_help_text = "(help.txt not found -- expected at ue4ss/Mods/LivingBaseSpawnMenu/help.txt)";
                return;
            }
            std::stringstream buffer;
            buffer << f.rdbuf();
            g_help_text = buffer.str();
        }

        auto PollHistory(bool force) -> void
        {
            auto now = std::chrono::steady_clock::now();
            if (!force && now - g_last_history_poll < HISTORY_POLL_INTERVAL)
            {
                return;
            }
            g_last_history_poll = now;
            std::ifstream f(HISTORY_PATH);
            if (!f)
            {
                return;
            }
            g_history.clear();
            std::string line;
            while (std::getline(f, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (!line.empty())
                {
                    g_history.push_back(line);
                }
            }
        }

        // Deliberately minimal -- ImGui has no real markdown renderer, and pulling one in for a
        // handful of headers/bullets isn't worth it. "# " = an emphasized header line (with a
        // trailing separator), "- " = an indented bullet, everything else = plain wrapped
        // paragraph text, blank lines = spacing.
        auto DrawFormattedHelpText(const std::string& text) -> void
        {
            std::stringstream ss(text);
            std::string line;
            while (std::getline(ss, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (line.empty())
                {
                    ImGui::Spacing();
                }
                else if (line.rfind("# ", 0) == 0)
                {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.85f, 0.64f, 0.20f, 1.0f), "%s", line.c_str() + 2);
                    ImGui::Separator();
                }
                else if (line.rfind("- ", 0) == 0)
                {
                    ImGui::Bullet();
                    ImGui::SameLine();
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(line.c_str() + 2);
                    ImGui::PopTextWrapPos();
                }
                else
                {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(line.c_str());
                    ImGui::PopTextWrapPos();
                }
            }
        }
    } // namespace

    auto ReloadNow() -> void
    {
        LoadHelpText();
        PollHistory(true);
    }

    auto DrawInstructionsTab() -> void
    {
        ImGui::BeginChild("##help_text", ImVec2(0.0f, 0.0f), false);
        DrawFormattedHelpText(g_help_text);
        ImGui::EndChild();
    }

    auto DrawHistoryTab() -> void
    {
        PollHistory(false);

        ImGui::TextDisabled("Every message shown as an on-screen toast this session.");
        ImGui::Separator();
        ImGui::BeginChild("##help_history", ImVec2(0.0f, 0.0f), false);
        // Auto-follow the bottom ONLY if the view was already there before this frame's entries
        // were added -- otherwise a user who scrolled up to read older history would get yanked
        // back down the next time something toasts.
        bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
        for (const auto& entry : g_history)
        {
            ImGui::TextWrapped("%s", entry.c_str());
        }
        if (wasAtBottom)
        {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
} // namespace RC::LivingBaseSpawnMenu::HelpMenu
