#include <BarbieMenu.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <ImageLoader.hpp>
#include <MenuStatus.hpp>
#include <StandaloneWindow.hpp>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include <imgui.h>

namespace RC::LivingBaseSpawnMenu::BarbieMenu
{
    namespace
    {
        // Same "ue4ss/Mods/..." CWD-relative convention as every other request file in this bridge
        // (see SpawnMenu.cpp's own REQUEST_PATH comment) -- own file, own mod folder, so a
        // concurrent Barbie spawn can never race the roster-tree Spawn/Replace request.
        constexpr const char* REQUEST_PATH = "ue4ss/Mods/LivingBase/barbie_spawn_request.txt";
        constexpr const char* SWATCH_DIR = "ue4ss/Mods/LivingBaseSpawnMenu/swatches/";

        // One cell of the Body Type grid: which donor class to spawn, its NATIVE family (used both
        // as the spawn's own `family` arg and, on the Lua side, to look up which
        // DA_Custom_BodyTypeList_<Label>As<Origin> family-label applies when an Origin other than
        // this one is picked), and the sex to request (== native sex for one column, a real
        // SwapBodySex() for the other -- Lua's Spawner.SwapBodyType already handles both cases
        // identically, see WINDROSE_MODDING_NOTES.md's 112/112-matrix + nude-mod-crash-fix writeup).
        struct BodyTypeSex
        {
            const char* thumb;      // filename under swatches/BodyTypes/
            const char* class_path; // full /Game/... class path
            const char* family;     // native BodyType family tag
            const char* name;       // this CELL's own donor name (2026-09-11) -- distinct from the
                                     // row label for Gatherer/Hunter, since "Gatherer / Hunter" isn't
                                     // a usable spawn name for either individual cell; every other
                                     // row's name matches its own label. Used to build the actual
                                     // per-spawn name sent to Lua (RedFalcon: "use the origin and
                                     // bodytype in the name, not literally the words").
        };
        struct BodyTypeRow
        {
            const char* label;
            BodyTypeSex male;
            BodyTypeSex female;
        };

