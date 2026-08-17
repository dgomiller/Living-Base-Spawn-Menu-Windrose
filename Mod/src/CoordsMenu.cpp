#include <CoordsMenu.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <MenuStatus.hpp>

#include <chrono>
#include <cmath>
#include <fstream>
#include <string>

#include <imgui.h>

namespace RC::LivingBaseSpawnMenu::CoordsMenu
{
    namespace
    {
        // Same file/append convention as MoveMenu.cpp's own queueLine -- duplicated here rather
        // than shared across translation units for a handful of lines, matching this project's
        // existing tolerance for small duplicated helpers (see main.lua's own
        // spawnMenuStatueShortName for precedent).
        constexpr const char* MOVE_REQUEST_PATH = "ue4ss/Mods/LivingBase/move_request.txt";
        auto queueLine(const std::string& line) -> void
        {
            std::ofstream f(MOVE_REQUEST_PATH, std::ios::app);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CoordsMenu: failed to write move_request.txt\n"));
                return;
            }
            f << line << "\n";
        }
        auto queueAction(const char* name) -> void { queueLine(std::string("ACTION:") + name); }
        auto queueCoordsMove(float x, float y, float z, float yaw) -> void
        {
            queueLine("COORDS_MOVE:" + std::to_string(x) + ":" + std::to_string(y) + ":" + std::to_string(z) + ":" + std::to_string(yaw));
        }

        bool g_open = false;
        std::string g_opened_id;    // identity check (retarget detection) -- see MenuStatus::TargetId()'s own comment for why NOT the label
        std::string g_opened_label; // display only
        float g_open_x{}, g_open_y{}, g_open_z{}, g_open_yaw{};
        float g_field_x{}, g_field_y{}, g_field_z{}, g_field_yaw{};

        // What we last told Lua to move the SAME object to (via Preview/Reset), and until when to
        // ignore MenuStatus's own position for "did something ELSE move this" purposes -- covers
        // the round-trip lag between writing a request and the next status poll reflecting it, so
        // that lag doesn't get misread as an external nudge and stomp the fields the user is mid-
        // typing. See the "external move" block below for the actual comparison.
        float g_expected_x{}, g_expected_y{}, g_expected_z{}, g_expected_yaw{};
        std::chrono::steady_clock::time_point g_suppress_external_until{};

        constexpr float kPositionEpsilon = 1.0f; // uu -- generous over float/string round-trip noise
        constexpr float kYawEpsilon = 0.5f;      // degrees

        auto NearlyEqual(float a, float b, float epsilon) -> bool { return std::fabs(a - b) <= epsilon; }

        // Unreal's own FRotator normalizes Yaw into (-180, 180] -- confirmed live this reads back
        // as e.g. -90 for what most players think of as "270 degrees," which RedFalcon found
        // confusing ("0 to 180 to -0") in this window specifically. Normalize to [0, 360) for
        // display/editing instead -- Unreal's SetActorRotation happily accepts a raw value like 270
        // or even something outside a single turn (it's just Yaw going into a quaternion), so
        // there's no need to convert BACK before sending, only when READING a value out of the
        // engine.
        auto NormalizeYaw360(float yaw) -> float
        {
            yaw = std::fmod(yaw, 360.0f);
            if (yaw < 0.0f)
            {
                yaw += 360.0f;
            }
            return yaw;
        }

        // Shortest angular distance between two yaw values, wraparound-safe regardless of which
        // side of 0/360 each one happens to land on -- a plain fabs(a - b) would see e.g. 359 and 1
        // as 358 degrees apart instead of the real 2, which would make the external-move check below
        // misfire constantly right around the new 0/360 seam (a far more common resting rotation
        // than the old -180/180 seam this bug would have hidden behind before).
        auto YawDelta(float a, float b) -> float
        {
            float d = std::fmod(std::fabs(a - b), 360.0f);
            return d > 180.0f ? 360.0f - d : d;
        }

