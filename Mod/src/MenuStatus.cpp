#include <MenuStatus.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>

namespace RC::LivingBaseSpawnMenu::MenuStatus
{
    namespace
    {
        // Same "ue4ss/Mods/LivingBase/..." CWD-relative convention as SpawnMenu.cpp's REQUEST_PATH
        // -- see that file's own comment for why a bare relative path is wrong here.
        constexpr const char* STATUS_PATH = "ue4ss/Mods/LivingBase/spawn_menu_status.txt";
        constexpr auto POLL_INTERVAL = std::chrono::milliseconds(300);

        // Defaults assume "everything's fine" (enabled, not restoring) rather than "everything's
        // locked" -- until the first successful read, there's no reason to grey out the whole
        // window just because main.lua hasn't written its first status line yet.
        bool g_enabled = true;
        bool g_restoring = false;
        std::string g_target;
        std::string g_target_id;
        float g_target_x = 0.0f;
        float g_target_y = 0.0f;
        float g_target_z = 0.0f;
        float g_target_yaw = 0.0f;
        int g_window_toggle_seq = 0;
        int g_focus_steal_seq = 0;

        std::chrono::steady_clock::time_point g_last_poll{};
    } // namespace

    auto Poll() -> void
    {
        auto now = std::chrono::steady_clock::now();
        if (now - g_last_poll < POLL_INTERVAL)
        {
            return;
        }
        g_last_poll = now;

        std::ifstream f(STATUS_PATH);
        if (!f)
        {
            // Not written yet (mod just started, or main.lua hasn't loaded) -- keep the last-known
            // values rather than snapping to some default the instant this fails once.
            return;
        }
        std::string line;
        while (std::getline(f, line))
        {
            auto eq = line.find('=');
            if (eq == std::string::npos)
            {
                continue;
            }
            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            if (!value.empty() && value.back() == '\r')
            {
                value.pop_back();
            }
            if (key == "ENABLED")
            {
                g_enabled = (value == "1");
            }
            else if (key == "RESTORING")
            {
                g_restoring = (value == "1");
            }
            else if (key == "TARGET")
            {
                g_target = value;
            }
            else if (key == "TARGET_ID")
            {
                g_target_id = value;
            }
            else if (key == "TARGET_X")
            {
                g_target_x = std::strtof(value.c_str(), nullptr);
            }
            else if (key == "TARGET_Y")
            {
                g_target_y = std::strtof(value.c_str(), nullptr);
            }
            else if (key == "TARGET_Z")
            {
                g_target_z = std::strtof(value.c_str(), nullptr);
            }
            else if (key == "TARGET_YAW")
            {
                g_target_yaw = std::strtof(value.c_str(), nullptr);
            }
            else if (key == "WINDOW_TOGGLE")
            {
                g_window_toggle_seq = std::atoi(value.c_str());
            }
            else if (key == "FOCUS_STEAL")
            {
                g_focus_steal_seq = std::atoi(value.c_str());
            }
        }
    }

    auto IsEnabled() -> bool { return g_enabled; }
    auto IsRestoring() -> bool { return g_restoring; }
    auto TargetLabel() -> const std::string& { return g_target; }
    auto TargetId() -> const std::string& { return g_target_id; }
    auto TargetX() -> float { return g_target_x; }
    auto TargetY() -> float { return g_target_y; }
    auto TargetZ() -> float { return g_target_z; }
    auto TargetYaw() -> float { return g_target_yaw; }
    auto WindowToggleSeq() -> int { return g_window_toggle_seq; }
    auto FocusStealSeq() -> int { return g_focus_steal_seq; }
} // namespace RC::LivingBaseSpawnMenu::MenuStatus