        // 7 rows, alphabetical by label (RedFalcon, 2026-09-11) -- BlackAxel/Farmer/Gatherer-Hunter/
        // Herbalist/JasperCrowe/MortarMan/Woodman is already alphabetical order, no resort needed.
        // Gatherer/Hunter share one row (the finalized (0,0,1)-shape pair from WINDROSE_MODDING_
        // NOTES.md 19m -- both sexes already covered natively, no sex-swap needed for either column)
        // -- the only row where Male and Female use two DIFFERENT donor classes rather than one
        // class sex-swapped.
        constexpr BodyTypeRow kBodyTypeRows[] = {
                {"BlackAxel",
                 {"bodytype_BlackAxelMale.png", "/Game/Gameplay/Character/AI/NPC/Employee/CookingStation/BP_NPC_Employee_CookingStation_BlackAxel.BP_NPC_Employee_CookingStation_BlackAxel_C", "Albion", "BlackAxel"},
                 {"bodytype_BlackAxelFemale.png", "/Game/Gameplay/Character/AI/NPC/Employee/CookingStation/BP_NPC_Employee_CookingStation_BlackAxel.BP_NPC_Employee_CookingStation_BlackAxel_C", "Albion", "BlackAxel"}},
                {"Farmer",
                 {"bodytype_FarmerMale.png", "/Game/Gameplay/Character/AI/NPC/Handyman/Handyman_Farmer/BP_NPC_Handyman_Farmer.BP_NPC_Handyman_Farmer_C", "Scum", "Farmer"},
                 {"bodytype_FarmerFemale.png", "/Game/Gameplay/Character/AI/NPC/Handyman/Handyman_Farmer/BP_NPC_Handyman_Farmer.BP_NPC_Handyman_Farmer_C", "Scum", "Farmer"}},
                {"Gatherer / Hunter",
                 {"bodytype_HunterMale.png", "/Game/Gameplay/Character/AI/NPC/Handyman/Handyman_Hunter/BP_NPC_Handyman_Hunter.BP_NPC_Handyman_Hunter_C", "African", "Hunter"},
                 {"bodytype_GathererFemale.png", "/Game/Gameplay/Character/AI/NPC/Handyman/Handyman_Gatherer/BP_NPC_Handyman_Gatherer.BP_NPC_Handyman_Gatherer_C", "Adventurer", "Gatherer"}},
                {"Herbalist",
                 {"bodytype_HerbalistMale.png", "/Game/Gameplay/Character/AI/NPC/Handyman/Handyman_Herbalist/BP_NPC_Handyman_Herbalist.BP_NPC_Handyman_Herbalist_C", "Adventurer", "Herbalist"},
                 {"bodytype_HerbalistFemale.png", "/Game/Gameplay/Character/AI/NPC/Handyman/Handyman_Herbalist/BP_NPC_Handyman_Herbalist.BP_NPC_Handyman_Herbalist_C", "Adventurer", "Herbalist"}},
                {"JasperCrowe",
                 {"bodytype_JasperCroweMale.png", "/Game/Gameplay/Character/AI/NPC/Employee/WeaponStation/BP_NPC_Employee_WeaponStation_JasperCrowe.BP_NPC_Employee_WeaponStation_JasperCrowe_C", "Adventurer", "JasperCrowe"},
                 {"bodytype_JasperCroweFemale.png", "/Game/Gameplay/Character/AI/NPC/Employee/WeaponStation/BP_NPC_Employee_WeaponStation_JasperCrowe.BP_NPC_Employee_WeaponStation_JasperCrowe_C", "Adventurer", "JasperCrowe"}},
                {"MortarMan",
                 {"bodytype_MortarManMale.png", "/Game/Gameplay/Character/AI/NPC/MortarMan/BP_NPC_MortarMan.BP_NPC_MortarMan_C", "Native", "MortarMan"},
                 {"bodytype_MortarManFemale.png", "/Game/Gameplay/Character/AI/NPC/MortarMan/BP_NPC_MortarMan.BP_NPC_MortarMan_C", "Native", "MortarMan"}},
                {"Woodman",
                 {"bodytype_WoodmanMale.png", "/Game/Gameplay/Character/AI/NPC/Handyman/Handyman_Woodman/BP_NPC_Handyman_Woodman.BP_NPC_Handyman_Woodman_C", "Scum", "Woodman"},
                 {"bodytype_WoodmanFemale.png", "/Game/Gameplay/Character/AI/NPC/Handyman/Handyman_Woodman/BP_NPC_Handyman_Woodman.BP_NPC_Handyman_Woodman_C", "Scum", "Woodman"}},
        };
        constexpr int kBodyTypeRowCount = sizeof(kBodyTypeRows) / sizeof(kBodyTypeRows[0]);

        // 8 Origin rows, alphabetical family name. Both thumbnails in a row select the SAME origin
        // value -- sex comes entirely from the Body Type grid (RedFalcon's own design, 2026-09-08:
        // "Origin also got Male+Female columns... purely so the user can preview how a given origin
        // renders on both sexes before picking"). Filenames match the ACTUAL captured swatches
        // (Origin_<Family>_<M|F>.png), under swatches/Origins/.
        struct OriginRow
        {
            const char* family;
            const char* thumb_m;
            const char* thumb_f;
        };
        constexpr OriginRow kOriginRows[] = {
                {"Adventurer", "Origin_Adventurer_M.png", "Origin_Adventurer_F.png"},
                {"African", "Origin_African_M.png", "Origin_African_F.png"},
                {"Albion", "Origin_Albion_M.png", "Origin_Albion_F.png"},
                {"Fable", "Origin_Fable_M.png", "Origin_Fable_F.png"},
                {"Native", "Origin_Native_M.png", "Origin_Native_F.png"},
                {"Orient", "Origin_Orient_M.png", "Origin_Orient_F.png"},
                {"Scum", "Origin_Scum_M.png", "Origin_Scum_F.png"},
                {"Senkamati", "Origin_Senkamati_M.png", "Origin_Senkamati_F.png"},
        };
        constexpr int kOriginRowCount = sizeof(kOriginRows) / sizeof(kOriginRows[0]);

        constexpr float kThumbSize = 96.0f;   // grid cells, inside the popups
        constexpr float kPreviewSize = 110.0f; // the always-visible "current pick" swatch