        // Sends the move and remembers it as "expected" so the external-move check below doesn't
        // mistake our own request's round-trip lag for something else having moved the object.
        auto SendMove(float x, float y, float z, float yaw) -> void
        {
            queueCoordsMove(x, y, z, yaw);
            g_expected_x = x;
            g_expected_y = y;
            g_expected_z = z;
            g_expected_yaw = yaw;
            g_suppress_external_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
        }
    } // namespace

    auto Open() -> void
    {
        g_opened_id = MenuStatus::TargetId();
        g_opened_label = MenuStatus::TargetLabel();
        g_open_x = MenuStatus::TargetX();
        g_open_y = MenuStatus::TargetY();
        g_open_z = MenuStatus::TargetZ();
        g_open_yaw = NormalizeYaw360(MenuStatus::TargetYaw());
        g_field_x = g_open_x;
        g_field_y = g_open_y;
        g_field_z = g_open_z;
        g_field_yaw = g_open_yaw;
        g_expected_x = g_open_x;
        g_expected_y = g_open_y;
        g_expected_z = g_open_z;
        g_expected_yaw = g_open_yaw;
        g_suppress_external_until = std::chrono::steady_clock::now();
        g_open = true;
        queueAction("COORDS_OPEN");
    }

    auto Draw() -> void
    {
        if (!g_open)
        {
            return;
        }

        // Lost target (re-targeted to something else, or unlocked) while this window was open --
        // treat as Cancel, but WITHOUT writing the opening coordinates back: Spawner.lockedTarget
        // may now point at a completely different actor, and blindly moving "whatever's locked
        // right now" to this window's remembered position would move the WRONG object. Compared
        // by TargetId() (a real per-instance identity), NOT TargetLabel() -- CONFIRMED live
        // (2026-08-16) that comparing by label let a retarget onto a same-labeled object (two
        // identically-dressed Senkamati, two identical decor props, etc.) go undetected, since the
        // label text never changed even though the underlying actor did.
        if (MenuStatus::TargetId() != g_opened_id)
        {
            queueAction("COORDS_CLOSE");
            g_open = false;
            return;
        }

        // Same object, but did something ELSE move it -- a D-pad nudge, a Replace, anything other
        // than this window's own Preview/Reset -- while this window stayed open? RedFalcon's call
        // (2026-08-16): refresh the fields AND the Reset/Cancel anchor to the new position, so an
        // external nudge never gets silently undone by a later Reset/Cancel. Suppressed for a short
        // window after our OWN Preview/Reset send (g_suppress_external_until) so that request's own
        // round-trip lag isn't misread as an external change and doesn't stomp mid-typing fields.
        if (std::chrono::steady_clock::now() >= g_suppress_external_until)
        {
            float mx = MenuStatus::TargetX(), my = MenuStatus::TargetY(), mz = MenuStatus::TargetZ();
            float myaw = NormalizeYaw360(MenuStatus::TargetYaw());
            if (!NearlyEqual(mx, g_expected_x, kPositionEpsilon) || !NearlyEqual(my, g_expected_y, kPositionEpsilon)
                || !NearlyEqual(mz, g_expected_z, kPositionEpsilon) || YawDelta(myaw, g_expected_yaw) > kYawEpsilon)
            {
                g_open_x = g_field_x = g_expected_x = mx;
                g_open_y = g_field_y = g_expected_y = my;
                g_open_z = g_field_z = g_expected_z = mz;
                g_open_yaw = g_field_yaw = g_expected_yaw = myaw;
            }
        }

        bool stayOpen = true;
        ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Edit Coordinates", &stayOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextDisabled("%s", g_opened_label.c_str());
            ImGui::Separator();
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputFloat("X", &g_field_x);
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputFloat("Y", &g_field_y);
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputFloat("Z", &g_field_z);
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputFloat("Rotation", &g_field_yaw);
            ImGui::Separator();

            if (ImGui::Button("Reset", ImVec2(60.0f, 0.0f)))
            {
                g_field_x = g_open_x;
                g_field_y = g_open_y;
                g_field_z = g_open_z;
                g_field_yaw = g_open_yaw;
                SendMove(g_open_x, g_open_y, g_open_z, g_open_yaw);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Move the object back to where it was when this window opened (or last externally moved), and reset these fields to match. Stays open.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(60.0f, 0.0f)))
            {
                SendMove(g_open_x, g_open_y, g_open_z, g_open_yaw);
                queueAction("COORDS_CLOSE");
                g_open = false;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Move the object back to where it was when this window opened (or last externally moved), then close.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Preview", ImVec2(60.0f, 0.0f)))
            {
                SendMove(g_field_x, g_field_y, g_field_z, g_field_yaw);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Move the object to the typed values now, without closing.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply", ImVec2(60.0f, 0.0f)))
            {
                SendMove(g_field_x, g_field_y, g_field_z, g_field_yaw);
                queueAction("COORDS_CLOSE");
                g_open = false;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Move the object to the typed values and close -- final.");
            }
        }
        ImGui::End();

        // The window's own [X] button flips stayOpen to false directly via the p_open pointer --
        // treat exactly like Cancel (revert-write + close), never like Apply. Guarded by g_open so
        // a Cancel/Apply/Reset click THIS SAME frame (which already set g_open=false above) isn't
        // double-processed here.
        if (!stayOpen && g_open)
        {
            SendMove(g_open_x, g_open_y, g_open_z, g_open_yaw);
            queueAction("COORDS_CLOSE");
            g_open = false;
        }
    }
} // namespace RC::LivingBaseSpawnMenu::CoordsMenu