        // -1 = nothing picked yet, matches every other "no selection" sentinel in this codebase
        // (SpawnMenu.cpp's own g_selected_index/g_has_selection pattern, CustomMenu.cpp's g_selected
        // rows).
        int g_selected_bodytype_row = -1; // index into kBodyTypeRows
        bool g_selected_bodytype_is_male = false;
        int g_selected_origin_row = -1; // index into kOriginRows

        auto ThumbPath(const char* subdir, const char* file) -> std::string
        {
            return std::string(SWATCH_DIR) + subdir + "/" + file;
        }

        // A single grid cell (either grid) -- an ImageButton showing the real thumbnail, a colored
        // border when selected, or a plain "(missing)" Selectable if the PNG failed to load (a bad
        // file shouldn't make its whole cell unreachable). Shared by both popups below.
        auto DrawGridCell(const char* id, const std::string& path, bool selected) -> bool
        {
            int w = 0, h = 0;
            ImTextureID tex = ImageLoader::GetOrLoad(path, w, h);

            ImGui::PushID(id);
            bool clicked;
            if (tex != ImTextureID_Invalid)
            {
                // A colored border via PushStyleColor is the ONLY reliable "selected" indicator here
                // -- ImageButton's own bg_col tint would dim the thumbnail itself, which defeats the
                // whole point of a thumbnail-only picker (RedFalcon's own "no hover text" design
                // intent -- the picture IS the label, don't obscure it).
                if (selected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f);
                }
                clicked = ImGui::ImageButton("##thumb", tex, ImVec2(kThumbSize, kThumbSize));
                if (selected)
                {
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                }
            }
            else
            {
                clicked = ImGui::Selectable("(missing)", selected, 0, ImVec2(kThumbSize, kThumbSize));
            }
            ImGui::PopID();
            return clicked;
        }

        // The always-visible "current pick" swatch (2026-09-11, RedFalcon: "make the image swatches
        // a pop out dropdown like the color swatches" -- same InvisibleButton-opens-a-popup shape as
        // CustomMenu.cpp's own GradientSwatchButton/DrawColorPickerPopup pair, just showing a real
        // thumbnail image instead of a flat color rect). Returns true when clicked -- the caller
        // opens the matching popup id in response, same as every other swatch button in this mod.
        // BUG FIX (2026-09-11, RedFalcon: "this is causing issues" -- an ImGui "2 visible items with
        // conflicting ID" warning popup): this is called twice per frame (once for the Body Type
        // preview, once for Origin), and every one of its 3 return paths used a bare literal ID/label
        // ("Choose...", "(missing)", "##preview") with no PushID wrapping the two call sites -- so
        // once BOTH grids had a real selection, both preview swatches resolved to the exact same
        // ImGui ID ("##preview") in the same window, the textbook cause of that warning. Callers now
        // each pass their own unique `id`, pushed here around all 3 branches.
        auto DrawPreviewSwatch(const char* id, const std::string& path, bool has_selection) -> bool
        {
            ImGui::PushID(id);
            bool clicked;
            if (!has_selection)
            {
                clicked = ImGui::Button("Choose...", ImVec2(kPreviewSize, kPreviewSize));
            }
            else
            {
                int w = 0, h = 0;
                ImTextureID tex = ImageLoader::GetOrLoad(path, w, h);
                if (tex == ImTextureID_Invalid)
                {
                    clicked = ImGui::Button("(missing)", ImVec2(kPreviewSize, kPreviewSize));
                }
                else
                {
                    clicked = ImGui::ImageButton("##preview", tex, ImVec2(kPreviewSize, kPreviewSize));
                }
            }
            ImGui::PopID();
            return clicked;
        }

        // Popup content is a no-op most frames (BeginPopup returns false until OpenPopup was called
        // for this exact id) -- same idiom as CustomMenu.cpp's own DrawColorPickerPopup, called
        // unconditionally every Draw() right after the swatch that opens it.
        auto DrawBodyTypePopup() -> void
        {
            if (!ImGui::BeginPopup("##barbie_bodytype_popup"))
            {
                return;
            }
            // Capped height + scrolling child (2026-09-11): 7 rows of 96px image buttons comfortably
            // exceeds a reasonable popup/screen height, unlike the color picker's own short 26px-tall
            // swatches -- CustomMenu.cpp's popup never needed this.
            ImGui::BeginChild("##barbie_bodytype_scroll", ImVec2(360.0f, 520.0f));
            ImGui::Columns(2, "##barbie_bodytype_cols", false);
            ImGui::TextUnformatted("Male");
            ImGui::NextColumn();
            ImGui::TextUnformatted("Female");
            ImGui::NextColumn();
            ImGui::Separator();
            for (int i = 0; i < kBodyTypeRowCount; ++i)
            {
                const BodyTypeRow& row = kBodyTypeRows[i];
                ImGui::PushID(i);

                const bool male_selected = g_selected_bodytype_row == i && g_selected_bodytype_is_male;
                if (DrawGridCell("m", ThumbPath("BodyTypes", row.male.thumb), male_selected))
                {
                    g_selected_bodytype_row = i;
                    g_selected_bodytype_is_male = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::TextUnformatted(row.label);
                ImGui::NextColumn();

                const bool female_selected = g_selected_bodytype_row == i && !g_selected_bodytype_is_male;
                if (DrawGridCell("f", ThumbPath("BodyTypes", row.female.thumb), female_selected))
                {
                    g_selected_bodytype_row = i;
                    g_selected_bodytype_is_male = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::TextUnformatted(row.label);
                ImGui::NextColumn();

                ImGui::PopID();
            }
            ImGui::Columns(1);
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        auto DrawOriginPopup() -> void
        {
            if (!ImGui::BeginPopup("##barbie_origin_popup"))
            {
                return;
            }
            ImGui::BeginChild("##barbie_origin_scroll", ImVec2(360.0f, 520.0f));
            // RedFalcon, 2026-09-11: make clear the Male/Female columns here are appearance previews
            // only -- sex is decided entirely by the Body Type pick (see kOriginRows's own header
            // comment), picking either column of a row selects the exact same Origin value.
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
            ImGui::TextDisabled("Male/Female here just preview how each origin looks -- they don't set the spawn's sex. That comes from your Body Type pick.");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            ImGui::Columns(2, "##barbie_origin_cols", false);
            ImGui::TextUnformatted("Male");
            ImGui::NextColumn();
            ImGui::TextUnformatted("Female");
            ImGui::NextColumn();
            ImGui::Separator();
            for (int i = 0; i < kOriginRowCount; ++i)
            {
                const OriginRow& row = kOriginRows[i];
                ImGui::PushID(i);

                const bool selected = g_selected_origin_row == i;
                if (DrawGridCell("m", ThumbPath("Origins", row.thumb_m), selected))
                {
                    g_selected_origin_row = i;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::TextUnformatted(row.family);
                ImGui::NextColumn();

                if (DrawGridCell("f", ThumbPath("Origins", row.thumb_f), selected))
                {
                    g_selected_origin_row = i;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::TextUnformatted(row.family);
                ImGui::NextColumn();

                ImGui::PopID();
            }
            ImGui::Columns(1);
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        auto WriteSpawnRequest() -> void
        {
            if (g_selected_bodytype_row < 0 || g_selected_origin_row < 0)
            {
                return;
            }
            const BodyTypeRow& row = kBodyTypeRows[g_selected_bodytype_row];
            const BodyTypeSex& cell = g_selected_bodytype_is_male ? row.male : row.female;
            const char* origin_family = kOriginRows[g_selected_origin_row].family;
            const char sex_char = g_selected_bodytype_is_male ? 'M' : 'F';

            std::ofstream f(REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] BarbieMenu: failed to write barbie_spawn_request.txt\n"));
                return;
            }
            // Grammar: CLASS:FAMILY:SEX:ORIGIN:NAME\n -- see main.lua's pollBarbieSpawnRequest for
            // the parser + what each field does. FAMILY is the donor's own NATIVE family (drives the
            // spawn's own mesh/skin resolution AND, on the Lua side, which DA_Custom_BodyTypeList_*
            // family-label to use for the origin retarget); ORIGIN is the requested destination
            // family, or exactly equal to FAMILY when no retarget is needed (spawn plain). NAME is
            // this cell's own donor name (2026-09-11, RedFalcon: "use the origin and bodytype in the
            // name, not literally the words") -- Lua builds the actual spawn label from NAME+SEX+
            // ORIGIN, not a fixed string.
            f << cell.class_path << ":" << cell.family << ":" << sex_char << ":" << origin_family << ":" << cell.name << "\n";
        }

        // "Zoom In"/"Zoom Out" (2026-09-11, RedFalcon: "Zoom In should change to Zoom Out and
        // clicking that returns to normal mode") -- own request file, own poll. g_zoomed is purely
        // this button's own local toggle state (same idiom CustomMenu.cpp's "Read Current"/
        // "Reading..." button already uses for a two-state label swap) -- it does NOT read back
        // whether the tripod camera is ACTUALLY still active on the Lua side, so if that ever gets
        // turned off some other way (e.g. a plain `lbphototripod off` typed in console) this button
        // would still say "Zoom Out" until clicked once more. Acceptable for a session-local toggle;
        // revisit with a real status round-trip (like MenuStatus::TargetLabel()'s own file bridge)
        // if that mismatch turns out to matter in practice.
        constexpr const char* ZOOM_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_zoom_request.txt";
        // "Full Body"/"Face View"/"Zoom Out" (2026-09-13, RedFalcon: "under camera change zoom in
        // to Full Body... add another button under it that is Face View... when either says zoom
        // out, drop it back to the regular character camera") -- ONE mode variable instead of two
        // independent bools so the two buttons are naturally mutually exclusive: selecting one
        // always clears the other's "Zoom Out" label on the next frame, with no extra bookkeeping.
        enum class ZoomMode { None, FullBody, Face };
        ZoomMode g_zoomMode = ZoomMode::None;
        auto WriteZoomRequest(const char* mode) -> void
        {
            std::ofstream f(ZOOM_REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] BarbieMenu: failed to write custom_zoom_request.txt\n"));
                return;
            }
            f << mode << "\n";
        }

        // Camera orbit rotate (2026-09-14, RedFalcon: "under face view add <- and -> buttons to
        // rotate on the Z axis no need for a label... When done I want rotation set back to before
        // zooming in", then widened same day: "can you also make it work in full body view?"). Own
        // tiny request file, payload is "<MODE>:<signed degree delta>" -- MODE tells the Lua side
        // which orbit accumulator/formula to use (Spawner.RotateFullBodyYaw vs.
        // Spawner.RotateFaceViewYaw), since Full Body and Face View use different base-pose math
        // (Spawner._computeTripodFullBodyPose vs. Spawner._computeHeadCenterPose). Each accumulator
        // resets to 0 every time its own camera mode (re)activates or Zoom Out fires, satisfying "set
        // back to before zooming in" without this side needing any state of its own beyond the
        // request file. Kept the "FACEVIEW" name (not renamed to something mode-neutral) since the
        // file path is already deployed/documented elsewhere -- only the payload shape changed.
        constexpr const char* FACEVIEW_ROTATE_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_faceview_rotate_request.txt";
        constexpr float kFaceViewRotateStepDegrees = 15.0f;
        auto WriteFaceViewRotateRequest(const char* mode, float deltaDegrees) -> void
        {
            std::ofstream f(FACEVIEW_ROTATE_REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] BarbieMenu: failed to write custom_faceview_rotate_request.txt\n"));
                return;
            }
            f << mode << ":" << deltaDegrees << "\n";
        }

    } // namespace

    auto Draw() -> void
    {
        const bool has_bodytype = g_selected_bodytype_row >= 0;
        const bool has_origin = g_selected_origin_row >= 0;

        ImGui::BeginGroup();
        ImGui::TextUnformatted("Body Type");
        std::string bt_path = has_bodytype
                ? ThumbPath("BodyTypes", g_selected_bodytype_is_male ? kBodyTypeRows[g_selected_bodytype_row].male.thumb
                                                                      : kBodyTypeRows[g_selected_bodytype_row].female.thumb)
                : std::string();
        if (DrawPreviewSwatch("##barbie_bodytype_preview", bt_path, has_bodytype))
        {
            ImGui::OpenPopup("##barbie_bodytype_popup");
        }
        if (has_bodytype)
        {
            ImGui::TextUnformatted(kBodyTypeRows[g_selected_bodytype_row].label);
            ImGui::TextDisabled(g_selected_bodytype_is_male ? "Male" : "Female");
        }
        else
        {
            ImGui::TextDisabled("(none)");
        }
        ImGui::EndGroup();

        ImGui::SameLine(0.0f, 40.0f);

        ImGui::BeginGroup();
        ImGui::TextUnformatted("Origin");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Male/Female previews only -- sex comes from Body Type.");
        }
        std::string origin_path = has_origin ? ThumbPath("Origins", g_selected_bodytype_is_male ? kOriginRows[g_selected_origin_row].thumb_m : kOriginRows[g_selected_origin_row].thumb_f)
                                              : std::string();
        if (DrawPreviewSwatch("##barbie_origin_preview", origin_path, has_origin))
        {
            ImGui::OpenPopup("##barbie_origin_popup");
        }
        if (has_origin)
        {
            ImGui::TextUnformatted(kOriginRows[g_selected_origin_row].family);
        }
        else
        {
            ImGui::TextDisabled("(none)");
        }
        ImGui::EndGroup();

        // "Zoom In" (2026-09-11, RedFalcon: "a button to the right of the portraits that only
        // toggles on when there's a target... point it at the middle of the actor... and pull back
        // from its front 200u") -- gated on the SAME target-lock CustomMenu.cpp's own cloth-color
        // panel uses, not the Body Type/Origin picks -- this operates on whatever's currently
        // target-locked (Num+), independent of the Barbie spawner above it.
        {
            const bool has_target = !MenuStatus::TargetLabel().empty();
            // Auto-reset label sync (2026-09-14, RedFalcon: "make it so that... losing a target
            // reset the view") -- Lua's own pollCameraAutoReset already fires the real
            // SetPhotoTripod("off", ...) reset when the target lock is lost while a camera mode is
            // active; this side has no way to read that back directly (g_zoomMode is a purely local
            // toggle, see its own declaration comment), but has_target going false while a mode is
            // active is the SAME signal Lua used, so resetting it here too keeps the button label
            // from going stale ("Zoom Out" lingering after the camera already reset itself).
            if (g_zoomMode != ZoomMode::None && !has_target)
            {
                g_zoomMode = ZoomMode::None;
            }
            // Zooming back OUT never needs a target (it's just "go back to normal") -- only the
            // initial Full Body/Face View click does. Each button independently allows itself to be
            // clicked when IT is the currently-active mode (so it can turn itself back off) or when
            // a target is locked (so it can turn itself on) -- clicking the OTHER button while one
            // mode is already active is also allowed and switches straight over (Lua's
            // FaceViewOnTarget/ZoomTripodOnTarget both reuse/reposition the same tripod actor rather
            // than requiring an off/on cycle in between).
            const bool can_click_fullbody = (g_zoomMode == ZoomMode::FullBody) || has_target;
            const bool can_click_face = (g_zoomMode == ZoomMode::Face) || has_target;
            // Disabled for the WHOLE duration of an active placement/relocate session, in either
            // direction (2026-09-11, RedFalcon: "let's not enable it until placement... clicking it
            // while it can be moved is a problem") -- switching the active view to/from the tripod
            // camera fights the follow-loop's own player-camera-relative math regardless of which
            // way the toggle is going.
            const bool placement_active = MenuStatus::IsPlacementActive();
            ImGui::SameLine(0.0f, 40.0f);
            ImGui::BeginGroup();
            ImGui::TextUnformatted("Camera");
            // Normal button height (2026-09-11, RedFalcon: "same height as the buttons on the tools
            // page") -- 0.0f height means ImGui's own default frame height, same convention every
            // OTHER real button in this mod uses (Spawn/Replace/Spawn Custom); kPreviewSize was only
            // ever this button's height because it started life visually paired with the swatches
            // above, not because anything about the layout required it.
            ImGui::BeginDisabled(!can_click_fullbody || placement_active || MenuStatus::IsRestoring());
            if (ImGui::Button(g_zoomMode == ZoomMode::FullBody ? "Zoom Out" : "Full Body", ImVec2(kPreviewSize, 0.0f)))
            {
                if (g_zoomMode == ZoomMode::FullBody)
                {
                    WriteZoomRequest("UNZOOM");
                    g_zoomMode = ZoomMode::None;
                }
                else
                {
                    WriteZoomRequest("ZOOM");
                    g_zoomMode = ZoomMode::FullBody;
                }
            }
            ImGui::EndDisabled();
            // "Face View" (2026-09-13, RedFalcon: "add another button under it that is Face View")
            // -- plain sequential Button() call, no SameLine(), so it stacks directly under Full
            // Body inside the same group.
            ImGui::BeginDisabled(!can_click_face || placement_active || MenuStatus::IsRestoring());
            if (ImGui::Button(g_zoomMode == ZoomMode::Face ? "Zoom Out" : "Face View", ImVec2(kPreviewSize, 0.0f)))
            {
                if (g_zoomMode == ZoomMode::Face)
                {
                    WriteZoomRequest("UNZOOM");
                    g_zoomMode = ZoomMode::None;
                }
                else
                {
                    WriteZoomRequest("FACE");
                    g_zoomMode = ZoomMode::Face;
                }
            }
            ImGui::EndDisabled();
            // Camera orbit rotate buttons (2026-09-14, RedFalcon: "under face view add <- and ->
            // buttons to rotate on the Z axis no need for a label. Make them fit so the two of them
            // fit the width of the button above", widened same day: "can you also make it work in
            // full body view?") -- meaningful whenever EITHER camera mode is active (no live tripod
            // yaw to nudge otherwise), so gated on that instead of can_click_face/can_click_fullbody.
            // Each is half kPreviewSize wide minus half the item spacing, so the pair together span
            // exactly kPreviewSize, matching Full Body/Face View above them. Which mode is currently
            // active decides the payload's MODE tag (see WriteFaceViewRotateRequest's own header).
            {
                const bool can_rotate = (g_zoomMode != ZoomMode::None) && !placement_active && !MenuStatus::IsRestoring();
                const char* rotate_mode = (g_zoomMode == ZoomMode::FullBody) ? "FULLBODY" : "FACE";
                const float halfW = (kPreviewSize - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                ImGui::BeginDisabled(!can_rotate);
                if (ImGui::Button("<##faceview_rotate_left", ImVec2(halfW, 0.0f)))
                {
                    WriteFaceViewRotateRequest(rotate_mode, -kFaceViewRotateStepDegrees);
                }
                ImGui::SameLine();
                if (ImGui::Button(">##faceview_rotate_right", ImVec2(halfW, 0.0f)))
                {
                    WriteFaceViewRotateRequest(rotate_mode, kFaceViewRotateStepDegrees);
                }
                ImGui::EndDisabled();
            }
            if (placement_active)
            {
                ImGui::TextDisabled("(placing...)");
            }
            else if (!has_target && g_zoomMode == ZoomMode::None)
            {
                ImGui::TextDisabled("(no target)");
            }
            ImGui::EndGroup();
        }

        // Physique dropdown MOVED to CustomMenu.cpp's new "Body" section (2026-09-12, RedFalcon:
        // "where selected target used to be add a 'Body' section with Physique in it") -- see that
        // file instead.

        DrawBodyTypePopup();
        DrawOriginPopup();

        ImGui::Spacing();

        const bool has_both = has_bodytype && has_origin;
        ImGui::BeginDisabled(!has_both || MenuStatus::IsRestoring());
        if (ImGui::Button("Spawn Custom", ImVec2(150.0f, 0.0f)))
        {
            WriteSpawnRequest();
            // Same "hand focus back to the game" convention as the roster tree's own Spawn button
            // (SpawnMenu.cpp) -- placing something shouldn't leave the player stuck alt-tabbed in.
            StandaloneWindow::ReturnFocusToGame();
        }
        ImGui::EndDisabled();
        if (!has_both)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Pick a Body Type and an Origin first.");
        }
    }
} // namespace RC::LivingBaseSpawnMenu::BarbieMenu
