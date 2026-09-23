#include <CustomMenu.hpp>

#include <BarbieMenu.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <ImageLoader.hpp>
#include <MenuStatus.hpp>
#include <SpawnMenu.hpp>
#include <StandaloneWindow.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
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

        // Clothes item lists (2026-09-14) -- generated by gen_clothes_cpp.py from the SAME
        // Other/Hair_And_Clothes_Export.xlsx source Config.CLOTHES_ITEMS (config.lua) is built
        // from -- see gen_clothes_lua.py. KEEP IN SYNC BY HAND with config.lua if the spreadsheet
        // is ever revised (re-run gen_clothes_cpp.py, re-paste). `locked` items ("Only with
        // Unlock" in the sheet) are skipped entirely from the dropdown unless g_clothesUnlocked
        // is true (mirrors Config.CLOTHES_UNLOCK_ALL, read live from clothes_unlock_state.txt --
        // see pollClothesUnlockState). No more reserved "(Remove)" sentinel at index 0 (2026-09-14,
        // RedFalcon: "not a huge fan of the remove inside the dropdowns... put a red X button next
        // to the swatches instead") -- see RemoveXButton's own header comment and its call site in
        // Draw()'s per-category loop, which fires that exact same "(Remove)" wire string now.
        struct ClothesItem { const char* name; bool locked; };
        constexpr ClothesItem kClothesTorsoItems[] = {
            { "Adventurer Torso 1", false },
            { "Bandit Torso 1", false },
            { "Blackbeard Grenadier Torso 1", false },
            { "Blackbeard Grenadier Torso 2", false },
            { "Blackbeard Grenadier Torso 3", false },
            { "Blackbeard Musketeer Torso 1", false },
            { "Blackbeard Musketeer Torso 2", false },
            { "Blackbeard Musketeer Torso 3", false },
            { "Blackbeard Sailor Torso 1", false },
            { "Blackbeard Sailor Torso 2", false },
            { "Blackbeard Sailor Torso 3", false },
            { "Blackbeard Sergeant Torso 1", false },
            { "Blackbeard Sergeant Torso 2", false },
            { "Blackbeard Sergeant Torso 3", false },
            { "Brigant Torso 1", false },
            { "Character Underwear Torso", false },
            { "Combatant Torso 1", false },
            { "Combatant Torso 2", false },
            { "Combatant Torso 3", false },
            { "Conquistador Torso 1", false },
            { "Dogface Torso 1", false },
            { "Dogface Torso 2", false },
            { "Dogface Torso 3", false },
            { "Flibustier Torso 1", false },
            { "Flibustier Torso 2", false },
            { "Flibustier Torso 2 - Long", false },
            { "Flibustier Torso 3", false },
            { "Flibustier Torso 4", false },
            { "Flibustier Torso 5", false },
            { "Ghostly Torso", false },
            { "Jeweler Torso 1", false },
            { "Jeweler Torso 2", false },
            { "Jeweler Torso 3", false },
            { "Jeweler Torso 4", false },
            { "Jeweler Torso 5", false },
            { "Jeweler Torso 6", false },
            { "Jeweler Torso 7", false },
            { "Jeweler Torso 8", false },
            { "Mercenary Torso 1", false },
            { "Musketeer Torso 1", false },
            { "Musketeer Torso 2", false },
            { "Musketeer Torso 3", false },
            { "Pikeman Torso 1", false },
            { "Senkamati Hunter Feather Torso 1", true },
            { "Senkamati Hunter Feather Torso 2", true },
            { "Senkamati Hunter Feather Torso 3", true },
            { "Senkamati Shaman Feather Torso 1", true },
            { "Senkamati Shaman Feather Torso 2", true },
            { "Senkamati Thrall Feather Torso 1", true },
            { "Senkamati Thrall Feather Torso 2", true },
            { "Senkamati Thrall Feather Torso 3", true },
            { "Senkamati Warrior Feather Torso 1", true },
            { "Senkamati Warrior Feather Torso 2", true },
            { "Senkamati Warrior Feather Torso 3", true },
            { "Starter Torso 1", false },
            { "Starter Torso 2", false },
            { "Vanilla Torso 1", false },
        };
        constexpr ClothesItem kClothesLegsItems[] = {
            { "Adventurer Legs 1", false },
            { "Bandit Legs 1", false },
            { "Blackbeard Grenadier Legs 1", false },
            { "Blackbeard Grenadier Legs 2", false },
            { "Blackbeard Grenadier Legs 3", false },
            { "Blackbeard Huntsman Legs 1", false },
            { "Blackbeard Musketeer Legs 1", false },
            { "Blackbeard Musketeer Legs 2", false },
            { "Blackbeard Musketeer Legs 3", false },
            { "Blackbeard Sailor Legs 1", false },
            { "Blackbeard Sailor Legs 2", false },
            { "Blackbeard Sailor Legs 3", false },
            { "Blackbeard Sergeant Legs 1", false },
            { "Blackbeard Sergeant Legs 2", false },
            { "Blackbeard Sergeant Legs 3", false },
            { "Brigant Legs 1", false },
            { "Character Underwear Legs", false },
            { "Combatant Legs 1", false },
            { "Combatant Legs 2", false },
            { "Combatant Legs 3", false },
            { "Conquistador Legs 1", false },
            { "Crafter Legs 1", false },
            { "Crafter Legs 2", false },
            { "Crafter Legs 3", false },
            { "Dogface Legs 1", false },
            { "Dogface Legs 2", false },
            { "Dogface Legs 3", false },
            { "Flibustier Legs 1", false },
            { "Flibustier Legs 2", false },
            { "Flibustier Legs 3", false },
            { "Ghostly Legs", false },
            { "Jeweler Legs 1", false },
            { "Jeweler Legs 2", false },
            { "Jeweler Legs 3", false },
            { "Mercenary Legs 1", false },
            { "Musketeer Legs 1", false },
            { "Musketeer Legs 2", false },
            { "Musketeer Legs 3", false },
            { "Pikeman Legs 1", false },
            { "Senkamati Hunter Feather Legs 1", true },
            { "Senkamati Hunter Feather Legs 2", true },
            { "Senkamati Hunter Feather Legs 3", true },
            { "Senkamati Hunter Wood Legs 1", true },
            { "Senkamati Hunter Wood Legs 2", true },
            { "Senkamati Hunter Wood Legs 3", true },
            { "Senkamati Shaman Feather Legs 1", true },
            { "Senkamati Shaman Feather Legs 2", true },
            { "Senkamati Thrall Feather Legs 1", true },
            { "Senkamati Thrall Feather Legs 2", true },
            { "Senkamati Thrall Feather Legs 3", true },
            { "Senkamati Thrall Wood Legs 1", true },
            { "Senkamati Thrall Wood Legs 2", true },
            { "Senkamati Thrall Wood Legs 3", true },
            { "Senkamati Warrior Feather Legs 1", true },
            { "Senkamati Warrior Feather Legs 2", true },
            { "Senkamati Warrior Feather Legs 3", true },
            { "Starter Legs 1", false },
            { "Starter Legs 2", false },
            { "Unique - Galen Skelton Legs", true },
            { "Unique - John Legs", true },
            { "Vanilla Legs 1", false },
        };
        constexpr ClothesItem kClothesWaistItems[] = {
            { "Adventurer Waist 1", false },
            { "Bandit Waist 1", false },
            { "Blackbeard Sailor Waist 2", false },
            { "Blackbeard Sailor Waist 3", false },
            { "Brigant Waist 1", false },
            { "Flibustier Waist 1", false },
            { "Jeweler Waist 1", false },
            { "Jeweler Waist 2", false },
            { "Jeweler Waist 3", false },
            { "Mercenary Waist 1", false },
            { "Pikeman Waist 1", false },
            { "Starter Waist 1", false },
            { "Starter Waist 2", false },
            { "Vanilla Waist 1", false },
        };
        constexpr ClothesItem kClothesHandsItems[] = {
            { "Adventurer Hands 1", false },
            { "Bandit Hands 1", false },
            { "Blackbeard Grenadier Hands 1", false },
            { "Blackbeard Grenadier Hands 2", false },
            { "Blackbeard Grenadier Hands 3", false },
            { "Blackbeard Musketeer Hands 1", false },
            { "Blackbeard Musketeer Hands 2", false },
            { "Blackbeard Musketeer Hands 3", false },
            { "Blackbeard Sailor Hands 1", false },
            { "Blackbeard Sailor Hands 2", false },
            { "Blackbeard Sailor Hands 3", false },
            { "Blackbeard Sergeant Hands 1", false },
            { "Blackbeard Sergeant Hands 2", false },
            { "Blackbeard Sergeant Hands 3", false },
            { "Brigant Hands 1", false },
            { "Combatant Hands 1", false },
            { "Combatant Hands 2", false },
            { "Combatant Hands 3", false },
            { "Conquistador Hands 1", false },
            { "Drowned Armored Hands 1", false },
            { "Drowned Armored Hands 2", false },
            { "Drowned Armored Hands 3", false },
            { "Drowned Hands 1", false },
            { "Drowned Hands 2", false },
            { "Drowned Hands 3", false },
            { "Flibustier Hands 1", false },
            { "Flibustier Hands 1 - Long", false },
            { "Flibustier Hands 2", false },
            { "Flibustier Hands 2 - Long", false },
            { "Ghostly Gloves", false },
            { "Jeweler Hands 1", false },
            { "Jeweler Hands 2", false },
            { "Jeweler Hands 3", false },
            { "Mercenary Hands 1", false },
            { "Musketeer Hands 1", false },
            { "Musketeer Hands 2", false },
            { "Musketeer Hands 3", false },
            { "Pikeman Hands 1", false },
            { "Senkamati Hunter Feather Hands 1", true },
            { "Senkamati Hunter Feather Hands 2", true },
            { "Senkamati Hunter Feather Hands 3", true },
            { "Senkamati Shaman Feather Hands 1", true },
            { "Senkamati Shaman Feather Hands 2", true },
            { "Senkamati Thrall Feather Hands 1", true },
            { "Senkamati Thrall Feather Hands 2", true },
            { "Senkamati Thrall Feather Hands 3", true },
            { "Senkamati Thrall Wood Hands 1", true },
            { "Senkamati Thrall Wood Hands 2", true },
            { "Senkamati Thrall Wood Hands 3", true },
            { "Senkamati Warrior Feather Hands 1", true },
            { "Senkamati Warrior Feather Hands 2", true },
            { "Senkamati Warrior Feather Hands 3", true },
            { "Starter Hands 1", false },
            { "Unique - John Hands", false },
            { "Vanilla Hands 1", false },
        };
        constexpr ClothesItem kClothesFeetItems[] = {
            { "Adventurer Feet 1", false },
            { "Bandit Feet 1", false },
            { "Blackbeard Grenadier Feet 1", false },
            { "Blackbeard Grenadier Feet 2", false },
            { "Blackbeard Grenadier Feet 3", false },
            { "Blackbeard Musketeer Feet 1", false },
            { "Blackbeard Musketeer Feet 1 - Long", false },
            { "Blackbeard Musketeer Feet 2", false },
            { "Blackbeard Musketeer Feet 2 - Long", false },
            { "Blackbeard Musketeer Feet 3", false },
            { "Blackbeard Musketeer Feet 3 - Long", false },
            { "Blackbeard Sailor Feet 1", false },
            { "Blackbeard Sailor Feet 2", false },
            { "Blackbeard Sailor Feet 3", false },
            { "Blackbeard Sergeant Feet 1", false },
            { "Blackbeard Sergeant Feet 2", false },
            { "Blackbeard Sergeant Feet 3", false },
            { "Brigant Feet 1", false },
            { "Combatant Feet 1", false },
            { "Combatant Feet 2", false },
            { "Combatant Feet 3", false },
            { "Conquistador Feet 1", false },
            { "Dogface Feet 1", false },
            { "Dogface Feet 2", false },
            { "Dogface Feet 3", false },
            { "Drowned Armored Feet 1", false },
            { "Drowned Armored Feet 2", false },
            { "Drowned Armored Feet 3", false },
            { "Drowned Feet 1", false },
            { "Drowned Feet 2", false },
            { "Drowned Feet 3", false },
            { "Flibustier Feet 1", false },
            { "Flibustier Feet 1 (2)", false },
            { "Flibustier Feet 2", false },
            { "Ghostly Boots", false },
            { "Jeweler Feet 1", false },
            { "Jeweler Feet 2", false },
            { "Jeweler Feet 3", false },
            { "Mercenary Feet 1", false },
            { "Musketeer Feet 1", false },
            { "Musketeer Feet 2", false },
            { "Musketeer Feet 3", false },
            { "Pikeman Feet 1", false },
            { "Senkamati Hunter Feather Feet 1", true },
            { "Senkamati Hunter Feather Feet 2", true },
            { "Senkamati Hunter Feather Feet 3", true },
            { "Senkamati Shaman Feather Feet 1", true },
            { "Senkamati Shaman Feather Feet 2", true },
            { "Senkamati Thrall Feather Feet 1", true },
            { "Senkamati Thrall Feather Feet 2", true },
            { "Senkamati Thrall Feather Feet 3", true },
            { "Senkamati Thrall Wood Feet 1", true },
            { "Senkamati Thrall Wood Feet 2", true },
            { "Senkamati Thrall Wood Feet 3", true },
            { "Senkamati Warrior Feather Feet 1", true },
            { "Senkamati Warrior Feather Feet 2", true },
            { "Senkamati Warrior Feather Feet 3", true },
            { "Starter Feet 1", false },
            { "Starter Feet 2", false },
            { "Unique - Galen Skelton Feet", false },
            { "Unique - John Feet", false },
            { "Vanilla Feet 1", false },
        };
        constexpr ClothesItem kClothesHatItems[] = {
            { "Adventurer Head 1", false },
            { "Bandit Head 1", false },
            { "Blackbeard Grenadier Head 1", false },
            { "Blackbeard Grenadier Head 2", false },
            { "Blackbeard Grenadier Head 3", false },
            { "Blackbeard Musketeer Head 1", false },
            { "Blackbeard Musketeer Head 2", false },
            { "Blackbeard Musketeer Head 3", false },
            { "Blackbeard Sailor Head 1", false },
            { "Blackbeard Sailor Head 2", false },
            { "Blackbeard Sergeant Head 1", false },
            { "Blackbeard Sergeant Head 2", false },
            { "Blackbeard Sergeant Head 3", false },
            { "Brigant Head 1", false },
            { "Combatant Head 1", false },
            { "Combatant Head 2", false },
            { "Combatant Head 3", false },
            { "Conquistador Head 1", false },
            { "Dogface Head 1", false },
            { "Dogface Head 2", false },
            { "Dogface Head 3", false },
            { "Drowned Armored Head 1", false },
            { "Drowned Armored Head 2", false },
            { "Drowned Armored Head 3", false },
            { "Drowned Head 1", false },
            { "Drowned Head 2", false },
            { "Drowned Head 3", false },
            { "Flibustier Head 1", false },
            { "Flibustier Head 2", false },
            { "Flibustier Head 3", false },
            { "Flibustier Head 4", false },
            { "Ghostly Helmet", false },
            { "Jeweler Head 1", false },
            { "Jeweler Head 2", false },
            { "Jeweler Head 3", false },
            { "Jeweler Head 4", false },
            { "Jeweler Head 7", false },
            { "Mercenary Head 1", false },
            { "Mercenary Head 1 (2)", false },
            { "Mercenary Head Hat 1", false },
            { "Musketeer Head 1", false },
            { "Musketeer Head 2", false },
            { "Musketeer Head 3", false },
            { "Pikeman Head 1", false },
            { "Senkamati Head 1", true },
            { "Senkamati Hunter Feather Head 1", true },
            { "Senkamati Hunter Feather Head 2", true },
            { "Senkamati Hunter Feather Head 3", true },
            { "Senkamati Shaman Feather Head 1", true },
            { "Senkamati Shaman Feather Head 2", true },
            { "Senkamati Shaman Feather Head 3", true },
            { "Senkamati Thrall Feather Head 1", true },
            { "Senkamati Thrall Feather Head 2", true },
            { "Senkamati Thrall Feather Head 3", true },
            { "Senkamati Thrall Feather Head 4", true },
            { "Senkamati Warrior Feather Head 1", true },
            { "Senkamati Warrior Feather Head 2", true },
            { "Senkamati Warrior Feather Head 3", true },
            { "Solo Player Head 1", false },
            { "Solo Player Head 2", false },
            { "Starter Head 1", false },
            { "Vanilla Head 1", false },
        };
        constexpr ClothesItem kClothesCapeItems[] = {
            { "Conquistador Cape 1", false },
            { "Flibustier Cape 2", false },
            { "Jeweler Cape 2", false },
            { "Jeweler Cape 3", false },
            { "Jeweler Cape 4", false },
            { "Pikeman Torso 1", false },
            { "Senkamati Shaman Feather Cape 1", true },
            { "Senkamati Shaman Feather Cape 2", true },
            { "Senkamati Shaman Feather Neck 2", true },
            { "Unique - Galen Skelton Cape", false },
            { "Unique - John Cape", false },
        };
        // Mask ADDED (2026-09-16, RedFalcon: "add it back in, as some of the pregen NPCs have it,
        // same as waist already being there despite not being able to use it") -- generated by
        // gen_clothes_cpp.py from the raw "Clothes" sheet (Mask has no rows in "Clothes Adjusted",
        // never covered by RedFalcon's own curated pass since Mask was excluded when that sheet was
        // built). All 3 real entries: 2 MALE ONLY (Mask 1/2) + 1 dual-sex (Mask 3) -- none locked.
        constexpr ClothesItem kClothesMaskItems[] = {
            { "Blackbeard Sailor Mask 1", false },
            { "Blackbeard Sailor Mask 2", false },
            { "Blackbeard Sailor Mask 3", false },
        };

        // Outfit dropdown (top of the Clothes section) -- same qualifying-set filter as
        // gen_clothes_lua.py's Config.CLOTHES_OUTFITS (must define at least Feet+Torso+Legs).
        // No `locked` concept here -- an outfit referencing a currently-locked piece just silently
        // skips that one piece on apply (Spawner.ApplyClothesOutfit's own per-piece
        // Spawner.ApplyClothesItem call already handles that gracefully). The "(Remove All)"
        // sentinel that used to sit at index 0 here is GONE (2026-09-14, RedFalcon: "not a huge fan
        // of the remove inside the dropdowns... put a red X button next to the swatches instead") --
        // DrawClothesOutfitRow's own red RemoveXButton fires that exact same wire string now.
        constexpr const char* kClothesOutfitNames[] = {
            "Adventurer 1",
            "Bandit 1",
            "Blackbeard Grenadier 1",
            "Blackbeard Grenadier 2",
            "Blackbeard Grenadier 3",
            "Blackbeard Musketeer 1",
            "Blackbeard Musketeer 2",
            "Blackbeard Musketeer 3",
            "Blackbeard Sailor 1",
            "Blackbeard Sailor 2",
            "Blackbeard Sailor 3",
            "Blackbeard Sergeant 1",
            "Blackbeard Sergeant 2",
            "Blackbeard Sergeant 3",
            "Brigant 1",
            "Combatant 1",
            "Combatant 2",
            "Combatant 3",
            "Conquistador 1",
            "Dogface 1",
            "Dogface 2",
            "Dogface 3",
            "Flibustier 1",
            "Flibustier 2",
            "Ghostly",
            "Jeweler 1",
            "Jeweler 2",
            "Jeweler 3",
            "Mercenary 1",
            "Musketeer 1",
            "Musketeer 2",
            "Musketeer 3",
            "Pikeman 1",
            "Starter 1",
            "Starter 2",
            "Vanilla 1",
        };

        // The 8-row list (Mask added 2026-09-16, see kClothesMaskItems' own comment). `key` is the
        // exact string sent over custom_color_request.txt/read back from custom_color_status.txt --
        // KEEP IN SYNC BY HAND with config.lua's Config.CUSTOM_TAB_CLOTH_CATEGORIES (same key
        // strings; bodyPart resolution itself stays Lua's own concern, see this file's own header
        // comment). `slotCount` is how many of the 3 CPD slots this category actually shows a
        // separate swatch for (2026-09-08, RedFalcon: "so each color # has its own swatch button
        // where applicable") -- 3 for the pieces that genuinely use all 3 slots, 1 for
        // Waist/Cape/Mask (see Config.CPD_BODYPART_COLOR_INFO's own notes on why those only have ONE
        // real slot). `soloSlot` is which of the 3 raw wire slots a slotCount==1 row's single swatch
        // actually reads/writes -- Waist only reads Color3; Cape/Mask's real slot varies by piece
        // (Mask has no documented slot at all, same as Cape), so Color1 is just this row's own
        // representative choice (write-time still fans it out to all 3 uniformly, matching the
        // existing "write the same value to all 3 slots to be safe" rule). `displayCol` (2026-09-16,
        // RedFalcon: "Waist's color is to the right next to its X, please move it so its next to the
        // label, like Cape is") is a SEPARATE, purely-visual column index for where a slotCount==1
        // row's one real swatch is drawn in the 3-swatch-wide row (0/1/2, matching Cape's own
        // next-to-the-dropdown position) -- decoupled from `soloSlot` on purpose, since soloSlot
        // still has to say which REAL CPD channel this piece reads/reports (Waist's is genuinely
        // Color3) regardless of which column its swatch happens to be drawn in. `luaBodyPart`/
        // `items`/`itemCount` (2026-09-14) are the Clothes name-dropdown's own data -- `luaBodyPart`
        // is the exact bodyPart string Config.CLOTHES_ITEMS/main.lua's pollCustomClothesItemRequest
        // expect (Feet->Feets, Hat->Headgear, everything else matches the label).
        struct Category
        {
            const char* key;
            const char* label;
            int slotCount;
            int soloSlot;    // which real CPD channel this solo row reads/reports -- only meaningful when slotCount == 1
            int displayCol;  // which of the 3 visual swatch columns this solo row's swatch draws in -- only meaningful when slotCount == 1
            const char* luaBodyPart;
            const ClothesItem* items;
            int itemCount;
        };
        constexpr Category kCategories[8] = {
            {"TORSO", "Torso", 3, 0, 0, "Torso",    kClothesTorsoItems, static_cast<int>(std::size(kClothesTorsoItems))},
            {"LEGS",  "Legs",  3, 0, 0, "Legs",     kClothesLegsItems,  static_cast<int>(std::size(kClothesLegsItems))},
            {"WAIST", "Waist", 1, 2, 0, "Waist",    kClothesWaistItems, static_cast<int>(std::size(kClothesWaistItems))},
            {"HANDS", "Hands", 3, 0, 0, "Hands",    kClothesHandsItems, static_cast<int>(std::size(kClothesHandsItems))},
            {"FEET",  "Feet",  3, 0, 0, "Feets",    kClothesFeetItems,  static_cast<int>(std::size(kClothesFeetItems))},
            {"HAT",   "Hat",   3, 0, 0, "Headgear", kClothesHatItems,   static_cast<int>(std::size(kClothesHatItems))},
            {"CAPE",  "Cape",  1, 0, 0, "Cape",     kClothesCapeItems,  static_cast<int>(std::size(kClothesCapeItems))},
            {"MASK",  "Mask",  1, 0, 0, "Mask",     kClothesMaskItems,  static_cast<int>(std::size(kClothesMaskItems))},
        };
        constexpr int kCategoryCount = 8;

        // g_selected[i][s]: -1 = nothing picked yet for that (category, slot). Persists for as long
        // as the DLL is loaded (same lifetime as MoveMenu.cpp's g_precision_idx) -- picking a color
        // doesn't apply it by itself, only the Apply button does, so this is deliberately "sticky"
        // across frames/tabs rather than reset on every draw. Only index 0 is meaningful for a
        // slotCount==1 row.
        int g_selected[kCategoryCount][3] = {};

        // Tracks the last value actually SENT via writeColorRequest for each (category, slot),
        // so picking a cloth color applies immediately (2026-09-14, RedFalcon: "i also want
        // clothing color to be real time... remove both the reset and apply buttons") -- same
        // "write exactly once per change" pattern HairRowState::lastWrittenColor already uses.
        // -2 is a sentinel distinct from -1 ("nothing picked") so the very first real pick still
        // triggers a write.
        int g_lastWrittenClothColor[kCategoryCount][3] = {
            {-2, -2, -2}, {-2, -2, -2}, {-2, -2, -2}, {-2, -2, -2}, {-2, -2, -2}, {-2, -2, -2}, {-2, -2, -2}, {-2, -2, -2},
        };

        // Clothes item dropdowns (2026-09-14) -- g_clothesSelected[i] indexes into
        // kCategories[i].items (-1 = nothing picked, shows "(select)"), immediate-apply like every
        // other dropdown in this file (no separate Apply button). g_clothesOutfitSelected indexes
        // kClothesOutfitNames the same way. g_clothesUnlocked mirrors Config.CLOTHES_UNLOCK_ALL,
        // kept current by pollClothesUnlockState (called once per frame from DrawTargetHeader).
        int g_clothesSelected[kCategoryCount] = { -1, -1, -1, -1, -1, -1, -1, -1 };
        int g_clothesOutfitSelected = -1;
        bool g_clothesUnlocked = false;

        // g_clothesSlotAvailable[i] (2026-09-16, RedFalcon: "if a target doesnt have a swappable
        // item in the hat slot, Grey out the hat selectors. If they dont have a swapable belt, grey
        // out the belt selector") -- true only once a Read Current confirms this target's own
        // composite actually HAS a BuildedCompositeMeshes entry for kCategories[i]'s BodyPart at all
        // (Spawner.TestReadClothesStyles' new second return value) -- distinct from g_clothesSelected
        // being -1, which just means nothing in the catalog matched the CURRENT mesh (a slot that
        // exists but holds an unrecognized/custom mesh is still swappable). Defaults false, same
        // "grey out until proven otherwise" convention g_beltVisible/g_slingVisible/etc already use --
        // safe because the whole Clothes section is already behind the g_hasDetected gate, so nothing
        // is interactive before a first real detect anyway.
        bool g_clothesSlotAvailable[kCategoryCount] = {};

        // Senkamati body detection (2026-09-14, RedFalcon: "the female senkamati should not be
        // allowed to change torso and legs to anything but other female senkamati torso and leg
        // parts... when a senkamati is selected and detected, the gender specific senkamati items
        // should be available") -- "Male"/"Female"/"" ("" = target isn't a Senkamati skeleton at
        // all), synced from Read Current's own "SENKAMATI:<sex>" line (Spawner.TestReadSenkamatiSex).
        // DrawClothesItemCombo's own Torso/Legs filtering is the only thing that reads this.
        std::string g_targetSenkamatiSex;

        // Mirrors spawner.lua's own senkamatiClothesRowSex(row) exactly, just keyed off the DISPLAY
        // name this DLL actually has (kClothesTorsoItems/kClothesLegsItems never carry the row's raw
        // `name` field, only the friendly display name) -- confirmed against config.lua's own rows
        // that the "Senkamati Shaman ..." / "Senkamati Hunter|Thrall|Warrior ..." prefix convention
        // holds for the display name too. Returns 'F'/'M'/0 (0 = not a Senkamati item at all).
        auto SenkamatiItemSex(const char* itemName) -> char
        {
            if (std::strncmp(itemName, "Senkamati Shaman ", 17) == 0) return 'F';
            if (std::strncmp(itemName, "Senkamati Hunter ", 17) == 0) return 'M';
            if (std::strncmp(itemName, "Senkamati Thrall ", 17) == 0) return 'M';
            if (std::strncmp(itemName, "Senkamati Warrior ", 18) == 0) return 'M';
            return 0;
        }

        // The Outfit row's own 3 "apply to all clothing items" swatches (2026-09-14) -- see
        // DrawClothesOutfitRow's own header comment. Same -1/-2 sentinel convention as every other
        // color picker in this file.
        int g_outfitColorSelected[3] = { -1, -1, -1 };
        int g_outfitColorLastWritten[3] = { -2, -2, -2 };

        // Read Current: request/response is asynchronous (main.lua polls every 400ms), so this
        // tracks a pending read across frames rather than blocking. Timeout is generous (2s) --
        // this only ever fails to resolve if LivingBase itself isn't loaded/running, in which case
        // spinning forever would be a worse symptom than just giving up quietly.
        bool g_readPending = false;
        std::chrono::steady_clock::time_point g_readRequestedAt{};
        constexpr auto kReadTimeout = std::chrono::seconds(2);

        // Detect gate (2026-09-14, RedFalcon: "I'd like to require a detect before making any
        // changes. so spawning and camera is fine, but nothing else until detected") -- Body/Hair/
        // Clothes below are all gated on g_hasDetected in addition to the existing hasTarget check;
        // spawning (SpawnMenu.cpp/BarbieMenu.cpp) and camera (BarbieMenu.cpp's Full Body/Face View/
        // zoom/orbit) live in other files entirely and are untouched, so they stay exempt by
        // construction. Tracks the locked target's own STABLE identity (TargetId(), not the cosmetic
        // TargetLabel() -- two different actors can share a label, see MenuStatus::TargetId()'s own
        // comment) so switching to a genuinely different actor re-locks editing until a fresh Read
        // Current confirms its real state; g_hasDetected is set true at the exact point a Read
        // Current cycle finishes successfully (see pollReadCurrentResult), same whether that cycle
        // was a manual button click or the barbie-spawn auto-detect below.
        //
        // Was TEMPORARILY BYPASSED 2026-09-16 (a live diagnostic experiment for the UE4SS.dll crash
        // saga, WINDROSE_MODDING_NOTES.md 19ai/3u -- RedFalcon: "can we temporarily remove the
        // detect requirement... I want to see if maybe its just the detect side that is an issue")
        // -- RE-ENABLED 2026-09-18 (RedFalcon: "let's re-enable locking edits until a detect") now
        // that the experiment concluded the detect side wasn't the crash cause. All 6
        // ImGui::BeginDisabled(...) gates further down have their own "|| !g_hasDetected" clause
        // back, so editing Body/Hair/Clothes/Belts/Poses once again requires a completed detect
        // first -- spawning and camera stay exempt by construction (they live in other files).
        bool g_hasDetected = false;
        std::string g_lastDetectTargetId;

        // Belts and Straps state (2026-09-15) -- declared here (well above DrawBeltsAndStraps' own
        // section) because pollReadCurrentResult's "BELTVISIBLE:"/"BELTPIECE:" parsing needs them and
        // sits between here and there; same forward-declaration reasoning as kBeltNames etc above.
        // -1 == nothing picked ("None"/default); visibility flags default false until the first
        // successful Read Current (RedFalcon: "the different accessory areas should be unavailable
        // if that strap is not available... Pistols is dependant on belt and sheath is dependant on
        // frog").
        int g_beltPieceSelected = -1;
        int g_slingPieceSelected = -1;
        int g_strapPieceSelected = -1;
        int g_frogPieceSelected = -1;
        bool g_beltVisible = false;
        bool g_slingVisible = false;
        bool g_strapVisible = false;
        bool g_frogVisible = false;
        // g_xxxAvailable (2026-09-16, RedFalcon: "let's extend this to the hair, facial hair and
        // belts and straps") -- distinct from g_xxxVisible: a HIDDEN piece (picked "None") is still
        // a real, swappable composite entry -- only a target with no such entry AT ALL should grey
        // out the piece SELECTOR itself. g_xxxVisible is UNCHANGED and keeps driving the Accessories
        // grid's own socket-dependency gating (Pistols need the belt actually WORN, not just present).
        bool g_beltAvailable = false;
        bool g_slingAvailable = false;
        bool g_strapAvailable = false;
        bool g_frogAvailable = false;

        // "Poses and Actions" state (2026-09-16) -- declared here for the same reason as the Belts
        // and Straps block just above: pollReadCurrentResult's "POSE:" parsing needs these and sits
        // between here and DrawPosesAndActions' own section further down. g_currentPoseName
        // defaults to "Unknown", the same fallback a target with no readable pose gets.
        std::string g_currentPoseName = "Unknown";
        // g_handCategorySelected[hand][category] (2026-09-21, replaces the old single
        // g_leftHandSelected/g_rightHandSelected now that real item lists exist) -- hand: 0=Left,
        // 1=Right; category: matches kHandCategoryRows' own order (Weapons/Tools/Bottles/Other).
        // -1 == nothing picked in that ONE category's dropdown -- since a hand can only ever hold
        // one item, picking anything in ANY of the 4 category dropdowns for a hand clears the
        // other 3 for that same hand (see DrawPosesAndActions' own hand-row loop).
        int g_handCategorySelected[2][4] = { { -1, -1, -1, -1 }, { -1, -1, -1, -1 } };

        // "Height" slider state (2026-09-18, RedFalcon's height-slider idea, 3ft-8ft range) --
        // declared here for the same forward-declaration reason as the blocks above:
        // pollReadCurrentResult's own "HEIGHT:" parsing needs this and sits well before the Body
        // section draws it. Defaults to 5.8333ft (scale 1.0, see CS.FEET_PER_SCALE_UNIT's own
        // header in spawner.lua) until a real Read Current overwrites it. No "LastWritten" sentinel
        // needed (unlike Skin Tone's swatch index) -- ImGui::SliderFloat's own return value already
        // means "the value just changed this frame," so the write fires directly off that.
        float g_heightFeet = 5.0f + 10.0f / 12.0f;

        // AI Toggle + Lantern state (2026-09-21) -- declared here for the same forward-declaration
        // reason as the blocks above: pollReadCurrentResult's own "AITOGGLE:"/"LANTERN:" parsing
        // (added same day, see that section's own header) sits well before the Body/Accessories
        // sections that draw these. Both default false ("AI running"/"lantern off"), matching the
        // reset block further down.
        bool g_aiDisabled = false;
        bool g_lanternOn = false;

        // Barbie auto-detect (2026-09-14, RedFalcon: "Also have the spawned barbies detect after
        // processing everything") -- main.lua's pollBarbieSpawnRequest locks the freshly-built
        // Barbie as the target (mirroring Spawner.ToggleTargetLock) and drops this one-line trigger
        // right after, so DrawTargetHeader() (called every frame regardless of which tab is open)
        // can fire the exact same requestReadCurrent() the "Read Current" button itself calls,
        // without the player needing to click it for something they just built.
        constexpr const char* BARBIE_SPAWN_DONE_PATH = "ue4ss/Mods/LivingBase/barbie_spawn_done.txt";

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
            // Fixed at 3 columns (8 rows for the 24 colors) rather than auto-fit-to-width
            // (2026-09-08, RedFalcon's request) -- was `pickAvail / (kPickW + spacing)`, which
            // varied with the window's own current size.
            constexpr int perRow = 3;
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

        // ==== "Body" section: Physique + Make Ghost (2026-09-12) ====
        // Physique dropdown MOVED here from BarbieMenu.cpp (RedFalcon: "where selected target used
        // to be add a 'Body' section with Physique in it") -- unchanged logic, see its own original
        // comment (now here): Toned/Cut/Soft are purely this UI's own display labels; the Lua side
        // (Spawner.TestSetSkinSize, lbtestskinsize) has always spoken Small/Medium/Large and stays
        // that way -- this file is the only place the renamed labels live.
        constexpr const char* PHYSIQUE_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_physique_request.txt";
        struct PhysiqueOption { const char* label; const char* lua_size; };
        constexpr PhysiqueOption kPhysiqueOptions[] = {
            { "Boney", "Small" },
            { "Cut",   "Medium" },
            { "Soft",  "Large" },
        };
        int g_physique_index = -1; // -1 = nothing picked yet this session
        auto WritePhysiqueRequest(const char* lua_size) -> void
        {
            std::ofstream f(PHYSIQUE_REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_physique_request.txt\n"));
                return;
            }
            f << lua_size << "\n";
        }

        // "Height" slider (2026-09-18, RedFalcon's height-slider idea, 3ft-8ft range) -- same
        // request-file bridge shape as Physique just above; see
        // Spawner.ApplyActorHeightFeet/CS.setActorScaleGrounded (spawner.lua) for the actual
        // scale-conversion + ground-compensation math.
        constexpr const char* HEIGHT_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_height_request.txt";
        auto WriteHeightRequest(float feet) -> void
        {
            std::ofstream f(HEIGHT_REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_height_request.txt\n"));
                return;
            }
            f << feet << "\n";
        }

        // "Make Ghost" (2026-09-12, RedFalcon: "to the right of physique put a 'Make Ghost' button
        // and call out it's not reversable") -- a single trigger, no picked value at all (unlike
        // every other request file in this bridge) -- writing anything into the file is the whole
        // signal; main.lua's own poll just checks for the file's existence and calls
        // Spawner.MakeGhost(). Genuinely not reversible with a button click of its own: MakeGhost
        // permanently swaps real mesh/material references on the target (skin/hair/armor materials
        // AND the Torso/Legs/Feet/Hands/Headgear skeleton-mesh swap) -- there is no "Undo Ghost"
        // mechanism, matching the same one-way nature Spawner.MakeGhost's own header documents.
        constexpr const char* MAKE_GHOST_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_makeghost_request.txt";
        auto WriteMakeGhostRequest() -> void
        {
            std::ofstream f(MAKE_GHOST_REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_makeghost_request.txt\n"));
                return;
            }
            f << "GHOST\n";
        }

        // "Eye Color" (2026-09-13, RedFalcon: "can we put 'Eye Color' underneath Physique") -- a
        // single swatch picker, no dropdown (unlike Hair, there's no separate "style" axis here,
        // just which of the 9 colors -- see kEyeColors' own header comment further down for the
        // real 8-decoded-plus-1-stylized breakdown). Immediate-apply, same convention as Physique.
        // g_eyeColorIdx is declared here (rather than next to kEyeColors itself) purely so it sits
        // alongside Physique's own g_physique_index -- both are simple Body-section picker state.
        constexpr const char* EYE_COLOR_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_eye_request.txt";
        int g_eyeColorIdx = -1; // -1 = nothing picked yet this session; see kEyeColorGlowingIdx for "Glowing"
        int g_eyeColorLastWritten = -2; // same "write exactly once per change" sentinel as HairRowState's own lastWrittenColor
        // Discrete lbtesteye MATERIAL variants (2026-09-14) -- declared here, ahead of kEyeColors
        // itself further down, purely because WriteEyeColorRequest (right below) needs them and
        // sits above kEyeColors in the file. See kEyeColors' own header comment for the full story.
        constexpr int kEyeColorMaterialStartIdx = 9;
        constexpr int kEyeColorMaterialCount = 4;
        constexpr const char* kEyeColorMaterialAssetNames[kEyeColorMaterialCount] = { "Blue", "Brown", "Green", "Grey" };
        auto WriteEyeColorRequest(int idx, int glowingIdx) -> void
        {
            std::ofstream f(EYE_COLOR_REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_eye_request.txt\n"));
                return;
            }
            if (idx == glowingIdx)
            {
                f << "GLOWING\n";
            }
            else if (idx >= kEyeColorMaterialStartIdx && idx < kEyeColorMaterialStartIdx + kEyeColorMaterialCount)
            {
                f << "MAT:" << kEyeColorMaterialAssetNames[idx - kEyeColorMaterialStartIdx] << "\n";
            }
            else
            {
                f << idx << "\n";
            }
        }

        // ==== "Hair" section (2026-09-12) -- driven by RedFalcon's own Other/HairCategories.xlsx ====
        // Friendly-name lists below are a direct, alphabetized dump of that spreadsheet (see
        // config.lua's own Config.HAIR_CATEGORY_ITEMS for the full row data incl. real mesh paths --
        // this DLL only ever needs the display strings + which one is picked, main.lua does all the
        // actual path/component resolution). KEEP IN SYNC BY HAND with config.lua if the spreadsheet
        // is ever revised -- same situation as kClothColors above, this DLL can't read config.lua at
        // build time.
        // No reserved "(Remove)" sentinel at index 0 (2026-09-14, added then REMOVED same day --
        // RedFalcon: "add remove to each of the hair and facial hair slots", then later "not a huge
        // fan of the remove inside the dropdowns... put a red X button next to the swatches
        // instead") -- DrawHairRow's own RemoveXButton fires that same "(Remove)" wire string now,
        // never a dropdown entry. Deliberately not offered on kHairSetFriendlyNames -- "Sets" is a
        // multi-slot convenience applier, not a real slot of its own (same reasoning it never gets
        // a Read Current line).
        constexpr const char* kHairFriendlyNames[] = {
            "Afro 1", "Afro 2", "Afro 3", "Afro 4", "Afro 5", "Braid", "Bristle", "Bun 1", "Bun 2",
            "Layered Bob", "Layered Bob Decorated", "Mohawk", "Partial Dredlocks", "Pixie 1",
            "Pixie 2", "Pixie 3", "Ponytail", "Shag 1", "Shag 2", "Shag 3", "Shag 4", "Shag 5",
            "Shag 6", "Short Bob", "Slicked Back", "Undercut", "Unique - Galen", "Unique - John",
            "Wavy 1", "Wavy 2", "Wavy 3", "Wavy 4", "Wavy 5", "Wig 1", "Wig 2",
        };
        constexpr const char* kBeardFriendlyNames[] = {
            "Bristle", "Half Ponytail", "Hungover", "Jag 1", "Jag 2", "Nordic", "Royal Marine",
            "Shag 2", "Shag 3", "Shag 4", "Shag 5", "Sparse 1", "Sparse 2", "Sparse 3",
            "Unique - Galen", "Unique - John",
        };
        constexpr const char* kMustacheFriendlyNames[] = {
            "Bristle", "Half Ponytail", "Hungover", "Jag 1", "Jag 2", "Nordic", "Royal Marine",
            "Shag 2", "Shag 3", "Shag 4", "Shag 5", "Sparse 1", "Sparse 2", "Sparse 3",
            "Unique - Galen", "Unique - John",
        };
        constexpr const char* kWhiskersFriendlyNames[] = {
            "Bristle", "Half Ponytail", "Hungover", "Jag 1", "Jag 2", "Nordic", "Royal Marine",
            "Shag 2", "Shag 3", "Shag 4", "Shag 5", "Sparse 1", "Sparse 2", "Sparse 3",
            "Unique - Galen", "Unique - John",
        };
        constexpr const char* kHairSetFriendlyNames[] = {
            "Bristle", "Half Ponytail", "Hungover", "Jag 1", "Jag 2", "Nordic", "Royal Marine",
            "Shag 2", "Shag 3", "Shag 4", "Shag 5", "Sparse 1", "Sparse 2", "Unique - Galen",
            "Unique - John",
        };
        // Eyebrows (2026-09-14, RedFalcon: "add an eyebrows dropdown ... next to Hair, matching
        // mustache and beard below so it's symmetric") -- hand-ported from Config.CUSTOM_FACIAL's
        // own family="Eyebrows"/"BlackSmith" rows (see config.lua's Config.HAIR_CATEGORY_ITEMS
        // Eyebrows block for the full sourcing note), NOT from HairCategories.xlsx like every other
        // list here -- KEEP IN SYNC BY HAND with that config.lua block if it's ever revised.
        constexpr const char* kEyebrowsFriendlyNames[] = {
            "Blacksmith", "Style 1", "Style 2", "Style 3", "Style 4", "Style 5",
        };

        // The 9-entry hair CPD palette (Config.CPD_HAIR_COLOR_NAMES) -- REAL decoded root->tip
        // values (not placeholders), from the same binary-decode technique as this project's other
        // CRV_HairColor_00..08 work (see project_livingbase_cpd_colors memory), already published
        // as the "Hair Color Palette" artifact 2026-09-06. Each entry is a genuine 2-stop gradient
        // (root at the scalp, tip at the end of the strand), same reasoning as kClothColors' own
        // 3-stop gradient above -- values here are already gamma-corrected sRGB floats (0..1),
        // matching the artifact's own hex swatches 1:1 (index order matches
        // Config.CPD_HAIR_COLOR_NAMES exactly). KEEP IN SYNC BY HAND with config.lua if that list is
        // ever revised.
        struct HairColor { const char* name; float root[3]; float tip[3]; };
        constexpr HairColor kHairColors[9] = {
            {"Ash Brown",       {0.1882f, 0.1882f, 0.1882f}, {0.4706f, 0.4039f, 0.4039f}},
            {"Charcoal",        {0.0784f, 0.0902f, 0.1490f}, {0.2941f, 0.3059f, 0.3765f}},
            {"Light Brown",     {0.1765f, 0.0667f, 0.0549f}, {0.8235f, 0.5529f, 0.4392f}},
            {"Blond",           {0.6588f, 0.5647f, 0.3373f}, {1.0000f, 0.8549f, 0.6039f}},
            {"Copper",          {0.0000f, 0.0000f, 0.0000f}, {0.9569f, 0.4157f, 0.1451f}},
            {"Chocolate",       {0.2235f, 0.0863f, 0.0196f}, {0.1961f, 0.0745f, 0.0667f}},
            {"Slate",           {0.0000f, 0.0000f, 0.0000f}, {0.5490f, 0.5098f, 0.5294f}},
            {"Silver",          {0.4784f, 0.4902f, 0.5176f}, {0.9961f, 0.9961f, 0.9961f}},
            {"Salt and Pepper", {0.2039f, 0.2078f, 0.2196f}, {0.6863f, 0.6863f, 0.6863f}},
        };
        constexpr int kHairColorCount = 9;

        // Root->tip 2-stop gradient swatch button -- same drawing approach as GradientSwatchButton
        // above (kClothColors' 3-stop version), just one AddRectFilledMultiColor span since this
        // shape only has 2 real keyframes (root/tip) instead of 3. Generalized (2026-09-13) to take
        // an explicit palette pointer/count instead of being hardcoded to kHairColors -- now shared
        // by both the Hair section's own per-category swatches AND the new Eye Color swatch below,
        // which needs the exact same root->tip rendering over a DIFFERENT array.
        auto FlatSwatchButton(const char* imguiId, int paletteIdx, ImVec2 size, const HairColor* palette, int paletteCount) -> bool
        {
            ImGui::PushID(imguiId);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 botRight = ImVec2(pos.x + size.x, pos.y + size.y);
            if (paletteIdx >= 0 && paletteIdx < paletteCount)
            {
                const HairColor& c = palette[paletteIdx];
                auto toU32 = [](const float rgb[3]) {
                    return ImGui::ColorConvertFloat4ToU32(ImVec4(rgb[0], rgb[1], rgb[2], 1.0f));
                };
                ImU32 c0 = toU32(c.root);
                ImU32 c1 = toU32(c.tip);
                dl->AddRectFilledMultiColor(pos, botRight, c0, c1, c1, c0);
            }
            else
            {
                dl->AddRectFilled(pos, botRight, IM_COL32(58, 54, 48, 255));
                dl->AddLine(pos, botRight, IM_COL32(90, 84, 74, 200), 1.0f);
            }
            dl->AddRect(pos, botRight, IM_COL32(0, 0, 0, 130));
            bool clicked = ImGui::InvisibleButton("##flatswatch", size);
            ImGui::PopID();
            return clicked;
        }

        // Also generalized alongside FlatSwatchButton -- one shared picker-popup shape for any
        // root/tip palette (Hair colors today, Eye colors below).
        auto DrawFlatColorPickerPopup(const char* popupId, int& outIdx, const HairColor* palette, int paletteCount) -> void
        {
            if (!ImGui::BeginPopup(popupId))
            {
                return;
            }
            constexpr float kPickW = 96.0f;
            constexpr float kPickH = 26.0f;
            constexpr int perRow = 3;
            for (int p = 0; p < paletteCount; ++p)
            {
                if (p % perRow != 0)
                {
                    ImGui::SameLine();
                }
                ImGui::PushID(p);
                if (FlatSwatchButton("pick_flatswatch", p, ImVec2(kPickW, kPickH), palette, paletteCount))
                {
                    outIdx = p;
                    ImGui::CloseCurrentPopup();
                }
                HoverTooltip(palette[p].name);
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }

        // Small red "X" button (2026-09-14) -- replaces the old in-dropdown "(Remove)"/"(Remove
        // All)" sentinel entries entirely (RedFalcon: "I'm not a huge fan of the remove inside the
        // dropdowns. Can we instead put a red X button next to the swatches"). Square, sized to
        // match a row's own combo/swatch height so it lines up visually. A "dumb" widget like
        // FlatSwatchButton/GradientSwatchButton above -- just draws + reports the click; the caller
        // fires the actual remove request (still the SAME "(Remove)"/"(Remove All)" wire string
        // main.lua's own pollCustom*Request functions already special-case, just sent from a button
        // now instead of a dropdown pick) and resets its own selection state.
        auto RemoveXButton(const char* imguiId) -> bool
        {
            ImGui::PushID(imguiId);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.10f, 0.10f, 1.0f));
            const float h = ImGui::GetFrameHeight();
            const bool clicked = ImGui::Button("X", ImVec2(h, h));
            ImGui::PopStyleColor(3);
            ImGui::PopID();
            return clicked;
        }

        // The 9-entry Eye Color picker (2026-09-13, RedFalcon: "put 'Eye Color' underneath
        // Physique and use the gradient of the eye colors") -- REAL decoded root->tip values for
        // the first 8, from the already-published "Eye Color Palette" artifact (see
        // project_livingbase_cpd_colors memory) -- same CRV_EyeColor_00..07 decode pipeline as the
        // hair palette, index-matched to Config.CPD_EYE_COLOR_NAMES. Index 8, "Glowing", is NOT a
        // real decoded palette entry -- it's the separate emissive MI_EyeRound_Evil_01 material
        // swap (Spawner.TestSetEyeColor("Glowing", ...), no CPD write involved at all) -- its
        // swatch is a plain dark-grey-to-white gradient per RedFalcon's own explicit request ("for
        // glowing go from a darker grey to white"), a stylized stand-in since there's no real
        // gradient asset backing it, not decoded data.
        // Indices 9-12 (2026-09-14, alongside EYE_COLOR_VARIANTS' own restoration in spawner.lua --
        // RedFalcon: "some NPCs do use the non cpd colors for their eyes") -- the 4 discrete
        // lbtesteye MATERIAL variants (Blue/Brown/Green/Grey), restored as their own swatches so
        // they're reachable from the GUI too, not just the console. Deliberately reuse the EXACT
        // same root/tip as their CPD counterparts above (index 5/0/3/6) -- these are visually
        // identical colors, just applied via a real material SWAP instead of a CPD15 write, for the
        // NPCs whose eyes are natively one of these 4 materials rather than the plain CPD-driven
        // MI_Eye (CPD has nothing to move on them at all). See the eye_color_palette artifact's own
        // "Discrete Material Variants" section for the source comparison.
        constexpr HairColor kEyeColors[13] = {
            {"Brown",      {0.1333f, 0.0353f, 0.0353f}, {0.3647f, 0.2118f, 0.1490f}},
            {"Hazel",      {0.2745f, 0.1843f, 0.1216f}, {0.3569f, 0.4667f, 0.2118f}},
            {"Amber",      {0.2235f, 0.2902f, 0.1451f}, {0.6392f, 0.5804f, 0.2980f}},
            {"Green",      {0.0000f, 0.7059f, 0.4588f}, {0.4157f, 0.5725f, 0.2902f}},
            {"Aquamarine", {0.0000f, 0.3725f, 0.4588f}, {0.0000f, 0.4275f, 0.4824f}},
            {"Blue",       {0.0000f, 0.3725f, 0.7922f}, {0.3843f, 0.7882f, 0.8275f}},
            {"Gray",       {0.3882f, 0.4353f, 0.4510f}, {0.3176f, 0.3176f, 0.3176f}},
            {"Silver",     {0.3804f, 0.4549f, 0.4549f}, {0.7882f, 0.7882f, 0.7882f}},
            {"Glowing",    {0.1800f, 0.1800f, 0.1800f}, {1.0000f, 1.0000f, 1.0000f}},
            {"Blue (Native Material)",  {0.0000f, 0.3725f, 0.7922f}, {0.3843f, 0.7882f, 0.8275f}},
            {"Brown (Native Material)", {0.1333f, 0.0353f, 0.0353f}, {0.3647f, 0.2118f, 0.1490f}},
            {"Green (Native Material)", {0.0000f, 0.7059f, 0.4588f}, {0.4157f, 0.5725f, 0.2902f}},
            {"Grey (Native Material)",  {0.3882f, 0.4353f, 0.4510f}, {0.3176f, 0.3176f, 0.3176f}},
        };
        constexpr int kEyeColorCount = 13;
        constexpr int kEyeColorGlowingIdx = 8;
        // kEyeColorMaterialStartIdx/kEyeColorMaterialCount/kEyeColorMaterialAssetNames are declared
        // earlier, next to WriteEyeColorRequest (which needs them and sits above this array).

        // Belts and Straps friendly-name lists (2026-09-15) -- declared here, well above
        // DrawBeltsAndStraps' own section further down, because pollReadCurrentResult's "BELTPIECE:"
        // parsing needs them and sits between here and there. See DrawBeltsAndStraps' own header
        // comment for the full feature writeup and the rest of this data (kSocketItemFriendlyNames,
        // kBeltSocketRows, etc, which nothing above them needs).
        constexpr const char* kBeltNames[] = { "Belt 1", "Belt 2", "Belt 3", "Belt 4" };
        constexpr const char* kSlingNames[] = { "Sling 1", "Sling 3", "Sling 4", "Shaman Necklace" };
        constexpr const char* kStrapNames[] = { "Strap 1", "Strap 3", "Strap 4" };
        constexpr const char* kFrogNames[] = { "Frog 1", "Frog 3", "Frog 4" };
        constexpr const char* kSetNames[] = { "Set 1", "Set 3", "Set 4" };

        // Left/Right Hand item lists (2026-09-21, RedFalcon: "populate the hand dropdowns... 4
        // dropdowns, they all override each other, its just to reduce the list as its a lot of
        // stuff and may eventually grow") -- Other\SocketItems.xlsx's "Tools" tab (Config.
        // SOCKETITEMS_TOOLS, config.lua), split by its own Type column into exactly these 4 groups.
        // Declared here (not down by kSocketItemFriendlyNames etc.) for the SAME forward-declaration
        // reason as kBeltNames just above: pollReadCurrentResult's own "HANDITEM:" parsing needs
        // these and sits between here and DrawPosesAndActions' own section further down. Sorted
        // alphabetically within each list; re-run gen_socketitems_lua.py's own Tools-tab output and
        // re-diff these 4 arrays whenever SocketItems.xlsx's Tools tab changes.
        constexpr const char* kHandWeaponNames[] = {
            "Axe - Copper", "Axe - Corrupted", "Axe - Iron", "Axe - Stone", "Blunderbuss",
            "Blunderbuss - Dragon's Breath", "Blunderbuss - Reliable", "Club", "Club - Boatwain",
            "Club - Corrupted", "Club - Forceful", "Club - Stunning", "Club - Tremor", "Greatsword",
            "Greatsword - Parrying", "Greatsword - Reliable", "Greatsword - Savage",
            "Greatsword - Slicer", "Greatsword - Soul Drinker", "Greatsword - Wicked", "Halberd",
            "Halberd - Chipped", "Halberd - Corrupted", "Halberd - Executioner",
            "Halberd - Reliable", "Large Knife", "Macuahuitl", "Musket", "Musket - Infantry",
            "Musket - Reliable", "Musket - Sniper", "Pistol", "Pistol - Corrupted",
            "Pistol - Drake's Doom", "Pistol - Reliable", "Pistol - Rusty", "Pistol - Shabby",
            "Pistol - Worn", "Rapier", "Rapier - Bleeding", "Rapier - Bleeding Advanced",
            "Rapier - Eviserate", "Rapier - Loyal", "Rapier - Relentless", "Rapier - Reliable",
            "Rapier - Swift", "Saber - Baneful", "Saber - Boarding", "Saber - Broken",
            "Saber - Corrupted", "Saber - Feral", "Saber - Filigree", "Saber - Fish",
            "Saber - Flawless", "Saber - Forged", "Saber - Graceful", "Saber - No Filigree",
            "Saber - Parrying", "Saber - Relentless", "Saber - Reliable", "Saber - Resolute",
            "Saber - Restless", "Saber - Rusty", "Saber - Savage", "Saber - Severe",
            "Saber - Thorn", "Saber - Vengeful", "Spear",
        };
        constexpr const char* kHandToolNames[] = {
            "Cleaver", "Fishing Rod", "Hammer - Steel", "Hammer - Wooden", "Hand Saw",
            "Kitchen Knife", "Pickaxe - Copper", "Pickaxe - Corrupted", "Pickaxe - Iron",
            "Pickaxe - Stone", "Shovel",
        };
        constexpr const char* kHandBottleNames[] = {
            "Bulbous Bottle", "Clay Bottle", "Faceted Bottle", "Fire Damage Potion",
            "Flat Bottomed Bottle", "Greater Healing Potion", "Healing Potion",
            "Large Faceted Bottle", "Lesser Healing Potion", "Rum Bottle", "Vial Bottle",
        };
        constexpr const char* kHandOtherNames[] = {
            "Coconut", "Gem Stone", "Shackles",
        };
        // The 4 dropdown rows, in display order -- `label` doubles as both the row's own heading
        // AND the combo's default-closed text (RedFalcon: "the name of the category is the default
        // when nothing is selected"), matching DrawBeltAccessoryCombo's own `defaultLabel` param.
        struct HandCategoryRow { const char* label; const char* const* names; int nameCount; };
        constexpr HandCategoryRow kHandCategoryRows[] = {
            { "Weapons", kHandWeaponNames, static_cast<int>(std::size(kHandWeaponNames)) },
            { "Tools",   kHandToolNames,   static_cast<int>(std::size(kHandToolNames)) },
            { "Bottles", kHandBottleNames, static_cast<int>(std::size(kHandBottleNames)) },
            { "Other",   kHandOtherNames,  static_cast<int>(std::size(kHandOtherNames)) },
        };
        constexpr int kHandCategoryCount = static_cast<int>(std::size(kHandCategoryRows));

        // "Belt and Straps Location Guide" reference image (2026-09-16, RedFalcon supplied the real
        // BeltandStrapsGuide.png) -- same relative-path convention BarbieMenu.cpp's SWATCH_DIR uses
        // (relative to the game's own working directory, ships alongside the DLL under this mod's
        // own folder, not baked into the exe).
        constexpr const char* kBeltStrapGuideImagePath = "ue4ss/Mods/LivingBaseSpawnMenu/swatches/BeltsAndStraps/BeltAndStrapsGuide.png";

        // The 11-entry Skin Tone swatch selector (2026-09-14, RedFalcon: "put in place of where
        // Physique was a Skin Tone Swatch Selector... I want the gradients and swatches in this
        // order") -- unlike kHairColors/kEyeColors/kClothColors above, these hex values are
        // RedFalcon's OWN direct picks for this picker, not decoded from an in-game gradient asset
        // -- reusing FlatSwatchButton/DrawFlatColorPickerPopup's shared HairColor-struct rendering
        // purely for visual consistency with the rest of this section. Covers the 7 human ethnicity
        // families config.lua's own Config.SKIN_FAMILIES already uses (Adventurer/African/Albion/
        // Fable/Native/Orient/Scum) plus 4 special cases Spawner.ApplyCustomTabSkinTone/
        // Config.{Israel,Corrupted,Drowned,Ghoul}SkinSwapRules handle on the Lua side: Israel (Male
        // gets the real boss material, Female substitutes Albion -- no Female Israel asset exists),
        // Corrupted (the Senkamati mob's own native skin, reused on a human body), Drowned (a single
        // Male-named asset applied to either sex), and Ghoul (a single asset with no Small/Medium/
        // Large variant at all -- Physique has nothing to act on once Ghoul is picked, by design).
        // KEEP IN SYNC BY HAND with Config.CUSTOM_TAB_SKIN_TONES if that list is ever revised -- this
        // DLL can't read config.lua at build time, same situation as every other palette here.
        constexpr HairColor kSkinTones[11] = {
            {"Adventurer", {0.7569f, 0.5176f, 0.4000f}, {0.7961f, 0.5961f, 0.4235f}},
            {"African",    {0.2588f, 0.1686f, 0.1412f}, {0.4314f, 0.3098f, 0.2275f}},
            {"Albion",     {0.6627f, 0.4745f, 0.4000f}, {0.8275f, 0.6824f, 0.5804f}},
            {"Fable",      {0.7333f, 0.5294f, 0.3882f}, {0.7882f, 0.5569f, 0.4039f}},
            {"Native",     {0.6588f, 0.4157f, 0.2824f}, {0.7294f, 0.5255f, 0.3725f}},
            {"Orient",     {0.7216f, 0.5451f, 0.3922f}, {0.8000f, 0.6667f, 0.4510f}},
            {"Scum",       {0.6667f, 0.4784f, 0.3843f}, {0.8039f, 0.6196f, 0.4627f}},
            {"Israel",     {0.6745f, 0.5686f, 0.4706f}, {0.8314f, 0.6863f, 0.5843f}},
            {"Corrupted",  {0.2039f, 0.1882f, 0.1686f}, {0.4196f, 0.3529f, 0.3098f}},
            {"Drowned",    {0.3255f, 0.3176f, 0.3020f}, {0.3608f, 0.2824f, 0.2431f}},
            {"Ghoul",      {0.4824f, 0.4980f, 0.4549f}, {0.6196f, 0.5373f, 0.4471f}},
        };
        constexpr int kSkinToneCount = 11;

        constexpr const char* SKIN_TONE_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_skintone_request.txt";
        int g_skinToneIdx = -1;         // -1 = nothing picked -- native/body-mesh-default skin
        int g_skinToneLastWritten = -2; // same "write exactly once per change" sentinel as HairRowState's own lastWrittenColor
        auto WriteSkinToneRequest(const char* name) -> void
        {
            std::ofstream f(SKIN_TONE_REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_skintone_request.txt\n"));
                return;
            }
            f << "SKINTONE:" << name << "\n";
        }

        constexpr const char* HAIR_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_hair_request.txt";
        auto WriteHairMeshRequest(const char* categoryKey, const char* friendlyName) -> void
        {
            std::ofstream f(HAIR_REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_hair_request.txt\n"));
                return;
            }
            f << "HAIR:" << categoryKey << ":" << friendlyName << "\n";
        }

        constexpr const char* HAIR_COLOR_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_hair_color_request.txt";
        auto WriteHairColorRequest(const char* categoryKey, int paletteIdx) -> void
        {
            std::ofstream f(HAIR_COLOR_REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_hair_color_request.txt\n"));
                return;
            }
            f << "HAIRCOLOR:" << categoryKey << ":" << paletteIdx << "\n";
        }

        // Clothes item/outfit dropdowns (2026-09-14) -- same request-file bridge shape as Hair
        // above. `bodyPartKey` is the row's own `luaBodyPart` (Torso/Legs/Waist/Hands/Feets/
        // Headgear/Cape); `itemName` is sent verbatim, including the reserved "(Remove)"/
        // "(Remove All)" sentinels -- main.lua's pollCustomClothesItemRequest/
        // pollCustomClothesOutfitRequest already special-case those exact strings.
        constexpr const char* CLOTHES_ITEM_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_clothes_item_request.txt";
        auto WriteClothesItemRequest(const char* bodyPartKey, const char* itemName) -> void
        {
            std::ofstream f(CLOTHES_ITEM_REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_clothes_item_request.txt\n"));
                return;
            }
            f << "CLOTHES:" << bodyPartKey << ":" << itemName << "\n";
        }

        constexpr const char* CLOTHES_OUTFIT_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_clothes_outfit_request.txt";
        auto WriteClothesOutfitRequest(const char* setName) -> void
        {
            std::ofstream f(CLOTHES_OUTFIT_REQUEST_PATH, std::ios::trunc);
            if (!f)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_clothes_outfit_request.txt\n"));
                return;
            }
            f << "OUTFIT:" << setName << "\n";
        }

        // Live-mirrors Config.CLOTHES_UNLOCK_ALL (2026-09-14, RedFalcon: "update the dropdowns when
        // that command is run") -- written by Spawner.ToggleClothesUnlock (lbunlockclothes) on every
        // toggle, polled here every frame the same "cheap ifstream, most frames find nothing new"
        // way pollReadCurrentResult already does regardless of pending state. No request/response
        // handshake needed -- this is a plain one-line status file, not a request.
        constexpr const char* CLOTHES_UNLOCK_STATE_PATH = "ue4ss/Mods/LivingBase/clothes_unlock_state.txt";
        auto pollClothesUnlockState() -> void
        {
            std::ifstream f(CLOTHES_UNLOCK_STATE_PATH);
            if (!f)
            {
                return; // not written yet -- keep g_clothesUnlocked at its current (default false) value
            }
            std::string content;
            std::getline(f, content);
            g_clothesUnlocked = !content.empty() && content[0] == '1';
        }

        // One row's worth of dropdown+swatch state (2026-09-12) -- 5 instances below, one per
        // category (Hair/Sets/Mustache/Whiskers/Beard). Persists for the DLL's lifetime, same
        // "sticky across frames/tabs" convention as g_selected[] above.
        struct HairRowState
        {
            const char* categoryKey;      // sent verbatim in request files -- matches
                                           // Config.HAIR_CATEGORY_ITEMS' own bodyPart strings
            const char* label;            // this row's own on-screen label
            const char* const* names;     // this category's own alphabetized friendly-name list
            int nameCount;
            int selectedName = -1;        // index into `names`, -1 = nothing picked
            int selectedColor = -1;       // index into kHairColors, -1 = nothing picked
            int lastWrittenColor = -2;    // last index actually sent via WriteHairColorRequest --
                                           // distinct sentinel from -1 so picking "nothing" (which
                                           // can't actually happen via the popup) would still write
                                           // once; really just here so a color pick writes exactly
                                           // once per change instead of every frame the popup is shut
            // available (2026-09-16, RedFalcon: "let's extend this to the hair, facial hair and
            // belts and straps") -- true only once a Read Current confirms a real component for
            // this category on the current target (Spawner.TestReadHairStyles' own findHairFamilyComponent
            // existence check). Defaults true (NOT false like g_clothesSlotAvailable) because
            // g_setsRow -- a multi-slot convenience applier, not a real body part -- never receives
            // its own HAIRSLOT line and must never grey out; the other 5 rows get explicitly reset
            // to false each Read Current, same "grey out until proven otherwise" convention as
            // everywhere else.
            bool available = true;
        };
        HairRowState g_hairRow      = { "Hairs",    "Hair",     kHairFriendlyNames,    static_cast<int>(std::size(kHairFriendlyNames)) };
        HairRowState g_eyebrowsRow  = { "Eyebrows", "Eyebrows", kEyebrowsFriendlyNames,static_cast<int>(std::size(kEyebrowsFriendlyNames)) };
        HairRowState g_setsRow      = { "Sets",     "Sets",     kHairSetFriendlyNames, static_cast<int>(std::size(kHairSetFriendlyNames)) };
        HairRowState g_mustacheRow  = { "Mustache", "Mustache", kMustacheFriendlyNames,static_cast<int>(std::size(kMustacheFriendlyNames)) };
        HairRowState g_whiskersRow  = { "Whiskers", "Whiskers", kWhiskersFriendlyNames,static_cast<int>(std::size(kWhiskersFriendlyNames)) };
        HairRowState g_beardRow     = { "Beard",    "Beard",    kBeardFriendlyNames,   static_cast<int>(std::size(kBeardFriendlyNames)) };

        // Draws one Hair-section row: label, a dropdown of friendly names, and a color swatch --
        // both immediate-apply (no separate Apply button, matching Physique's own convention).
        // `dropdownWidth` is the SAME for every row (RedFalcon: "make all the drop downs the same
        // width so they line up better") -- SetNextItemWidth already forced that part; what was
        // still crooked is where each dropdown STARTS, since a bare SameLine() places it right
        // after that row's own label text, and "Hair"/"Sets" is shorter than "Whiskers"/"Mustache".
        // `labelColW` fixes that for whichever row starts a fresh line (Hair/Sets/Whiskers): pass a
        // real column width there so every first-of-line dropdown begins at the SAME absolute X
        // (matches the cloth panel's own `ImGui::SameLine(kLabelW)` convention further down this
        // file). Pass 0 for a row that's the SECOND item already sharing a line with another
        // DrawHairRow (Mustache/Beard) -- jumping to an absolute column there would try to move the
        // cursor backward into the first row's own widgets, since SameLine(x) measures x from the
        // WINDOW's left edge, not from wherever the previous item ended.
        auto DrawHairRow(HairRowState& row, float dropdownWidth, float labelColW) -> void
        {
            ImGui::PushID(row.categoryKey);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(row.label);
            if (labelColW > 0.0f)
            {
                ImGui::SameLine(labelColW);
            }
            else
            {
                ImGui::SameLine();
            }

            // Greyed out when this target has no real component for this category at all
            // (2026-09-16, RedFalcon: "let's extend this to the hair, facial hair and belts and
            // straps") -- same BeginDisabled(!available) convention Clothes rows just got, driven
            // here by row.available (Spawner.TestReadHairStyles' own findHairFamilyComponent
            // existence check). Nests fine with the isFemale BeginDisabled the CALL SITE already
            // wraps Beard/Mustache/Whiskers in -- ImGui disables compose, doesn't fight it.
            ImGui::BeginDisabled(!row.available);

            ImGui::SetNextItemWidth(dropdownWidth);
            const char* currentName = (row.selectedName >= 0) ? row.names[row.selectedName] : "(select)";
            if (ImGui::BeginCombo("##name", currentName))
            {
                for (int i = 0; i < row.nameCount; ++i)
                {
                    const bool selected = (row.selectedName == i);
                    if (ImGui::Selectable(row.names[i], selected))
                    {
                        row.selectedName = i;
                        WriteHairMeshRequest(row.categoryKey, row.names[i]);
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (FlatSwatchButton("##row_hairswatch", row.selectedColor, ImVec2(32.0f, ImGui::GetFrameHeight()), kHairColors, kHairColorCount))
            {
                ImGui::OpenPopup("##hair_color_picker");
            }
            if (row.selectedColor >= 0)
            {
                HoverTooltip(kHairColors[row.selectedColor].name);
            }
            else
            {
                HoverTooltip("Click to choose a color");
            }
            DrawFlatColorPickerPopup("##hair_color_picker", row.selectedColor, kHairColors, kHairColorCount);
            if (row.selectedColor >= 0 && row.selectedColor != row.lastWrittenColor)
            {
                row.lastWrittenColor = row.selectedColor;
                WriteHairColorRequest(row.categoryKey, row.selectedColor);
            }
            // Red "X" (2026-09-14) -- replaces the old in-dropdown "(Remove)" entry (Hairs/Eyebrows/
            // Beard/Mustache/Whiskers only, never "Sets" -- same reasoning "Sets" never had one:
            // it's a multi-slot convenience applier, not a real slot of its own). Not offered for
            // "Sets" -- categoryKey check below skips drawing it entirely for that row.
            if (std::strcmp(row.categoryKey, "Sets") != 0)
            {
                ImGui::SameLine();
                if (RemoveXButton("##hair_remove"))
                {
                    row.selectedName = -1;
                    WriteHairMeshRequest(row.categoryKey, "(Remove)");
                }
                HoverTooltip("Remove");
            }
            ImGui::EndDisabled(); // !row.available
            ImGui::PopID();
        }

        // Forward declarations -- both defined further below (requestReadCurrent needs
        // g_readPending/g_readRequestedAt, already declared above this point; writeColorRequest
        // needs kCategories/g_selected, also already declared above). requestReadCurrent is called
        // by DrawClothesItemCombo (to catch the "requested item was sex-blocked, underwear applied
        // instead" substitution -- Spawner.ApplyClothesItem's own real behavior, 2026-09-14 -- so
        // the dropdown reflects what ACTUALLY got equipped rather than what was clicked) and by
        // DrawClothesOutfitRow (one outfit touches several slots at once, every other row needs to
        // catch up to match). writeColorRequest is called by DrawClothesOutfitRow's own 3 "apply to
        // all" swatches (2026-09-14).
        auto requestReadCurrent() -> void;
        auto writeColorRequest() -> void;

        // Draws just the item-name combo for one Clothes category row (2026-09-14) -- no label, no
        // color swatch; the caller (the existing per-category loop in Draw()) already draws the
        // label and places the swatches, this fills the gap between them. `categoryIdx` indexes
        // kCategories directly. Locked items (Only-with-Unlock) are skipped from the list entirely
        // while g_clothesUnlocked is false (RedFalcon: "hidden until unlocked") -- their index into
        // kCategories[categoryIdx].items still stays valid/selectable via g_clothesSelected once
        // toggled on, since this only skips drawing the Selectable, it never renumbers the array.
        //
        // Senkamati filtering (2026-09-14, widened same day -- RedFalcon: "When a senkamati is
        // selected I want senkamati items in all categories available"). Two independent rules:
        // (1) AVAILABILITY, every category: a matching-sex Senkamati item auto-bypasses the lock the
        // moment that sex is detected (RedFalcon: "the gender specific senkamati items should be
        // available") -- Feet/Hands/Headgear/Cape/Waist included, not just Torso/Legs.
        // (2) RESTRICTION, Torso/Legs ONLY, FEMALE ONLY (RedFalcon: "Male Senkamati can wear other
        // clothes, its just the females who are limited due to their body shape" -- the Witch/Shaman
        // skeleton's own proportions, "Others slots seem ok"): a detected FEMALE Senkamati body
        // flips JUST those two categories' lists entirely to her own matching items -- regular items
        // and male Senkamati items are both hidden outright there. Every other combination (Male
        // Senkamati on any slot, Female Senkamati on any slot BESIDES Torso/Legs, nothing detected)
        // falls through to ordinary locked-item visibility, unchanged.
        auto DrawClothesItemCombo(int categoryIdx, float dropdownWidth) -> void
        {
            const Category& cat = kCategories[categoryIdx];
            const bool isTorsoOrLegs = (std::strcmp(cat.luaBodyPart, "Torso") == 0) || (std::strcmp(cat.luaBodyPart, "Legs") == 0);
            ImGui::PushID("clothes_item");
            ImGui::SetNextItemWidth(dropdownWidth);
            const char* currentName = (g_clothesSelected[categoryIdx] >= 0)
                ? cat.items[g_clothesSelected[categoryIdx]].name : "Select One";
            if (ImGui::BeginCombo("##clothesitem", currentName))
            {
                for (int i = 0; i < cat.itemCount; ++i)
                {
                    const ClothesItem& item = cat.items[i];
                    const char itemSex = SenkamatiItemSex(item.name);
                    const bool matchesDetectedSex = !g_targetSenkamatiSex.empty()
                        && ((itemSex == 'F' && g_targetSenkamatiSex == "Female") || (itemSex == 'M' && g_targetSenkamatiSex == "Male"));
                    if (isTorsoOrLegs && g_targetSenkamatiSex == "Female")
                    {
                        if (itemSex != 'F')
                        {
                            continue;
                        }
                    }
                    else if (matchesDetectedSex)
                    {
                        // Auto-available on a detected matching-sex Senkamati, any category --
                        // bypasses the lock, doesn't hide anything else (falls straight through to
                        // drawing the Selectable).
                    }
                    else if (item.locked && !g_clothesUnlocked)
                    {
                        continue;
                    }
                    const bool selected = (g_clothesSelected[categoryIdx] == i);
                    if (ImGui::Selectable(item.name, selected))
                    {
                        g_clothesSelected[categoryIdx] = i;
                        WriteClothesItemRequest(cat.luaBodyPart, item.name);
                        requestReadCurrent();
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }

        // Draws the standalone "Outfit" row, top of the Clothes section (2026-09-14, RedFalcon: "an
        // additional 'Outfits' type with no colors" -- deliberately no swatch of its own, unlike
        // every row below it). Selecting a real outfit name just forwards the string verbatim to
        // Lua. Also triggers a Read Current refresh right after (RedFalcon: "when selecting an
        // outfit, let's make sure all the other body type dropdowns are updated to match") -- an
        // outfit can set several slots at once, and re-deriving each one from Lua's own actual
        // resulting state is simpler and more correct than duplicating Config.CLOTHES_OUTFITS'
        // piece list in C++ just to guess locally. main.lua's own poll order was adjusted
        // (pollCustomClothesOutfitRequest runs before pollCustomColorReadRequest in the same tick)
        // so the outfit finishes applying before this read captures the result.
        //
        // 3 extra swatches + a red X (2026-09-14, RedFalcon: "lets add 3 swatches next to outfit and
        // have them apply to all clothing items... put a red X button next to the swatches" --
        // supersedes the old "(Remove All)" dropdown entry, same wire string, just fired from the
        // button instead). These are a genuinely different concept from the "no colors" statement
        // above: NOT a property of any one outfit selection, just a quick "recolor every clothing
        // slot at once" convenience -- index k (0/1/2) maps to Color1/Color2/Color3 exactly like a
        // 3-slot category's own 3 swatches (see kCategories' own header on slotCount/soloSlot).
        // Picking swatch k writes g_selected[i][k] for every 3-slot category, and g_selected[i][0]
        // for every 1-slot (solo) category whose OWN soloSlot == k (Waist/Cape only really represent
        // ONE of the 3 channels each -- same rule Read Current's own per-category parsing already
        // follows) -- categories whose soloSlot != k are left alone, since swatch k has nothing to
        // say about them. Reuses writeColorRequest() directly (it already sends the FULL current
        // g_selected state every time), so no new request-file plumbing is needed.
        auto DrawClothesOutfitRow(float dropdownWidth, float labelColW, float swatchW, float swatchH, float swatchGap) -> void
        {
            ImGui::PushID("clothes_outfit_row");
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Outfit");
            ImGui::SameLine(labelColW);
            ImGui::SetNextItemWidth(dropdownWidth);
            // Disabled for a detected FEMALE Senkamati ONLY (2026-09-14, RedFalcon: "We'll need to
            // disable the outfit dropdown for the senkamati female as trying to set one hard locks
            // the game" -- an outfit applies Torso+Legs+Feet+etc all at once, and something about
            // that combination on her skeleton hangs the game outright, not just a bad clip). The 3
            // "apply to all" swatches and the Remove-All X below stay fully functional (RedFalcon:
            // "The remove outfit X and colors should still work") -- only the dropdown itself (real
            // outfit SELECTION) is gated.
            const bool senkamatiFemaleOutfitLock = (g_targetSenkamatiSex == "Female");
            ImGui::BeginDisabled(senkamatiFemaleOutfitLock);
            const char* currentName = (g_clothesOutfitSelected >= 0) ? kClothesOutfitNames[g_clothesOutfitSelected] : "Select One";
            if (ImGui::BeginCombo("##clothesoutfit", currentName))
            {
                for (int i = 0; i < static_cast<int>(std::size(kClothesOutfitNames)); ++i)
                {
                    const bool selected = (g_clothesOutfitSelected == i);
                    if (ImGui::Selectable(kClothesOutfitNames[i], selected))
                    {
                        g_clothesOutfitSelected = i;
                        WriteClothesOutfitRequest(kClothesOutfitNames[i]);
                        requestReadCurrent();
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled(); // senkamatiFemaleOutfitLock
            if (senkamatiFemaleOutfitLock)
            {
                HoverTooltip("Outfits aren't safe on this body -- picking one can hang the game. Use the individual slot dropdowns instead.");
            }

            for (int k = 0; k < 3; ++k)
            {
                ImGui::SameLine(0.0f, swatchGap);
                ImGui::PushID(k);
                if (GradientSwatchButton("outfit_row_swatch", g_outfitColorSelected[k], ImVec2(swatchW, swatchH)))
                {
                    ImGui::OpenPopup("##color_picker");
                }
                if (g_outfitColorSelected[k] >= 0)
                {
                    HoverTooltip(kClothColors[g_outfitColorSelected[k]].name);
                }
                else
                {
                    HoverTooltip("Click to choose a color -- applies to every clothing slot");
                }
                DrawColorPickerPopup("##color_picker", g_outfitColorSelected[k]);
                if (g_outfitColorSelected[k] >= 0 && g_outfitColorSelected[k] != g_outfitColorLastWritten[k])
                {
                    g_outfitColorLastWritten[k] = g_outfitColorSelected[k];
                    for (int i = 0; i < kCategoryCount; ++i)
                    {
                        const Category& cat = kCategories[i];
                        if (cat.slotCount == 3)
                        {
                            g_selected[i][k] = g_outfitColorSelected[k];
                            g_lastWrittenClothColor[i][k] = g_outfitColorSelected[k];
                        }
                        else if (cat.soloSlot == k)
                        {
                            g_selected[i][0] = g_outfitColorSelected[k];
                            g_lastWrittenClothColor[i][0] = g_outfitColorSelected[k];
                        }
                    }
                    writeColorRequest();
                }
                ImGui::PopID();
            }

            ImGui::SameLine(0.0f, swatchGap);
            if (RemoveXButton("##outfit_remove"))
            {
                g_clothesOutfitSelected = -1;
                g_outfitColorSelected[0] = g_outfitColorSelected[1] = g_outfitColorSelected[2] = -1;
                WriteClothesOutfitRequest("(Remove All)");
                requestReadCurrent();
            }
            HoverTooltip("Remove All");
            ImGui::PopID();
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

            // Reset every dropdown this read is authoritative for to "nothing selected" BEFORE
            // parsing (2026-09-14, RedFalcon: "when a slot has no item, set it back to 'Select
            // One'" -- also fixes a real gap: a HIDDEN-but-not-removed item (Spawner.
            // TestReadClothesStyles/TestReadHairStyles both now treat hidden as "not there") would
            // otherwise leave a stale prior selection showing instead of reverting to blank, since
            // Lua simply never emits a line for a slot with nothing detected. "Sets" and "Outfit"
            // are deliberately NOT reset here -- neither ever receives its own read-back line (Sets
            // isn't a real body part, Outfit is a bulk action, not persistent state), so resetting
            // them on every Read Current would erase a still-valid manual pick for no reason.
            for (int i = 0; i < kCategoryCount; ++i)
            {
                g_clothesSelected[i] = -1;
                g_clothesSlotAvailable[i] = false;
            }
            g_hairRow.selectedName = -1;
            g_eyebrowsRow.selectedName = -1;
            g_beardRow.selectedName = -1;
            g_mustacheRow.selectedName = -1;
            g_whiskersRow.selectedName = -1;
            // available (2026-09-16) -- g_setsRow deliberately excluded, see HairRowState's own
            // header comment (it's a multi-slot convenience applier, never receives a HAIRSLOT line).
            g_hairRow.available = false;
            g_eyebrowsRow.available = false;
            g_beardRow.available = false;
            g_mustacheRow.available = false;
            g_whiskersRow.available = false;
            // Skin Tone (2026-09-14) gets the SAME reset-then-repopulate treatment as Clothes/Hair
            // above (not left alone the way Eye Color/Physique are) -- a target whose current skin
            // material isn't one of the 11 known tones (native family, or simply a DIFFERENT target
            // than whichever this swatch last reflected) must revert to "nothing selected" rather
            // than showing a stale tone left over from a previous target.
            g_skinToneIdx = -1;
            // Senkamati detection (2026-09-14) -- same reset-then-repopulate reasoning as Skin Tone
            // just above: a DIFFERENT target that isn't Senkamati must revert the Torso/Legs
            // dropdowns back to the regular item list, not keep showing the previous target's
            // Senkamati-only one.
            g_targetSenkamatiSex.clear();
            // Belts and Straps (2026-09-15) -- same reset-then-repopulate treatment; a target with
            // nothing visible there (or a different target entirely) must revert to "None"/greyed
            // out, not keep showing a previous target's state.
            g_beltPieceSelected = -1;
            g_slingPieceSelected = -1;
            g_strapPieceSelected = -1;
            g_frogPieceSelected = -1;
            g_beltVisible = false;
            g_slingVisible = false;
            g_strapVisible = false;
            g_frogVisible = false;
            g_beltAvailable = false;
            g_slingAvailable = false;
            g_strapAvailable = false;
            g_frogAvailable = false;
            // Poses and Actions (2026-09-16) -- same reset-then-repopulate treatment. Left/Right
            // Hand (2026-09-21: now 4 categories each, see g_handCategorySelected's own header)
            // revert to "nothing selected" here; a "HANDITEM:<Left|Right>:<friendlyName>" line
            // further down repopulates whichever one is actually detected.
            g_currentPoseName = "Unknown";
            for (int h = 0; h < 2; ++h)
            {
                for (int c = 0; c < 4; ++c) { g_handCategorySelected[h][c] = -1; }
            }
            // Height (2026-09-18) -- always overwritten below (HEIGHT: is always present, same
            // "textbox always shows something" convention as Pose), so this reset is defensive only
            // (a target whose read genuinely fails for some reason shouldn't keep showing a
            // previous target's height).
            g_heightFeet = 5.0f + 10.0f / 12.0f;

            std::string line;
            while (std::getline(f, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (line.empty())
                {
                    continue;
                }

                // "PHYSIQUE:<Small|Medium|Large>" -- 2026-09-12, RedFalcon: "read all currently set
                // properties we have (other than the body type and mesh ones)" -- Physique/skin
                // size is a material swap, not a mesh one, so it's part of this same read.
                if (line.rfind("PHYSIQUE:", 0) == 0)
                {
                    const std::string sizeStr = line.substr(9);
                    for (int i = 0; i < static_cast<int>(std::size(kPhysiqueOptions)); ++i)
                    {
                        if (sizeStr == kPhysiqueOptions[i].lua_size)
                        {
                            g_physique_index = i;
                            break;
                        }
                    }
                    continue;
                }

                // "HAIRSTYLE:<Hairs|Beard|Mustache|Whiskers|Eyebrows>:<friendlyName>" -- 2026-09-12,
                // RedFalcon: "i want read current to check all items including hair styles and
                // colors" -- widened from colors-only. friendlyName is matched against the row's
                // OWN `names` array (case-sensitive, exact -- both sides come from the same
                // Other/HairCategories.xlsx source, see kHairFriendlyNames' own header comment --
                // Eyebrows is the one exception, hand-ported from Config.CUSTOM_FACIAL instead, see
                // kEyebrowsFriendlyNames' own header) to find the dropdown index; no match (a
                // custom/unknown mesh) leaves selectedName alone rather than guessing. No "Sets"
                // line ever arrives, same reasoning as colors.
                if (line.rfind("HAIRSTYLE:", 0) == 0)
                {
                    std::istringstream hs(line.substr(10));
                    std::string catKey, friendlyName;
                    if (std::getline(hs, catKey, ':') && std::getline(hs, friendlyName))
                    {
                        HairRowState* row = nullptr;
                        if (catKey == "Hairs") { row = &g_hairRow; }
                        else if (catKey == "Eyebrows") { row = &g_eyebrowsRow; }
                        else if (catKey == "Beard") { row = &g_beardRow; }
                        else if (catKey == "Mustache") { row = &g_mustacheRow; }
                        else if (catKey == "Whiskers") { row = &g_whiskersRow; }
                        if (row)
                        {
                            for (int i = 0; i < row->nameCount; ++i)
                            {
                                if (friendlyName == row->names[i])
                                {
                                    row->selectedName = i;
                                    break;
                                }
                            }
                        }
                    }
                    continue;
                }

                // "HAIRCOLOR:<Hairs|Beard|Mustache|Whiskers|Eyebrows>:<idx>" -- same read, the
                // hair-family CPD colors. No "Sets" line ever arrives (main.lua's TestReadHairColors
                // doesn't emit one -- Sets isn't a real body part) -- that row's own swatch is left
                // alone.
                if (line.rfind("HAIRCOLOR:", 0) == 0)
                {
                    std::istringstream hs(line.substr(10));
                    std::string catKey, idxStr;
                    if (std::getline(hs, catKey, ':') && std::getline(hs, idxStr, ':'))
                    {
                        HairRowState* row = nullptr;
                        if (catKey == "Hairs") { row = &g_hairRow; }
                        else if (catKey == "Eyebrows") { row = &g_eyebrowsRow; }
                        else if (catKey == "Beard") { row = &g_beardRow; }
                        else if (catKey == "Mustache") { row = &g_mustacheRow; }
                        else if (catKey == "Whiskers") { row = &g_whiskersRow; }
                        if (row)
                        {
                            const int idx = std::atoi(idxStr.c_str());
                            row->selectedColor = idx;
                            // Mark this value as already "sent" so DrawHairRow doesn't immediately
                            // re-fire a write request for a value the target already has.
                            row->lastWrittenColor = idx;
                        }
                    }
                    continue;
                }

                // "EYECOLOR:<GLOWING|idx>" -- 2026-09-13, RedFalcon: "i would like read current to
                // include eyes". "GLOWING" is the material-swap variant (kEyeColorGlowingIdx);
                // otherwise a raw CPD15 palette index (0-7), the SAME numeric space
                // WriteEyeColorRequest sends on write. Marked as already-written so the swatch
                // doesn't immediately re-fire a request for a value the target already has.
                if (line.rfind("EYECOLOR:", 0) == 0)
                {
                    const std::string val = line.substr(9);
                    const int idx = (val == "GLOWING") ? kEyeColorGlowingIdx : std::atoi(val.c_str());
                    if (idx >= 0 && idx < kEyeColorCount)
                    {
                        g_eyeColorIdx = idx;
                        g_eyeColorLastWritten = idx;
                    }
                    continue;
                }

                // "SKINTONE:<name>" -- 2026-09-14, the new Skin Tone swatch selector's own Read
                // Current line (Spawner.TestReadSkinTone). name matched against kSkinTones[].name
                // exactly; no match leaves g_skinToneIdx at -1 (reset just above, same convention as
                // Clothes/Hair) -- either the target's native/un-swapped family, or simply nothing
                // readable.
                if (line.rfind("SKINTONE:", 0) == 0)
                {
                    const std::string toneName = line.substr(9);
                    for (int i = 0; i < kSkinToneCount; ++i)
                    {
                        if (toneName == kSkinTones[i].name)
                        {
                            g_skinToneIdx = i;
                            // Already applied on the target -- don't immediately re-fire a write.
                            g_skinToneLastWritten = i;
                            break;
                        }
                    }
                    continue;
                }

                // "SENKAMATI:<Male|Female>" -- 2026-09-14, RedFalcon: "when a senkamati is selected
                // and detected, the gender specific senkamati items should be available"
                // (Spawner.TestReadSenkamatiSex). No line at all means the target isn't a Senkamati
                // body -- reset just above already cleared g_targetSenkamatiSex for that case.
                if (line.rfind("SENKAMATI:", 0) == 0)
                {
                    g_targetSenkamatiSex = line.substr(10);
                    continue;
                }

                // "BELTVISIBLE:<Belt|Sling|Strap|Frog>:<0|1>" -- 2026-09-15, RedFalcon: "i'd like the
                // belts and straps to be detected like the other stuff" -- drives the Accessories
                // windowshade's own column/row gating (Strap column greyed out with no Strap visible,
                // etc; see DrawBeltsAndStraps).
                if (line.rfind("BELTVISIBLE:", 0) == 0)
                {
                    std::istringstream bv(line.substr(12));
                    std::string pieceType, visStr;
                    if (std::getline(bv, pieceType, ':') && std::getline(bv, visStr))
                    {
                        const bool vis = (visStr == "1");
                        if (pieceType == "Belt") { g_beltVisible = vis; }
                        else if (pieceType == "Sling") { g_slingVisible = vis; }
                        else if (pieceType == "Strap") { g_strapVisible = vis; }
                        else if (pieceType == "Frog") { g_frogVisible = vis; }
                    }
                    continue;
                }

                // "BELTAVAILABLE:<Belt|Sling|Strap|Frog>:<0|1>" (2026-09-16, RedFalcon: "let's extend
                // this to the hair, facial hair and belts and straps") -- distinct from BELTVISIBLE:
                // greys out the piece SELECTOR itself (a target with no such composite entry at all),
                // NOT the same thing as "currently hidden" (BELTVISIBLE), which keeps driving the
                // Accessories grid's own dependency gating unchanged.
                if (line.rfind("BELTAVAILABLE:", 0) == 0)
                {
                    std::istringstream ba(line.substr(14));
                    std::string pieceType, availStr;
                    if (std::getline(ba, pieceType, ':') && std::getline(ba, availStr))
                    {
                        const bool avail = (availStr == "1");
                        if (pieceType == "Belt") { g_beltAvailable = avail; }
                        else if (pieceType == "Sling") { g_slingAvailable = avail; }
                        else if (pieceType == "Strap") { g_strapAvailable = avail; }
                        else if (pieceType == "Frog") { g_frogAvailable = avail; }
                    }
                    continue;
                }

                // "HAIRSLOT:<Hairs|Beard|Mustache|Whiskers|Eyebrows>:<0|1>" (2026-09-16, RedFalcon:
                // "let's extend this to the hair, facial hair and belts and straps") -- greys out
                // that row's own dropdown/swatch/X via HairRowState::available, same "component
                // genuinely doesn't exist, not just hidden" distinction CLOTHESSLOT/BELTAVAILABLE use.
                if (line.rfind("HAIRSLOT:", 0) == 0)
                {
                    std::istringstream hs(line.substr(9));
                    std::string catKey, availStr;
                    if (std::getline(hs, catKey, ':') && std::getline(hs, availStr))
                    {
                        const bool avail = (availStr == "1");
                        HairRowState* row = nullptr;
                        if (catKey == "Hairs") { row = &g_hairRow; }
                        else if (catKey == "Eyebrows") { row = &g_eyebrowsRow; }
                        else if (catKey == "Beard") { row = &g_beardRow; }
                        else if (catKey == "Mustache") { row = &g_mustacheRow; }
                        else if (catKey == "Whiskers") { row = &g_whiskersRow; }
                        if (row) { row->available = avail; }
                    }
                    continue;
                }

                // "BELTPIECE:<Belt|Sling|Strap|Frog>:<friendlyName>" -- the Belt/Sling/Strap/Frog
                // dropdowns' own read-back, same "no match leaves it alone" convention as
                // CLOTHESITEM/HAIRSTYLE (a hidden slot or unrecognized mesh already reset -1 above).
                if (line.rfind("BELTPIECE:", 0) == 0)
                {
                    std::istringstream bp(line.substr(10));
                    std::string pieceType, friendlyName;
                    if (std::getline(bp, pieceType, ':') && std::getline(bp, friendlyName))
                    {
                        auto matchInto = [&](int& selectedIdx, const char* const* names, int nameCount)
                        {
                            for (int i = 0; i < nameCount; ++i)
                            {
                                if (friendlyName == names[i]) { selectedIdx = i; break; }
                            }
                        };
                        if (pieceType == "Belt") { matchInto(g_beltPieceSelected, kBeltNames, static_cast<int>(std::size(kBeltNames))); }
                        else if (pieceType == "Sling") { matchInto(g_slingPieceSelected, kSlingNames, static_cast<int>(std::size(kSlingNames))); }
                        else if (pieceType == "Strap") { matchInto(g_strapPieceSelected, kStrapNames, static_cast<int>(std::size(kStrapNames))); }
                        else if (pieceType == "Frog") { matchInto(g_frogPieceSelected, kFrogNames, static_cast<int>(std::size(kFrogNames))); }
                    }
                    continue;
                }

                // "POSE:<name>" (2026-09-16) -- the Poses and Actions windowshade's current-pose
                // textbox. Always present (Spawner.TestReadCurrentPoseName/the step-8c wrapper in
                // main.lua both always resolve to a string, "Unknown" included), so no reset-then-
                // "if present" dance needed here beyond what the reset block above already did.
                if (line.rfind("POSE:", 0) == 0)
                {
                    g_currentPoseName = line.substr(5);
                    continue;
                }

                // "HANDITEM:<Left|Right>:<friendlyName>" (2026-09-21) -- read-back half of the
                // Left/Right Hand item dropdowns. Same "no match leaves it alone" convention as
                // CLOTHESITEM/BELTPIECE (nothing in the hand, or an item this roster doesn't know
                // about, just leaves the reset-block's -1 in place for every category).
                if (line.rfind("HANDITEM:", 0) == 0)
                {
                    std::istringstream hi(line.substr(9));
                    std::string hand, friendlyName;
                    if (std::getline(hi, hand, ':') && std::getline(hi, friendlyName))
                    {
                        const int h = (hand == "Right") ? 1 : (hand == "Left") ? 0 : -1;
                        if (h >= 0)
                        {
                            for (int c = 0; c < kHandCategoryCount; ++c)
                            {
                                const HandCategoryRow& row = kHandCategoryRows[c];
                                for (int i = 0; i < row.nameCount; ++i)
                                {
                                    if (friendlyName == row.names[i])
                                    {
                                        g_handCategorySelected[h][c] = i;
                                    }
                                }
                            }
                        }
                    }
                    continue;
                }

                // "HEIGHT:<feet>" (2026-09-18, RedFalcon's height-slider idea) -- always present
                // (Spawner.TestReadActorHeightFeet always resolves to a number when a target/scale
                // is readable at all), same convention as Pose just above.
                if (line.rfind("HEIGHT:", 0) == 0)
                {
                    g_heightFeet = std::strtof(line.substr(7).c_str(), nullptr);
                    continue;
                }

                // "AITOGGLE:0|1" / "LANTERN:0|1" (2026-09-21, RedFalcon: "it also doesnt seem to
                // correctly detect current AI or lantern status on detect") -- both now always
                // emitted by main.lua's own Read Current round-trip (see that file's own header on
                // this same fix), same "always present" convention as Pose/Height above. AITOGGLE:1
                // means AI is running (g_aiDisabled=false); LANTERN mirrors the exact bool
                // Spawner.TestReadLanternState returns, matching WriteLanternRequest's own on/off
                // sense (the checkbox reads directly from g_lanternOn just below).
                if (line.rfind("AITOGGLE:", 0) == 0)
                {
                    g_aiDisabled = (line.substr(9) != "1");
                    continue;
                }
                if (line.rfind("LANTERN:", 0) == 0)
                {
                    g_lanternOn = (line.substr(8) == "1");
                    continue;
                }

                // "CLOTHESITEM:<Torso|Legs|Waist|Hands|Feets|Headgear|Cape>:<friendlyName>" --
                // 2026-09-14, RedFalcon: "make it realtime like the other items, and all items
                // detected along with the color" -- clothes COLORS are already covered by the
                // generic COLOR: parsing below, this is the missing item-name half. friendlyName is
                // matched against that row's own `items` array (case-sensitive, exact -- both sides
                // come from the same Hair_And_Clothes_Export.xlsx source) to find the dropdown
                // index; no match (a custom/unknown mesh, or nothing equipped) leaves
                // g_clothesSelected alone rather than guessing. No "Outfit" line ever arrives, same
                // reasoning HAIRSTYLE: never sends one for "Sets".
                if (line.rfind("CLOTHESITEM:", 0) == 0)
                {
                    std::istringstream cs(line.substr(12));
                    std::string partKey, friendlyName;
                    if (std::getline(cs, partKey, ':') && std::getline(cs, friendlyName))
                    {
                        for (int i = 0; i < kCategoryCount; ++i)
                        {
                            if (partKey != kCategories[i].luaBodyPart)
                            {
                                continue;
                            }
                            for (int j = 0; j < kCategories[i].itemCount; ++j)
                            {
                                if (friendlyName == kCategories[i].items[j].name)
                                {
                                    g_clothesSelected[i] = j;
                                    break;
                                }
                            }
                            break;
                        }
                    }
                    continue;
                }

                // "CLOTHESSLOT:<bodyPart>:0|1" (2026-09-16, RedFalcon: "if a target doesnt have a
                // swappable item in the hat slot, Grey out the hat selectors") -- ALWAYS sent for
                // every one of the 8 real slots (unlike CLOTHESITEM, only sent on a catalog match),
                // so a slot with no BuildedCompositeMeshes entry at all on THIS target (unlike a
                // slot that merely holds a hidden/unrecognized mesh) greys out its whole row via
                // g_clothesSlotAvailable rather than just leaving the dropdown on "Select One".
                if (line.rfind("CLOTHESSLOT:", 0) == 0)
                {
                    std::istringstream cs(line.substr(12));
                    std::string partKey, availStr;
                    if (std::getline(cs, partKey, ':') && std::getline(cs, availStr))
                    {
                        for (int i = 0; i < kCategoryCount; ++i)
                        {
                            if (partKey != kCategories[i].luaBodyPart)
                            {
                                continue;
                            }
                            g_clothesSlotAvailable[i] = (availStr == "1");
                            break;
                        }
                    }
                    continue;
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
            // Detect gate satisfied (2026-09-14) -- a Read Current cycle just finished successfully
            // (this is the ONLY place that happens, whether triggered by a manual button click or
            // the barbie-spawn auto-detect in DrawTargetHeader) -- Body/Hair/Clothes editing unlocks
            // for whichever target g_lastDetectTargetId is currently tracking.
            g_hasDetected = true;
        }
    } // namespace

    // Reuses MoveMenu.cpp's own move_request.txt/"ACTION:" wire protocol directly (2026-09-14,
    // RedFalcon: "a + button that recreates pressing num +") rather than inventing a parallel
    // mechanism -- appends the EXACT same "ACTION:TARGET_LOCK" line MoveMenu.cpp's own Numpad+
    // passthrough already sends (pressKey(ImGuiKey_KeypadAdd, "TARGET_LOCK")), so main.lua's
    // existing handleMoveMenuTargetLock (Spawner.ToggleTargetLock) needs no changes at all --
    // append mode, same as MoveMenu.cpp's own queueLine, so this can never clobber a move nudge
    // that main.lua hasn't drained yet.
    constexpr const char* MOVE_REQUEST_PATH_FOR_TARGET_LOCK = "ue4ss/Mods/LivingBase/move_request.txt";
    auto WriteTargetLockToggleAction() -> void
    {
        std::ofstream f(MOVE_REQUEST_PATH_FOR_TARGET_LOCK, std::ios::app);
        if (!f)
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write move_request.txt\n"));
            return;
        }
        f << "ACTION:TARGET_LOCK\n";
    }

    // ============================================================================================
    // "Belts and Straps" section, bottom of the Custom tab (2026-09-15). Driven by RedFalcon's own
    // Other/SocketItems.xlsx (same source gen_socketitems_lua.py already turns into config.lua's
    // Config.BELTSTRAPS_PIECES/SOCKETITEMS_* tables) -- the friendly-name lists and the per-socket
    // friendly-name -> real-socket mapping below are a hand-synced C++ mirror of that same data,
    // same convention as kHairFriendlyNames/kEyeColors/etc above. If RedFalcon edits the spreadsheet
    // again, re-run gen_socketitems_lua.py and re-sync these arrays by hand -- there's no automatic
    // link between the two.
    //
    // Belt/Sling/Strap/Frog are fully INDEPENDENT (2026-09-15, RedFalcon: "let's also let the belt
    // and strap choices work independent of each other. no forcing belts with straps, etc. Just
    // basic replacement") -- no cross-requirement like the older lbtestbeltroll rule. "Set" is a
    // separate convenience dropdown that applies Belt N + Sling N + Strap N together; Frog has no
    // Set concept at all, matching the spreadsheet's own "Belts and Straps" tab.
    //
    // kBeltNames/kSlingNames/kStrapNames/kFrogNames/kSetNames are declared further UP the file
    // (right after kEyeColors) rather than here -- pollReadCurrentResult's own "BELTPIECE:" parsing
    // needs them and sits well above this point, and C++ namespace-scope names must be declared
    // before use (no hoisting).

    // Every "Items" tab friendly name (2026-09-15) -- RedFalcon: "For Belt, Sling, and strap ones i
    // want all items in the Items tab available, ignoring the randomization limitations" -- so every
    // Belt/Sling/Strap socket row's own dropdown offers this SAME full list, not a per-socket-
    // filtered one. Re-verified whole (not hand-patched) 2026-09-18 against the live-generated
    // Config.SOCKETITEMS_ITEMS, after the lantern-slot scan surfaced 2 more new pistol variants on
    // top of "Holding Strap"/"Holster" (2026-09-17) -- see gen_socketitems_lua.py's own output for
    // the authoritative list; re-run and re-diff this array whenever SocketItems.xlsx changes again
    // rather than hand-adding one-off entries, since a manual patch already missed some once.
    constexpr const char* kSocketItemFriendlyNames[] = {
        "Bag - Belt",
        "Bag - Fancy Coinpurse",
        "Bag - Galen's",
        "Bag - John's",
        "Bag - John's  With Scroll",
        "Bag - Large Belt",
        "Bag - Large Explosives",
        "Bag - Medium Bag 2",
        "Bag - Medium Belt 1",
        "Bag - Simple Belt ",
        "Bag - Small Belt",
        "Bag - With Powder",
        "Bag - Worn",
        "Bone 1",
        "Bone 2",
        "Bones",
        "Bones - Hanging",
        "Bones - Necklace",
        "Book",
        "Canteen",
        "Feather",
        "Feathers",
        "Fish Hook",
        "Fishing Bobber",
        "Gloves",
        "Grenade",
        "Hand Fan",
        "Holding Strap",
        "Holster",
        "Knife - Fancy",
        "Knife - No Sheath",
        "Knife - Primitive Iron",
        "Knife - Primitive Stone",
        "Knife - Sheath",
        "Pipe",
        "Pistol - Baneful - Belt",
        "Pistol - Basic 3",
        "Pistol - Basic 4",
        "Pistol - Basic 5",
        "Pistol - Basic 6",
        "Pistol - Basic 7",
        "Pistol - Belt",
        "Pistol - Devastating - Belt",
        "Pistol - Drake's Doom - Belt",
        "Pistol - Faithful - Belt",
        "Pistol - Rusty - Belt",
        "Pouch - Dark",
        "Pouch - Light",
        "Powder Horn",
        "Skull - Dodo",
        "Skull - Human",
        "Skull - Wolf 1",
        "Skull - Wolf 2",
        "Skull Badge",
        "Skulls - Metal",
        "Vials - Hanging x1",
        "Vials - Hanging x2",
        "Vials - Hanging x3",
        "Vials - Strapped 1",
        "Vials - Strapped 2",
    };

    // "Weapons" tab friendly names, split by Location column (2026-09-15, RedFalcon: "use the
    // Location column to determine the list, and pistol can go in either pistol dropdown") -- Back
    // and Sheath each combine SEVERAL real sockets behind one dropdown (Spawner.ApplyWeaponSlotManual
    // resolves which specific real socket a given weapon actually belongs on); the same Pistol list
    // backs BOTH the Left and Right Pistol dropdowns.
    constexpr const char* kBackWeaponNames[] = {
        "Axe - Basic 1",
        "Axe - Basic 2",
        "Axe - Corrupted",
        "Axe - Stone",
        "Blunderbuss",
        "Blunderbuss - DragonBreath",
        "Blunderbuss - Fateful",
        "Blunderbuss - Reliable",
        "Club - Basic",
        "Fishing Rod",
        "Gaff - Steel",
        "Great Axe - Basic",
        "Greatsword",
        "Greatsword - Parrying",
        "Greatsword - Reliable",
        "Greatsword - Savage",
        "Greatsword - Slicer",
        "Greatsword - Souldrinker",
        "GreatSword - Voved",
        "Greatsword - Wicked",
        "Halberd",
        "Halberd - Basic",
        "Halberd - Corrupted",
        "Halberd - Executioner",
        "Halberd - Reliable",
        "Musket",
        "Musket - Artifact",
        "Musket - Baneful",
        "Musket - Basic 1",
        "Musket - Basic 2",
        "Musket - Basic 3",
        "Musket - Basic 4",
        "Musket - Basic 5",
        "Musket - Infantry",
        "Musket - Relentless",
        "Musket - Reliable",
        "Musket - Sniper",
        "Musket - Wicked",
        "Pickaxe - Basic 1",
        "Pickaxe - Basic 2",
        "Pickaxe - Corrupted",
        "Pickaxe - Stone",
        "Saber - Thorn",
        "Shovel 1",
        "Shovel 2",
        "Spear - Corrupted",
        "Spear - Dendromorph",
        "Spear - Steel",
    };
    constexpr const char* kSheathWeaponNames[] = {
        "Club",
        "Club - Artifact",
        "Club - Corrupted",
        "Club - Forceful",
        "Club - Stunning",
        "Club - Tremor",
        "Knife - Basic",
        "Macuahuitl - Dendromorph",
        "Rapier",
        "Rapier - Advanced - Bleeding",
        "Rapier - Bleeding",
        "Rapier - Eviscerate",
        "Rapier - Loyal",
        "Rapier - Relentless",
        "Rapier - Reliable",
        "Rapier - Swift",
        "Saber",
        "Saber - Baneful",
        "Saber - Basic 1",
        "Saber - Basic 2",
        "Saber - Boarding",
        "Saber - Broken",
        "Saber - Corrupted",
        "Saber - Feral",
        "Saber - Flaweless",
        "Saber - Forged",
        "Saber - Graceful",
        "Saber - Parrying",
        "Saber - Relentless",
        "Saber - Reliable",
        "Saber - Resolute",
        "Saber - Restless",
        "Saber - Savage",
        "Saber - Severe",
        "Saber - Vengeful",
    };
    // Updated 2026-09-17 (RedFalcon added 9 new "Pistol - Basic 3..7"/"Pistol - X - Belt" entries,
    // rarity="-" so they never roll randomly but are fully selectable manually, same "ignore the
    // randomization limitations" rule this whole list already follows) -- also fixes a real
    // pre-existing mismatch: this list said "Pistol - DrakesDoom" but the actual Weapons-tab
    // friendly name has always been "Pistol - Drake's Doom" (apostrophe + space) -- that stale
    // spelling would have silently failed to match on Read Current for that one row.
    // "Pistol - Baneful"/"Pistol - Devastating" added 2026-09-18 -- genuinely pre-existing native
    // catalog entries (real Uncommon/Rare rollable weapons, NOT the manual-only "-Belt" variants)
    // that were simply never added to this hardcoded list at all -- surfaced by the lantern-slot
    // scan finding both meshes natively equipped on real NPCs.
    constexpr const char* kPistolWeaponNames[] = {
        "Pistol",
        "Pistol - Baneful",
        "Pistol - Basic 1",
        "Pistol - Basic 2",
        "Pistol - Basic 3",
        "Pistol - Basic 4",
        "Pistol - Basic 5",
        "Pistol - Basic 6",
        "Pistol - Basic 7",
        "Pistol - Belt",
        "Pistol - Corrupted",
        "Pistol - Devastating",
        "Pistol - Drake's Doom",
        "Pistol - Drake's Doom - Belt",
        "Pistol - Faithful - Belt",
        "Pistol - Reliable",
        "Pistol - Rusty",
        "Pistol - Rusty - Belt",
    };

    // Per-socket friendly-name -> real-socket mapping for the Accessories windowshade (2026-09-15) --
    // hand-copied from Config.SOCKETITEMS_SOCKETS' own generated `friendlyName` field (socType=="soc"
    // rows only; the socket-count-per-column here -- 7/8/9 -- is exactly RedFalcon's own mockup grid).
    // "Lantern Slot" added 2026-09-17 at the TOP of the Belt column (RedFalcon: "insert a Lantern
    // dropdown at the top of the belt column and treat it the same as the others") -- soc_Lantern
    // was previously fully excluded from this whole system (a real native NPC accessory was found
    // sharing it with our own separate Lantern toggle). This row is otherwise IDENTICAL to every
    // other one here -- same combo, same WriteSocketItemRequest/detection path -- the actual
    // coexistence handling (never mistaking or destroying our own lantern mesh when a real item is
    // detected/applied/removed here) lives entirely on the Lua side (CS.LANTERN_KNOWN_MESHES /
    // filterOutLanternMeshes, spawner.lua), so nothing extra is needed here.
    struct BeltSocketRow { const char* friendlyName; const char* socket; };
    constexpr BeltSocketRow kBeltSocketRows[] = {
        { "Lantern Slot", "soc_Lantern" },
        { "Belt 1", "soc_Strap01F" }, { "Belt 2", "soc_beltB02_l" }, { "Belt 3", "soc_beltSlingF" },
        { "Belt 4", "soc_beltSlingB" }, { "Belt 5", "soc_belt02B_l" }, { "Belt 6", "soc_beltB" },
        { "Belt 7", "soc_beltStrapB" },
    };
    constexpr BeltSocketRow kSlingSocketRows[] = {
        { "Sling 1", "soc_Sling04F" }, { "Sling 2", "soc_Sling03F" }, { "Sling 3", "soc_Sling02F" },
        { "Sling 4", "soc_Sling01F" }, { "Sling 5", "soc_Sling04B" }, { "Sling 6", "soc_Sling03B" },
        { "Sling 7", "soc_Sling02B" }, { "Sling 8", "soc_Sling01B" },
    };
    constexpr BeltSocketRow kStrapSocketRows[] = {
        { "Strap 1", "soc_Strap04F" }, { "Strap 2", "soc_Strap03F" }, { "Strap 3", "soc_Strap02F" },
        { "Strap 4", "soc_Strap01F" }, { "Strap 5", "soc_Strap04B" }, { "Strap 6", "soc_Strap03B" },
        { "Strap 7", "soc_Strap02B" }, { "Strap 8", "soc_Strap01B" }, { "Strap 9", "soc_Strap_r" },
    };

    // The 4 combined weapon dropdowns (2026-09-15) -- `locationKey` is the exact wire string
    // Spawner.ApplyWeaponSlotManual's own WEAPON_GROUP_SOCKETS table keys on.
    struct WeaponSlotRow { const char* label; const char* locationKey; const char* const* names; int nameCount; };
    const WeaponSlotRow kWeaponSlotRows[] = {
        { "Sheath",       "Sheath",     kSheathWeaponNames, static_cast<int>(std::size(kSheathWeaponNames)) },
        { "Back Weapon",  "Back",       kBackWeaponNames,   static_cast<int>(std::size(kBackWeaponNames)) },
        { "Left Pistol",  "LeftPistol", kPistolWeaponNames, static_cast<int>(std::size(kPistolWeaponNames)) },
        { "Right Pistol", "RightPistol",kPistolWeaponNames, static_cast<int>(std::size(kPistolWeaponNames)) },
    };

    // Persistent GUI state, DLL lifetime (same "sticky across frames" convention as g_selected[]
    // etc above). -1 == nothing picked ("None"/default). g_beltPieceSelected/g_slingPieceSelected/
    // g_strapPieceSelected/g_frogPieceSelected and the 4 visibility bools are declared further UP
    // the file (alongside g_hasDetected) -- see that declaration's own comment for why.
    int g_beltSetSelected = -1;
    // g_lanternOn/g_aiDisabled moved further UP the file (2026-09-21, alongside g_currentPoseName/
    // g_heightFeet) -- pollReadCurrentResult's own "AITOGGLE:"/"LANTERN:" parsing needed them
    // declared before its own definition, same reasoning as those other forward-declared blocks.
    int g_beltSocketSelected[std::size(kBeltSocketRows)] = {};
    int g_slingSocketSelected[std::size(kSlingSocketRows)] = {};
    int g_strapSocketSelected[std::size(kStrapSocketRows)] = {};
    int g_weaponSlotSelected[4] = {};

    // "Poses and Actions" section (2026-09-16) -- g_currentPoseName/g_handCategorySelected are
    // declared further UP the file (alongside g_beltPieceSelected etc.), same forward-declaration
    // reasoning: pollReadCurrentResult's own "POSE:"/"HANDITEM:" parsing needs them. kHandCategoryRows
    // (the real item lists) are ALSO declared further up (alongside kBeltNames) for the same reason.

    // ResetCustomViewState() (2026-09-18, RedFalcon: "can we reset the custom view when a target
    // is unselected?") -- every dropdown/swatch/checkbox the Custom tab shows, reverted to its
    // neutral default. Duplicates (rather than shares/refactors) the reset logic
    // pollReadCurrentResult and pollSocketAccStatus already run before parsing their own status
    // files -- those two functions are declared BEFORE some of the fields reset here (no hoisting
    // in C++, same reasoning kBeltNames' own forward-declaration comment already documents), so a
    // single shared helper can't sit above both call sites. Called when the locked target becomes
    // fully unselected (see the target-change tracking block below) -- a genuinely DIFFERENT
    // target (not unselected, just swapped) is already handled correctly by the existing
    // g_hasDetected reset plus whatever the next Read Current naturally overwrites.
    auto ResetCustomViewState() -> void
    {
        for (int i = 0; i < kCategoryCount; ++i)
        {
            g_clothesSelected[i] = -1;
            g_clothesSlotAvailable[i] = false;
        }
        g_hairRow.selectedName = -1;
        g_eyebrowsRow.selectedName = -1;
        g_beardRow.selectedName = -1;
        g_mustacheRow.selectedName = -1;
        g_whiskersRow.selectedName = -1;
        g_hairRow.available = false;
        g_eyebrowsRow.available = false;
        g_beardRow.available = false;
        g_mustacheRow.available = false;
        g_whiskersRow.available = false;
        g_skinToneIdx = -1;
        g_targetSenkamatiSex.clear();
        g_beltPieceSelected = -1;
        g_slingPieceSelected = -1;
        g_strapPieceSelected = -1;
        g_frogPieceSelected = -1;
        g_beltVisible = false;
        g_slingVisible = false;
        g_strapVisible = false;
        g_frogVisible = false;
        g_beltAvailable = false;
        g_slingAvailable = false;
        g_strapAvailable = false;
        g_frogAvailable = false;
        g_currentPoseName = "Unknown";
        for (int h = 0; h < 2; ++h)
        {
            for (int c = 0; c < 4; ++c) { g_handCategorySelected[h][c] = -1; }
        }
        g_beltSetSelected = -1;
        g_lanternOn = false;
        g_aiDisabled = false;
        for (auto& v : g_beltSocketSelected) { v = -1; }
        for (auto& v : g_slingSocketSelected) { v = -1; }
        for (auto& v : g_strapSocketSelected) { v = -1; }
        for (auto& v : g_weaponSlotSelected) { v = -1; }
        g_heightFeet = 5.0f + 10.0f / 12.0f;
    }

    constexpr const char* BELTPIECE_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_beltpiece_request.txt";
    auto WriteBeltPieceRequest(const char* pieceType, const char* friendlyName) -> void
    {
        std::ofstream f(BELTPIECE_REQUEST_PATH, std::ios::trunc);
        if (!f)
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_beltpiece_request.txt\n"));
            return;
        }
        f << "BELTPIECE:" << pieceType << ":" << friendlyName << "\n";
    }

    constexpr const char* BELTSET_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_beltset_request.txt";
    auto WriteBeltSetRequest(const char* setName) -> void
    {
        std::ofstream f(BELTSET_REQUEST_PATH, std::ios::trunc);
        if (!f)
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_beltset_request.txt\n"));
            return;
        }
        f << "BELTSET:" << setName << "\n";
    }

    constexpr const char* LANTERN_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_lantern_request.txt";
    auto WriteLanternRequest(bool on) -> void
    {
        std::ofstream f(LANTERN_REQUEST_PATH, std::ios::trunc);
        if (!f)
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_lantern_request.txt\n"));
            return;
        }
        f << "LANTERN:" << (on ? "1" : "0") << "\n";
    }

    // "Toggle AI" (2026-09-16, RedFalcon: "Add a 'Toggle AI' button underneath the 'Make Ghost'
    // warning that sets and disables AI Processing") -- wraps Spawner.SetAILogic (StartLogic/
    // StopLogic on the target's own AIController), an existing mechanism this project already uses
    // elsewhere (crew-follow logic), not a new one.
    constexpr const char* AITOGGLE_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_aitoggle_request.txt";
    auto WriteAIToggleRequest(bool on) -> void
    {
        std::ofstream f(AITOGGLE_REQUEST_PATH, std::ios::trunc);
        if (!f)
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_aitoggle_request.txt\n"));
            return;
        }
        f << "AITOGGLE:" << (on ? "1" : "0") << "\n";
    }

    // "Save Customizations" (2026-09-16, RedFalcon: "how do we tie [Custom tab edits] into the
    // persist file to regenerate the changes... they can fiddle all they want, but it isn't
    // persisted until saved"). Fire-and-forget, no payload needed -- the file's mere existence IS
    // the request (same convention as custom_zoom_request.txt), and the target is whatever's
    // currently Spawner.lockedTarget, same as every other Custom tab action. main.lua's own
    // Spawner.SaveCustomState does the actual identity lookup + file write into a SEPARATE
    // custom_state_<islandId>.txt (deliberately not persist.txt itself).
    constexpr const char* SAVE_CUSTOMIZATIONS_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_save_request.txt";
    auto WriteSaveCustomizationsRequest() -> void
    {
        std::ofstream f(SAVE_CUSTOMIZATIONS_REQUEST_PATH, std::ios::trunc);
        if (!f)
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_save_request.txt\n"));
            return;
        }
        f << "SAVE\n";
    }

    constexpr const char* SOCKETACC_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_socketacc_request.txt";
    auto WriteSocketAccRequest(const char* action) -> void
    {
        std::ofstream f(SOCKETACC_REQUEST_PATH, std::ios::trunc);
        if (!f)
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_socketacc_request.txt\n"));
            return;
        }
        f << action << "\n";
    }

    constexpr const char* SOCKETITEM_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_socketitem_request.txt";
    auto WriteSocketItemRequest(const char* socket, const char* friendlyName) -> void
    {
        std::ofstream f(SOCKETITEM_REQUEST_PATH, std::ios::trunc);
        if (!f)
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_socketitem_request.txt\n"));
            return;
        }
        f << "SOCKETITEM:" << socket << ":" << friendlyName << "\n";
    }

    // Pose scrub Play/Pause/Step/Seek (2026-09-21, RedFalcon's frame-by-frame pose scrubber
    // follow-up) -- same fire-and-forget/single-payload request shapes as everything above. None
    // of these carry a target -- Spawner.PoseScrubPlay/Pause/Step/Seek all act on whatever
    // Spawner.poseScrub is already tracking (armed the instant a pose is picked from the tree, see
    // main.lua's CUSTOM_POSES handler).
    constexpr const char* POSE_SCRUB_PLAY_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_pose_scrub_play_request.txt";
    auto WritePoseScrubPlayRequest() -> void
    {
        std::ofstream f(POSE_SCRUB_PLAY_REQUEST_PATH, std::ios::trunc);
        if (!f)
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_pose_scrub_play_request.txt\n"));
            return;
        }
        f << "PLAY\n";
    }

    constexpr const char* POSE_SCRUB_PAUSE_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_pose_scrub_pause_request.txt";
    auto WritePoseScrubPauseRequest() -> void
    {
        std::ofstream f(POSE_SCRUB_PAUSE_REQUEST_PATH, std::ios::trunc);
        if (!f)
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_pose_scrub_pause_request.txt\n"));
            return;
        }
        f << "PAUSE\n";
    }

    constexpr const char* POSE_SCRUB_STEP_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_pose_scrub_step_request.txt";
    auto WritePoseScrubStepRequest(int delta) -> void
    {
        std::ofstream f(POSE_SCRUB_STEP_REQUEST_PATH, std::ios::trunc);
        if (!f)
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_pose_scrub_step_request.txt\n"));
            return;
        }
        f << delta << "\n";
    }

    constexpr const char* POSE_SCRUB_SEEK_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_pose_scrub_seek_request.txt";
    auto WritePoseScrubSeekRequest(int frame) -> void
    {
        std::ofstream f(POSE_SCRUB_SEEK_REQUEST_PATH, std::ios::trunc);
        if (!f)
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_pose_scrub_seek_request.txt\n"));
            return;
        }
        f << frame << "\n";
    }

    constexpr const char* WEAPONSLOT_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_weaponslot_request.txt";
    auto WriteWeaponSlotRequest(const char* locationKey, const char* friendlyName) -> void
    {
        std::ofstream f(WEAPONSLOT_REQUEST_PATH, std::ios::trunc);
        if (!f)
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CustomMenu: failed to write custom_weaponslot_request.txt\n"));
            return;
        }
        f << "WEAPONSLOT:" << locationKey << ":" << friendlyName << "\n";
    }

    // Draws one plain dropdown for the Belts and Straps section (2026-09-15) -- shared by the Set/
    // Belt/Sling/Strap/Frog rows AND every Accessories windowshade row. `defaultLabel` is shown when
    // `selectedIdx` is -1 (nothing picked) -- for Set/Belt/Sling/Strap/Frog that's the literal
    // "None"; for a per-socket Accessories row it's that socket's OWN friendly name (RedFalcon: "by
    // default it will display the socket's friendly name"). "None" is always offered as the first
    // real entry in the list too, distinct from the closed-dropdown default text, so picking it
    // explicitly resets selectedIdx to -1 and is reported back to the caller as a change. Returns
    // true exactly on the frame the selection changed (caller does the actual request-file write).
    auto DrawBeltAccessoryCombo(const char* imguiId, const char* defaultLabel, int& selectedIdx,
        const char* const* names, int nameCount, float width) -> bool
    {
        ImGui::PushID(imguiId);
        ImGui::SetNextItemWidth(width);
        const char* currentName = (selectedIdx >= 0) ? names[selectedIdx] : defaultLabel;
        bool changed = false;
        if (ImGui::BeginCombo("##combo", currentName))
        {
            const bool noneSelected = (selectedIdx == -1);
            if (ImGui::Selectable("None", noneSelected))
            {
                selectedIdx = -1;
                changed = true;
            }
            if (noneSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
            for (int i = 0; i < nameCount; ++i)
            {
                const bool selected = (selectedIdx == i);
                if (ImGui::Selectable(names[i], selected))
                {
                    selectedIdx = i;
                    changed = true;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        return changed;
    }

    // pollSocketAccStatus() -- (2026-09-16) the read-back half of "Detect Accessories"/Randomize/
    // Clear. Spawner.TestReadSocketAccessories (called by main.lua's BeltStrapPolls.socketAcc after
    // ANY of the 3 actions) writes one "SOCKETACCESSORY:<realSocketName>:<friendlyName>" line per
    // detected item to a plain status file -- no request/response pending-timer dance needed (unlike
    // Read Current), since this is a fire-and-forget "the Lua side will get to it eventually, poll
    // cheaply every frame" status file, same convention pollClothesUnlockState already uses. Every
    // dropdown is reset to -1 FIRST (same "no line = nothing there, revert to placeholder" rule
    // pollReadCurrentResult's own reset already established), then each line is reverse-matched
    // against the real-socket tables (kBeltSocketRows/kSlingSocketRows/kStrapSocketRows) or, for a
    // weapon socket, against a small hardcoded socket->kWeaponSlotRows-index map (mirrors Lua's own
    // WEAPON_GROUP_SOCKETS grouping exactly -- Sheath/Back/LeftPistol/RightPistol).
    constexpr const char* SOCKETACC_STATUS_PATH = "ue4ss/Mods/LivingBase/custom_socketacc_status.txt";
    auto pollSocketAccStatus() -> void
    {
        std::ifstream f(SOCKETACC_STATUS_PATH);
        if (!f)
        {
            return;
        }
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();
        std::remove(SOCKETACC_STATUS_PATH);

        for (auto& v : g_beltSocketSelected) { v = -1; }
        for (auto& v : g_slingSocketSelected) { v = -1; }
        for (auto& v : g_strapSocketSelected) { v = -1; }
        for (auto& v : g_weaponSlotSelected) { v = -1; }

        auto matchFriendly = [](int& outIdx, const char* const* names, int nameCount, const std::string& friendlyName)
        {
            for (int j = 0; j < nameCount; ++j)
            {
                if (friendlyName == names[j]) { outIdx = j; return; }
            }
        };

        std::istringstream stream(content);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r') { line.pop_back(); }
            // LANTERN (2026-09-18, RedFalcon: "the lantern detection doesnt see it when our
            // lantern is already there") -- g_lanternOn was write-only until now, never actually
            // read back from live game state. main.lua's BeltStrapPolls.socketAcc now piggybacks
            // a "LANTERN:0"/"LANTERN:1" line onto this SAME status file (the natural place, since
            // soc_Lantern is this same cycle's own socket now) -- checked BEFORE the
            // SOCKETACCESSORY-only filter below so it isn't skipped.
            if (line.rfind("LANTERN:", 0) == 0)
            {
                g_lanternOn = (line.substr(8) == "1");
                continue;
            }
            if (line.rfind("SOCKETACCESSORY:", 0) != 0) { continue; }
            std::istringstream ls(line.substr(16));
            std::string socket, friendlyName;
            if (!std::getline(ls, socket, ':') || !std::getline(ls, friendlyName)) { continue; }

            bool matched = false;
            for (int i = 0; i < static_cast<int>(std::size(kBeltSocketRows)) && !matched; ++i)
            {
                if (socket == kBeltSocketRows[i].socket)
                {
                    matchFriendly(g_beltSocketSelected[i], kSocketItemFriendlyNames, static_cast<int>(std::size(kSocketItemFriendlyNames)), friendlyName);
                    matched = true;
                }
            }
            for (int i = 0; i < static_cast<int>(std::size(kSlingSocketRows)) && !matched; ++i)
            {
                if (socket == kSlingSocketRows[i].socket)
                {
                    matchFriendly(g_slingSocketSelected[i], kSocketItemFriendlyNames, static_cast<int>(std::size(kSocketItemFriendlyNames)), friendlyName);
                    matched = true;
                }
            }
            for (int i = 0; i < static_cast<int>(std::size(kStrapSocketRows)) && !matched; ++i)
            {
                if (socket == kStrapSocketRows[i].socket)
                {
                    matchFriendly(g_strapSocketSelected[i], kSocketItemFriendlyNames, static_cast<int>(std::size(kSocketItemFriendlyNames)), friendlyName);
                    matched = true;
                }
            }
            if (matched) { continue; }

            int weaponRow = -1;
            if (socket == "swordSlot_lSocket" || socket == "rapierSlot_lSocket") { weaponRow = 0; }
            else if (socket == "Axe1h_backsocket" || socket == "Axe2h_backsocket" || socket == "Crossbow2h_backsocket"
                     || socket == "GSword_backsocket" || socket == "Halberd_backsocket" || socket == "Musket_backsocket") { weaponRow = 1; }
            else if (socket == "beltSlot_01_lSocket") { weaponRow = 2; }
            else if (socket == "beltSlot_01_rSocket") { weaponRow = 3; }
            if (weaponRow >= 0)
            {
                const auto& r = kWeaponSlotRows[weaponRow];
                matchFriendly(g_weaponSlotSelected[weaponRow], r.names, r.nameCount, friendlyName);
            }
        }
    }

    // Draws the whole "Belts and Straps" section (2026-09-15) -- called once, near the bottom of
    // Draw(), inside the SAME target/detect-gated BeginDisabled block the Clothes panel already
    // uses (this feature needs a real live target for exactly the same reasons Clothes does).
    auto DrawBeltsAndStraps() -> void
    {
        pollSocketAccStatus();

        // The int[] state arrays above zero-fill by default (index 0 "picked"), same trap
        // Draw()'s own g_selected fixup already documents -- fix them to -1 ("None"/nothing picked)
        // exactly once, lazily, the same way.
        {
            static bool initialized = false;
            if (!initialized)
            {
                initialized = true;
                for (auto& v : g_beltSocketSelected) v = -1;
                for (auto& v : g_slingSocketSelected) v = -1;
                for (auto& v : g_strapSocketSelected) v = -1;
                for (auto& v : g_weaponSlotSelected) v = -1;
            }
        }

        // No separator/header drawn here (2026-09-16, RedFalcon: "Get rid of the lines between
        // sections... Make each section after target a windowshade" -- Draw() now wraps this whole
        // function's own call in a "Belts and Straps" CollapsingHeader, which supplies the heading).

        // Column width shared by the Set row, the Belt/Sling/Strap/Frog row, AND the Accessories
        // grid further down (2026-09-16, RedFalcon: "the accessory dropdowns should be in line with
        // the belt and straps dropdowns above... I would also like the set dropdown shortened so
        // that it's X aligns with the end of the Belt dropdown") -- computed ONCE from the actual
        // available content width, before anything in this whole section draws, so every one of
        // these rows shares IDENTICAL X positions. Plain `SameLine(x)` absolute positioning
        // throughout, deliberately NOT `ImGui::Columns()` anywhere in this section -- the classic
        // Columns API silently stretches its own LAST column to fill the rest of the content region
        // regardless of any `SetColumnWidth` call on it, which is what made earlier attempts look
        // uneven/misaligned. `kGap` (2026-09-16, RedFalcon: "it looks like the spacing between the
        // belt and sling dropdowns is 0") widens the margin a plain `ItemSpacing.x` alone left too
        // small to read as a real gap at this width.
        // `baseX` (2026-09-16, root-caused from a screenshot: "the spacing between belt and sling is
        // not as wide as the space between the others") -- ImGui::SameLine(x)'s absolute offset is
        // measured from the WINDOW's raw left edge (window->Pos.x), but GetContentRegionAvail() (and
        // the plain automatic-newline every column-0 cell relied on) measures/lands relative to the
        // CURRENT, possibly-indented cursor position. With any nonzero indent active in this part of
        // the tab, column 0 (indent-aware) sits correctly while every OTHER column (positioned via a
        // literal `SameLine(i * kColW)`, indent NOT included in that formula) lands shifted LEFT by
        // that same indent amount -- closing the gap specifically between column 0 and column 1,
        // while columns 1->2 and 2->3 stay evenly spaced since both shift equally. Capturing the
        // real starting X once via GetCursorPosX() (same coordinate frame SameLine's offset uses) and
        // adding it to every column's target -- INCLUDING column 0 explicitly, no longer relying on
        // the implicit newline -- puts every column in one consistent frame regardless of indent.
        const float baseX = ImGui::GetCursorPosX();
        const float kColW = ImGui::GetContentRegionAvail().x / 4.0f;
        // kGap reverted back to the original 10px margin (2026-09-16, RedFalcon: "I liked your
        // initial spacing before trying to 'fix' it better" -- the widened ItemSpacing.x*2 version
        // was never actually needed; the real bug was baseX, above, not this value).
        const float kGap = 10.0f;
        const float kComboW = kColW - kGap;
        const float kXBtnW = ImGui::GetFrameHeight(); // RemoveXButton's own square size

        // Set / Lantern row. Set's dropdown is shortened (2026-09-16) so its own red X lands at
        // EXACTLY `kComboW` -- the same right edge the Belt dropdown's own column has -- rather than
        // trailing off wherever a fixed-width combo happened to end.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Set");
        constexpr float kSetLabelW = 35.0f;
        ImGui::SameLine(baseX + kSetLabelW);
        const float kSetDropdownW = kComboW - kSetLabelW - kGap - kXBtnW;
        if (DrawBeltAccessoryCombo("beltset", "None", g_beltSetSelected, kSetNames, static_cast<int>(std::size(kSetNames)), kSetDropdownW))
        {
            WriteBeltSetRequest(g_beltSetSelected >= 0 ? kSetNames[g_beltSetSelected] : "None");
            requestReadCurrent();
        }
        ImGui::SameLine(baseX + kComboW - kXBtnW);
        if (RemoveXButton("##beltset_remove"))
        {
            g_beltSetSelected = -1;
            WriteBeltSetRequest("None");
            requestReadCurrent();
        }
        HoverTooltip("Remove Set (clears Belt, Sling, and Strap)");
        // Lantern: label THEN checkbox (2026-09-16, RedFalcon: "put the check box for lantern to the
        // right of the label" -- ImGui::Checkbox draws its own label AFTER the box by default, the
        // opposite order, hence drawing them as two separate widgets here), the whole label+checkbox
        // group right-aligned to the END OF THE FROG DROPDOWN specifically (RedFalcon, correcting
        // himself: "i mispoke and wanted lantern aligned with the right side of the frog dropdown" --
        // NOT the raw window/section edge as first built) -- `3 * kColW + kComboW` is that same
        // right edge the Frog column's own dropdown/X already line up on.
        {
            const float kFrogRightEdge = baseX + 3.0f * kColW + kComboW;
            const float textW = ImGui::CalcTextSize("Lantern").x;
            const float groupW = textW + ImGui::GetStyle().ItemSpacing.x + kXBtnW;
            ImGui::SameLine(kFrogRightEdge - groupW);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Lantern");
            ImGui::SameLine();
            if (ImGui::Checkbox("##lantern", &g_lanternOn))
            {
                WriteLanternRequest(g_lanternOn);
            }
        }

        // Belt / Sling / Strap / Frog -- fully independent of each other (RedFalcon, 2026-09-15: "no
        // forcing belts with straps, etc. Just basic replacement"). Laid out as a label+X row
        // followed by a dropdown row directly beneath it (RedFalcon's own mockup) -- the X on each
        // label row is right-aligned to `kComboW` (2026-09-16, RedFalcon: "the remove X buttons on
        // the labels to align with the right hand side of the dropdowns") rather than sitting
        // wherever the label's own text happens to end.
        //
        // Every change here also fires requestReadCurrent() (2026-09-15, RedFalcon: "when belt or
        // straps change... the accessory dropdowns should change to match every time") -- Lua's own
        // Spawner.ApplyBeltStrapPiece is the single source of truth for what's actually equipped
        // (a Set apply can also change 3 slots at once), so re-deriving every dropdown/gating flag
        // from a fresh Read Current is simpler and more correct than guessing locally, same
        // reasoning DrawClothesOutfitRow's own requestReadCurrent() calls already established.
        {
            const char* const kPieceLabels[4] = { "Belt", "Sling", "Strap", "Frog" };
            // Greyed out per-piece when this target has no swappable component there at all
            // (2026-09-16, RedFalcon: "let's extend this to the hair, facial hair and belts and
            // straps") -- distinct from g_xxxVisible (which stays driving the Accessories grid's
            // own dependency gating, unchanged): a HIDDEN piece is still real/swappable, only a
            // target with no such composite entry at all should greyed out the piece SELECTOR.
            const bool kPieceAvailable[4] = { g_beltAvailable, g_slingAvailable, g_strapAvailable, g_frogAvailable };

            for (int i = 0; i < 4; ++i)
            {
                // i==0 deliberately does NOT call SameLine (2026-09-16 fix, see baseX's own header
                // comment) -- SameLine() re-homes CursorPos.y to the PREVIOUS line (the Lantern row
                // above), which is exactly wrong for the first item of this new row; column 0 must
                // reach this row via the ordinary automatic newline, which is ALREADY indent-aware
                // and lands at baseX on its own. Only columns 1-3 (continuing an existing line) need
                // the explicit baseX-adjusted offset.
                if (i > 0)
                {
                    ImGui::SameLine(baseX + i * kColW);
                }
                ImGui::PushID(i);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(kPieceLabels[i]);
                ImGui::SameLine(baseX + i * kColW + kComboW - kXBtnW);
                ImGui::BeginDisabled(!kPieceAvailable[i]);
                if (RemoveXButton("##piece_remove"))
                {
                    switch (i)
                    {
                        case 0: g_beltPieceSelected = -1; break;
                        case 1: g_slingPieceSelected = -1; break;
                        case 2: g_strapPieceSelected = -1; break;
                        default: g_frogPieceSelected = -1; break;
                    }
                    WriteBeltPieceRequest(kPieceLabels[i], "None");
                    requestReadCurrent();
                }
                ImGui::EndDisabled(); // !kPieceAvailable[i]
                HoverTooltip("Remove");
                ImGui::PopID();
            }

            auto drawPieceCombo = [&](int i, const char* pieceType, int& selectedIdx, const char* const* names, int nameCount)
            {
                if (i > 0)
                {
                    ImGui::SameLine(baseX + i * kColW);
                }
                ImGui::PushID(pieceType);
                ImGui::BeginDisabled(!kPieceAvailable[i]);
                if (DrawBeltAccessoryCombo("piece", "None", selectedIdx, names, nameCount, kComboW))
                {
                    WriteBeltPieceRequest(pieceType, selectedIdx >= 0 ? names[selectedIdx] : "None");
                    requestReadCurrent();
                }
                ImGui::EndDisabled(); // !kPieceAvailable[i]
                ImGui::PopID();
            };
            drawPieceCombo(0, "Belt", g_beltPieceSelected, kBeltNames, static_cast<int>(std::size(kBeltNames)));
            drawPieceCombo(1, "Sling", g_slingPieceSelected, kSlingNames, static_cast<int>(std::size(kSlingNames)));
            drawPieceCombo(2, "Strap", g_strapPieceSelected, kStrapNames, static_cast<int>(std::size(kStrapNames)));
            drawPieceCombo(3, "Frog", g_frogPieceSelected, kFrogNames, static_cast<int>(std::size(kFrogNames)));
        }

        ImGui::Spacing();
        // "Detect Accessories" (2026-09-16, RedFalcon: "to the left of randomize accessories, can we
        // add a read accessories button that will read the accessories in the sockets and populate
        // them" -- named "Detect Accessories" per his own follow-up correction) -- a pure read, no
        // roll/clear performed first. Randomize/Clear now ALSO always re-read afterward on the Lua
        // side (RedFalcon: "when randomize is run, can it populate the fields") -- see
        // pollSocketAccStatus's own header for how that result comes back.
        if (ImGui::Button("Detect Accessories"))
        {
            WriteSocketAccRequest("READ");
        }
        ImGui::SameLine();
        if (ImGui::Button("Randomize Accessories"))
        {
            WriteSocketAccRequest("RANDOMIZE");
        }
        ImGui::SameLine();
        if (RemoveXButton("##socketacc_clear"))
        {
            WriteSocketAccRequest("CLEAR");
        }
        HoverTooltip("Clear all accessory socket items (not Belt/Sling/Strap/Frog, not the Lantern)");

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Accessories"))
        {
            // Gating REMOVED (2026-09-18, RedFalcon: "I found some of the default NPC set other
            // sockets without the related belt socket, so let's not disable any accessory sockets,
            // keep them always available after a detect accessories call" -- the original
            // 2026-09-15 assumption this piece-visibility gating was built on (an accessory can
            // only exist alongside its own piece) turned out to be wrong on real native data.
            // g_beltVisible/g_slingVisible/g_strapVisible/g_frogVisible are UNCHANGED and still
            // drive clearAccessoriesForRemovedPiece's own Lua-side behavior (removing a piece still
            // clears ITS accessories -- RedFalcon: "clearing that section when that belt item is
            // removed is still good") -- only the dropdowns' own interactivity is no longer gated
            // by them.
            //
            // Laid out with the SAME `SameLine(i * kColW)` positioning as the Belt/Sling/Strap/Frog
            // row above (2026-09-16) -- not `ImGui::Columns()` -- so this grid's own 4 columns
            // (Belt/Sling/Strap/Weapon) land at the EXACT same X as their own piece dropdown above,
            // matching RedFalcon's mockup. Each column draws independently (rather than looping
            // column-then-NextColumn) since the 4 lists have different lengths (7/8/9/4) and a
            // missing cell must still leave the NEXT row's SameLine offsets correct -- explicit
            // absolute positioning handles that for free, unlike Columns' implicit cell-advance.
            const int rowCount = 9; // max(Belt=7, Sling=8, Strap=9, Weapon=4)
            for (int row = 0; row < rowCount; ++row)
            {
                // ID note: kBeltSocketRows[0] ("Belt 1") and kStrapSocketRows[3] ("Strap 4") share
                // the SAME real socket name (soc_Strap01F is one physical socket some Belt pieces
                // reuse for their own bundled strap sub-entry) -- PushID(r.socket) alone would
                // therefore collide between the Belt and Strap columns. PushID on the (column, row)
                // pair below is unique regardless of any two rows sharing a real socket name.
                // Column 0 ALWAYS draws something, even when Belt has run out of real entries
                // (2026-09-16 fix) -- a bare `Dummy` placeholder rather than skipping outright, so
                // this row reliably starts via the ordinary indent-aware automatic newline exactly
                // once. Without this, a row where Belt (7 entries) is empty but Sling/Strap/Weapon
                // (8/9/4) still have one would make the FIRST `SameLine` call below the very first
                // positioning call on this row -- and SameLine() re-homes CursorPos.y to the
                // PREVIOUS row's Y (see baseX's own header comment), silently overlapping this row
                // onto the one above it.
                if (row < static_cast<int>(std::size(kBeltSocketRows)))
                {
                    const auto& r = kBeltSocketRows[row];
                    ImGui::PushID(0 * 100 + row);
                    if (DrawBeltAccessoryCombo("sock", r.friendlyName, g_beltSocketSelected[row], kSocketItemFriendlyNames, static_cast<int>(std::size(kSocketItemFriendlyNames)), kComboW))
                    {
                        WriteSocketItemRequest(r.socket, g_beltSocketSelected[row] >= 0 ? kSocketItemFriendlyNames[g_beltSocketSelected[row]] : "None");
                    }
                    ImGui::PopID();
                }
                else
                {
                    ImGui::Dummy(ImVec2(kComboW, ImGui::GetFrameHeight()));
                }

                // Columns 1-3 always position explicitly at their own absolute X, baseX-adjusted
                // (2026-09-16) -- column 0 above is now GUARANTEED to have drawn something (real or
                // Dummy), so every one of these SameLine calls is safely "continue this existing
                // line", never "start a new one".
                ImGui::SameLine(baseX + 1 * kColW);
                if (row < static_cast<int>(std::size(kSlingSocketRows)))
                {
                    const auto& r = kSlingSocketRows[row];
                    ImGui::PushID(1 * 100 + row);
                    if (DrawBeltAccessoryCombo("sock", r.friendlyName, g_slingSocketSelected[row], kSocketItemFriendlyNames, static_cast<int>(std::size(kSocketItemFriendlyNames)), kComboW))
                    {
                        WriteSocketItemRequest(r.socket, g_slingSocketSelected[row] >= 0 ? kSocketItemFriendlyNames[g_slingSocketSelected[row]] : "None");
                    }
                    ImGui::PopID();
                }

                ImGui::SameLine(baseX + 2 * kColW);
                if (row < static_cast<int>(std::size(kStrapSocketRows)))
                {
                    const auto& r = kStrapSocketRows[row];
                    ImGui::PushID(2 * 100 + row);
                    if (DrawBeltAccessoryCombo("sock", r.friendlyName, g_strapSocketSelected[row], kSocketItemFriendlyNames, static_cast<int>(std::size(kSocketItemFriendlyNames)), kComboW))
                    {
                        WriteSocketItemRequest(r.socket, g_strapSocketSelected[row] >= 0 ? kSocketItemFriendlyNames[g_strapSocketSelected[row]] : "None");
                    }
                    ImGui::PopID();
                }

                ImGui::SameLine(baseX + 3 * kColW);
                // Column 3 ALSO always draws something now (2026-09-16, second real screenshot bug:
                // "▶eltAAccessory Location Cheat Sheet" garbage text, rows visually fused together) --
                // the weapon list only has 4 entries against Belt/Sling/Strap's 7/8/9, so rows 4-8
                // used to leave this SameLine call dangling with nothing to land on. A dangling
                // SameLine means the NEXT row's own column-0 draw lands on THIS SAME line instead of
                // starting fresh (same root cause as the column-0 fix above, just triggered from the
                // opposite end of the row) -- across 5 consecutive rows (4-8) this cascaded into
                // several rows visually fusing onto one line, ending with the "Accessory Location
                // Cheat Sheet" header itself smashed onto the tail end of it. A Dummy here guarantees
                // every row always ends with a real widget, so the automatic newline after it is
                // never skipped.
                if (row < static_cast<int>(std::size(kWeaponSlotRows)))
                {
                    const auto& r = kWeaponSlotRows[row];
                    ImGui::PushID(3 * 100 + row);
                    if (DrawBeltAccessoryCombo("weaponslot", r.label, g_weaponSlotSelected[row], r.names, r.nameCount, kComboW))
                    {
                        WriteWeaponSlotRequest(r.locationKey, g_weaponSlotSelected[row] >= 0 ? r.names[g_weaponSlotSelected[row]] : "None");
                    }
                    ImGui::PopID();
                }
                else
                {
                    ImGui::Dummy(ImVec2(kComboW, ImGui::GetFrameHeight()));
                }
            }
            // "Belt and Straps Location Guide" (2026-09-16, RedFalcon supplied the real reference
            // image and asked for it nested INSIDE Accessories -- "add the guide section inside of
            // accessories, similar to how accessories is inside of belts and straps" -- rather than
            // as its own sibling windowshade like the original placeholder was). Renders at the
            // image's own native size, capped to whatever width is actually available (shrinks
            // proportionally if the window is narrower than the image, never upscales past native),
            // and centered horizontally. Closed by default, same convention as Accessories itself.
            ImGui::Spacing();
            if (ImGui::CollapsingHeader("Belt and Straps Location Guide"))
            {
                int imgW = 0, imgH = 0;
                ImTextureID tex = ImageLoader::GetOrLoad(kBeltStrapGuideImagePath, imgW, imgH);
                if (tex == ImTextureID_Invalid || imgW <= 0 || imgH <= 0)
                {
                    ImGui::TextDisabled("(guide image failed to load)");
                }
                else
                {
                    const float availW = ImGui::GetContentRegionAvail().x;
                    const float drawW = std::min(static_cast<float>(imgW), availW);
                    const float scale = drawW / static_cast<float>(imgW);
                    const float drawH = static_cast<float>(imgH) * scale;
                    const float offsetX = (availW - drawW) * 0.5f;
                    if (offsetX > 0.0f)
                    {
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                    }
                    ImGui::Image(tex, ImVec2(drawW, drawH));
                }
            }
        }
    }

    // ============================================================================================
    // "Poses and Actions" section, bottom of the Custom tab (2026-09-16, RedFalcon: "let's bring
    // over the poses... on the left side have it display a similar tree view to the tools, but
    // have it contain only everything under the poses branch from the tools tab"). Reuses
    // SpawnMenu::GetPosesTree() (the "Custom > Poses" subtree the Tools tab's own tree already
    // parses from spawn_menu.ini) rather than re-parsing that file here -- one source of truth,
    // refreshed whenever the Tools tab's own "Refresh" button is pressed.

    // Recursively finds the FIRST leaf whose own label matches `wantLabel` -- used by the pose
    // "reset" X button below to look up "Regular Fem Player Idle"/"Regular Masc Player Idle"'s own
    // Config.CUSTOM_POSES index without needing a hardcoded/hand-synced copy of that index (which
    // would silently go stale if RedFalcon ever reorders Other\Poses.xlsx). Returns -1 if not found
    // (e.g. spawn_menu.ini hasn't been generated with that row yet).
    auto FindPoseIndexByLabel(const SpawnMenu::PoseNode& node, const char* wantLabel) -> int
    {
        if (node.is_leaf && node.children.empty())
        {
            return (node.label == wantLabel) ? node.index : -1;
        }
        for (auto& child : node.children)
        {
            const int found = FindPoseIndexByLabel(child, wantLabel);
            if (found >= 0)
            {
                return found;
            }
        }
        return -1;
    }

    // One tree row: a category (TreeNode, same as the Tools tab) or a leaf (a small "+" button
    // that applies the pose immediately, followed by its name) -- NOT Selectable like the Tools
    // tab's own leaves, since there's nothing to "select" here, only "apply now".
    auto DrawPoseTreeNode(const SpawnMenu::PoseNode& node) -> void
    {
        if (node.is_leaf && node.children.empty())
        {
            ImGui::PushID(&node);
            if (ImGui::SmallButton("+"))
            {
                SpawnMenu::ApplyPoseByIndex(node.index);
            }
            HoverTooltip("Apply this pose to the target");
            ImGui::SameLine();
            ImGui::TextUnformatted(node.label.c_str());
            ImGui::PopID();
            return;
        }
        if (ImGui::TreeNode(node.label.c_str()))
        {
            for (auto& child : node.children)
            {
                DrawPoseTreeNode(child);
            }
            ImGui::TreePop();
        }
    }

    auto DrawPosesAndActions() -> void
    {
        // Left: the Poses tree, ~50% of the section width and tall enough for ~15 lines
        // (RedFalcon's own spec), scrollable beyond that -- same bordered-child convention the
        // Tools tab's own "##spawnmenu_tree" child uses.
        const float avail = ImGui::GetContentRegionAvail().x;
        const float kTreeWidth = avail * 0.5f;
        const float kTreeHeight = ImGui::GetTextLineHeightWithSpacing() * 15.0f;

        ImGui::BeginChild("##poses_tree", ImVec2(kTreeWidth, kTreeHeight), true);
        const SpawnMenu::PoseNode& posesRoot = SpawnMenu::GetPosesTree();
        if (posesRoot.children.empty())
        {
            ImGui::TextDisabled("(no poses -- check spawn_menu.ini and the Tools tab's Refresh)");
        }
        else
        {
            for (auto& child : posesRoot.children)
            {
                DrawPoseTreeNode(child);
            }
        }
        ImGui::EndChild();

        // Right: current-pose readout + reset, then Left/Right Hand dropdowns. Drawn inside its
        // OWN BeginChild (2026-09-16 fix -- RedFalcon: "this is also not looking right", the
        // "Unknown" box/X had wrapped onto a new line starting back at the WINDOW's own left edge
        // instead of sitting next to the tree) -- a bare TextUnformatted followed by another widget
        // with no SameLine() call in between always advances to the ordinary next line, which resets
        // CursorPos.X to the current window's own indent, NOT to wherever this column visually
        // started; only an explicit SameLine(x) avoids that, and one was missing here. A child
        // window sidesteps the whole class of bug: it's a separate ImGuiWindow with its OWN
        // coordinate frame starting at 0, so every plain SameLine()/SameLine(0-based x) call and
        // every automatic newline inside naturally lands relative to THIS column's own left edge,
        // no `rightBaseX` bookkeeping required (this replaces the previous fix's plain-SameLine(x)
        // approach, which was still correct for the two BELOW rows but never actually reached them
        // because the FIRST row above already wrapped to the wrong place).
        ImGui::SameLine();
        const float rightWidth = ImGui::GetContentRegionAvail().x;
        ImGui::BeginChild("##poses_right", ImVec2(rightWidth, kTreeHeight), false);
        const float innerW = ImGui::GetContentRegionAvail().x;
        const float kXBtnW = ImGui::GetFrameHeight();
        const float kGap = 10.0f;

        // Reset X aligned with the Right Hand row's own X below it (2026-09-16, RedFalcon: "can
        // we make it so that the pose X is aligned with the right hand X") -- that button sits at
        // `innerW - kGap - kXBtnW` (the Right Hand column's own right edge, one `kGap` in from
        // `innerW` -- see the Hand-row loop below, i=1 case), one `kGap` short of this row's own
        // naive `innerW - kXBtnW`, so this row's X (and the box feeding it) is pulled in to match.
        const float kPoseXPos = innerW - kGap - kXBtnW;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Current Pose");
        char poseBuf[128];
        std::snprintf(poseBuf, sizeof(poseBuf), "%s", g_currentPoseName.c_str());
        ImGui::SetNextItemWidth(kPoseXPos - kGap);
        ImGui::InputText("##current_pose_box", poseBuf, sizeof(poseBuf), ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine(kPoseXPos);
        if (RemoveXButton("##pose_reset"))
        {
            const bool isFemale = (MenuStatus::TargetSex() == "F");
            const int idx = FindPoseIndexByLabel(posesRoot, isFemale ? "Regular Fem Player Idle" : "Regular Masc Player Idle");
            if (idx >= 0)
            {
                SpawnMenu::ApplyPoseByIndex(idx);
                requestReadCurrent();
            }
        }
        HoverTooltip("Reset to the default standing idle pose for this target's sex");

        // Play/Pause/Frame stepper + scrub slider (2026-09-21, RedFalcon's frame-by-frame pose
        // scrubber follow-up) -- fits the same total width as the Current Pose row + its remove
        // button (`innerW`) just above. Play (">") is only enabled while a scrub is active AND
        // paused; Pause ("| |") only while active AND playing; the Frame</> steppers and the slider
        // both need an active scrub at all (the slider itself works whether playing or paused --
        // see Spawner.PoseScrubSeek's own header for why it doesn't force a pause).
        {
            const bool scrubActive = MenuStatus::ScrubActive();
            const bool scrubPaused = MenuStatus::ScrubPaused();

            ImGui::BeginDisabled(!(scrubActive && scrubPaused));
            if (ImGui::Button(">##pose_scrub_play"))
            {
                WritePoseScrubPlayRequest();
            }
            ImGui::EndDisabled();
            HoverTooltip("Resume playback of the current pose");

            ImGui::SameLine();
            ImGui::BeginDisabled(!(scrubActive && !scrubPaused));
            if (ImGui::Button("| |##pose_scrub_pause"))
            {
                WritePoseScrubPauseRequest();
            }
            ImGui::EndDisabled();
            HoverTooltip("Freeze the current pose in place");

            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Frame:");

            ImGui::SameLine();
            ImGui::BeginDisabled(!scrubActive);
            if (ImGui::Button("<##pose_scrub_prev"))
            {
                WritePoseScrubStepRequest(-1);
            }
            HoverTooltip("Previous frame");
            ImGui::SameLine();
            if (ImGui::Button(">##pose_scrub_next"))
            {
                WritePoseScrubStepRequest(1);
            }
            HoverTooltip("Next frame");
            ImGui::EndDisabled();

            // Slider, same total width as the Current Pose row + its remove button above
            // (`innerW`) -- RedFalcon: "Make all that fit the same width as current pose and its
            // remove button". Enabled whenever a scrub is active at all (see this block's own
            // header for why the slider isn't paused-only like the frame steppers).
            ImGui::BeginDisabled(!scrubActive);
            int scrubFrame = MenuStatus::ScrubFrame();
            const int maxFrame = std::max(0, MenuStatus::ScrubNumFrames() - 1);
            ImGui::SetNextItemWidth(innerW);
            if (ImGui::SliderInt("##pose_scrub_slider", &scrubFrame, 0, maxFrame))
            {
                WritePoseScrubSeekRequest(scrubFrame);
            }
            ImGui::EndDisabled();
            HoverTooltip("Scrub through the pose");
        }

        ImGui::Spacing();

        // Left Hand / Right Hand -- side by side (2026-09-16, RedFalcon: "i want left hand and
        // right hand on the same line next to each other"), each its own label+X row followed by
        // 4 category dropdowns (2026-09-21: Weapons/Tools/Bottles/Other, RedFalcon: "populate the
        // hand dropdowns... 4 dropdowns, they all override each other, its just to reduce the list
        // as its a lot of stuff and may eventually grow" -- replaces the old single always-empty
        // combo). Same two-pass layout DrawBeltsAndStraps' own Belt/Sling/Strap/Frog columns use
        // (`i * kColW`, 0-based here since it's this child's own coordinate frame). The X
        // (RedFalcon: "the X for left hand will clear socket ik_weapon_lSocket and the X for right
        // hand will clear ik_weapon_rSocket") reuses the EXISTING per-socket "None" pipeline the
        // Accessories grid's own dropdowns already use (WriteSocketItemRequest ->
        // BeltStrapPolls.socketItem -> Spawner.ApplySocketItemManual, whose "None" branch already
        // calls Spawner.RemoveSocketAttachment) rather than inventing a new request file -- these
        // are real IK weapon-attach sockets, not a new mechanism.
        {
            const char* const kHandLabels[2] = { "Left Hand", "Right Hand" };
            const char* const kHandSockets[2] = { "ik_weapon_lSocket", "ik_weapon_rSocket" };
            const float kHandColW = innerW / 2.0f;
            const float kHandComboW = kHandColW - kGap;

            for (int i = 0; i < 2; ++i)
            {
                if (i > 0)
                {
                    ImGui::SameLine(i * kHandColW);
                }
                ImGui::PushID(i);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(kHandLabels[i]);
                ImGui::SameLine(i * kHandColW + kHandComboW - kXBtnW);
                if (RemoveXButton("##hand_remove"))
                {
                    for (int c = 0; c < kHandCategoryCount; ++c) { g_handCategorySelected[i][c] = -1; }
                    WriteSocketItemRequest(kHandSockets[i], "None");
                    requestReadCurrent();
                }
                HoverTooltip("Remove");
                ImGui::PopID();
            }

            // 4 mutually-exclusive category dropdowns per hand -- only one item can ever occupy a
            // hand at a time, so picking something in ONE dropdown clears the other 3 for that
            // same hand (RedFalcon: "they all override each other") and applies the pick;
            // "None" (DrawBeltAccessoryCombo signals this via selectedIdx going back to -1) clears
            // the whole hand, same as the X button just above -- "selecting none, clears that hand
            // and resets the name" (each combo's own closed-state text is its category name,
            // DrawBeltAccessoryCombo's `defaultLabel` param, matching the belt-socket convention).
            for (int c = 0; c < kHandCategoryCount; ++c)
            {
                const HandCategoryRow& row = kHandCategoryRows[c];
                for (int i = 0; i < 2; ++i)
                {
                    if (i > 0)
                    {
                        ImGui::SameLine(i * kHandColW);
                    }
                    ImGui::PushID(i * 10 + c);
                    const bool changed = DrawBeltAccessoryCombo(
                        "hand_combo", row.label, g_handCategorySelected[i][c], row.names, row.nameCount, kHandComboW);
                    if (changed)
                    {
                        if (g_handCategorySelected[i][c] < 0)
                        {
                            for (int c2 = 0; c2 < kHandCategoryCount; ++c2) { g_handCategorySelected[i][c2] = -1; }
                            WriteSocketItemRequest(kHandSockets[i], "None");
                        }
                        else
                        {
                            for (int c2 = 0; c2 < kHandCategoryCount; ++c2)
                            {
                                if (c2 != c) { g_handCategorySelected[i][c2] = -1; }
                            }
                            WriteSocketItemRequest(kHandSockets[i], row.names[g_handCategorySelected[i][c]]);
                        }
                        requestReadCurrent();
                    }
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
    }

    // "Lights" section (2026-09-21, Photo tab mockup) -- 3 fixed light+spill-shield rigs, each a
    // real (but non-persistent) spawn controlled through the exact same request/poll bridge shape
    // as everything else in this file. NOT gated on hasTarget/g_hasDetected -- lights are their own
    // independent world objects, not a customization of the locked target (same "spawning and
    // camera are fine regardless" exemption Poses/Belts don't get).
    bool g_lightActive[3] = { false, false, false };
    int g_lightColor[3][3] = { { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 } };
    float g_lightBrightness[3] = { 1000.0f, 1000.0f, 1000.0f };
    float g_lightThrow[3] = { 500.0f, 500.0f, 500.0f };
    float g_lightShieldDist[3] = { 15.0f, 15.0f, 15.0f };
    float g_lightShieldSize[3] = { 1.0f, 1.0f, 1.0f };
    bool g_lightShieldVisible[3] = { true, true, true };

    // "Weather" / "Time" / "Freeze Time" (2026-09-22, under Light 3 on RedFalcon's own request) --
    // thin GUI front-end over the existing lbphotoweather/lbphototime console commands (main.lua),
    // same names/order as PHOTO_WEATHERS there (Genlandia's real preset order -- see that table's
    // own comment if this is ever used on a different map). Weather/Time are fire-and-forget action
    // combos, not persisted selectors: RedFalcon's own spec is "let 'Weather' be the default
    // display, and once a weather is selected, return it back to 'Weather'" -- so their preview
    // text is hardcoded, never bound to the last pick. Declared HERE (above DrawLightsSectionImpl,
    // which uses all of this at the end of its own body) since C++ needs these visible before use,
    // unlike the Lua side's closures.
    constexpr const char* kPhotoWeatherNames[] = {
        "Windy", "TortugaMist", "TestSunny", "TestCloudy", "Sunny", "Storm", "RainHeavy",
        "Rainbow", "Rain", "Overcast", "Mist", "LobbySunny", "HighPressure", "Fog", "Default",
        "Cloudy", "AshlandsFog",
    };

    constexpr const char* PHOTO_WEATHER_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_photoweather_request.txt";
    auto WritePhotoWeatherRequest(const char* name) -> void
    {
        std::ofstream f(PHOTO_WEATHER_REQUEST_PATH, std::ios::trunc);
        if (!f) { return; }
        f << name << "\n";
    }

    constexpr const char* PHOTO_TIME_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_phototime_request.txt";
    auto WritePhotoTimeRequest(int hour) -> void
    {
        std::ofstream f(PHOTO_TIME_REQUEST_PATH, std::ios::trunc);
        if (!f) { return; }
        f << hour << "\n";
    }

    // Freeze Time's checkbox reflects the day-cycle component's REAL tick-enabled state (published
    // every poll tick by main.lua, read back via IsComponentTickEnabled -- not a value this file
    // caches from its own last write) -- same lesson as the Target Highlight toggle further below:
    // trusting a locally-cached flag instead of the live state is exactly what made that toggle read
    // wrong after an exit/re-entry the button didn't cause.
    bool g_photoTimeFrozen = false;
    constexpr const char* PHOTO_FREEZETIME_STATUS_PATH = "ue4ss/Mods/LivingBase/custom_photofreezetime_status.txt";
    auto pollPhotoFreezeTimeStatus() -> void
    {
        std::ifstream f(PHOTO_FREEZETIME_STATUS_PATH);
        if (!f) { g_photoTimeFrozen = false; return; }
        std::string line;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r') { line.pop_back(); }
            if (line == "FROZEN=1") { g_photoTimeFrozen = true; }
            else if (line == "FROZEN=0") { g_photoTimeFrozen = false; }
        }
    }

    constexpr const char* PHOTO_FREEZETIME_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_photofreezetime_request.txt";
    auto WritePhotoFreezeTimeRequest(bool freeze) -> void
    {
        std::ofstream f(PHOTO_FREEZETIME_REQUEST_PATH, std::ios::trunc);
        if (!f) { return; }
        f << (freeze ? "1" : "0") << "\n";
    }

    constexpr const char* LIGHTS_STATUS_PATH = "ue4ss/Mods/LivingBase/custom_lights_status.txt";
    // Continuous poll (2026-09-21) -- reads whatever main.lua's own BeltStrapPolls.publishLightsStatus
    // last wrote, same "small file, no request/response pending-timer dance" convention as
    // MenuStatus.cpp's own spawn_menu_status.txt. Called once per frame from DrawLightsSection
    // itself (cheap: one ifstream open) rather than folded into pollReadCurrentResult -- lights
    // aren't part of that target-scoped read at all.
    auto pollLightsStatus() -> void
    {
        std::ifstream f(LIGHTS_STATUS_PATH);
        if (!f)
        {
            for (int i = 0; i < 3; ++i) { g_lightActive[i] = false; }
            return;
        }
        for (int i = 0; i < 3; ++i) { g_lightActive[i] = false; }
        std::string line;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r') { line.pop_back(); }
            auto eq = line.find('=');
            if (eq == std::string::npos) { continue; }
            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            if (key.rfind("LIGHT", 0) != 0) { continue; }
            size_t us = key.find('_', 5);
            if (us == std::string::npos) { continue; }
            int slot = std::atoi(key.substr(5, us - 5).c_str());
            if (slot < 1 || slot > 3) { continue; }
            int idx = slot - 1;
            std::string field = key.substr(us + 1);
            if (field == "ACTIVE") { g_lightActive[idx] = (value == "1"); }
            else if (field == "COLOR")
            {
                std::istringstream cs(value);
                std::string r, g, b;
                if (std::getline(cs, r, ',') && std::getline(cs, g, ',') && std::getline(cs, b))
                {
                    g_lightColor[idx][0] = std::atoi(r.c_str());
                    g_lightColor[idx][1] = std::atoi(g.c_str());
                    g_lightColor[idx][2] = std::atoi(b.c_str());
                }
            }
            else if (field == "BRIGHTNESS") { g_lightBrightness[idx] = std::strtof(value.c_str(), nullptr); }
            else if (field == "THROW") { g_lightThrow[idx] = std::strtof(value.c_str(), nullptr); }
            else if (field == "SHIELDDIST") { g_lightShieldDist[idx] = std::strtof(value.c_str(), nullptr); }
            else if (field == "SHIELDSIZE") { g_lightShieldSize[idx] = std::strtof(value.c_str(), nullptr); }
            else if (field == "SHIELDVIS") { g_lightShieldVisible[idx] = (value == "1"); }
        }
    }

    constexpr const char* LIGHT_ENABLE_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_light_enable_request.txt";
    auto WriteLightEnableRequest(int slot, bool on) -> void
    {
        std::ofstream f(LIGHT_ENABLE_REQUEST_PATH, std::ios::trunc);
        if (!f) { return; }
        f << slot << ":" << (on ? "1" : "0") << "\n";
    }

    constexpr const char* LIGHT_COLOR_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_light_color_request.txt";
    auto WriteLightColorRequest(int slot, int r, int g, int b) -> void
    {
        std::ofstream f(LIGHT_COLOR_REQUEST_PATH, std::ios::trunc);
        if (!f) { return; }
        f << slot << ":" << r << ":" << g << ":" << b << "\n";
    }

    constexpr const char* LIGHT_BRIGHTNESS_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_light_brightness_request.txt";
    auto WriteLightBrightnessRequest(int slot, float value) -> void
    {
        std::ofstream f(LIGHT_BRIGHTNESS_REQUEST_PATH, std::ios::trunc);
        if (!f) { return; }
        f << slot << ":" << value << "\n";
    }

    constexpr const char* LIGHT_THROW_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_light_throwdist_request.txt";
    auto WriteLightThrowRequest(int slot, float value) -> void
    {
        std::ofstream f(LIGHT_THROW_REQUEST_PATH, std::ios::trunc);
        if (!f) { return; }
        f << slot << ":" << value << "\n";
    }

    constexpr const char* LIGHT_SHIELDDIST_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_light_shielddist_request.txt";
    auto WriteLightShieldDistRequest(int slot, float value) -> void
    {
        std::ofstream f(LIGHT_SHIELDDIST_REQUEST_PATH, std::ios::trunc);
        if (!f) { return; }
        f << slot << ":" << value << "\n";
    }

    constexpr const char* LIGHT_SHIELDSIZE_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_light_shieldsize_request.txt";
    auto WriteLightShieldSizeRequest(int slot, float value) -> void
    {
        std::ofstream f(LIGHT_SHIELDSIZE_REQUEST_PATH, std::ios::trunc);
        if (!f) { return; }
        f << slot << ":" << value << "\n";
    }

    constexpr const char* LIGHT_SHIELDVIS_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_light_shieldvisible_request.txt";
    auto WriteLightShieldVisibleRequest(int slot, bool visible) -> void
    {
        std::ofstream f(LIGHT_SHIELDVIS_REQUEST_PATH, std::ios::trunc);
        if (!f) { return; }
        f << slot << ":" << (visible ? "1" : "0") << "\n";
    }

    // Row layout matches the Height slider's own convention (DrawTargetHeader's Body section):
    // a fixed-width label on the left, a "##hidden" slider taking the rest of the row's width.
    auto DrawLightsSectionImpl() -> void
    {
        pollLightsStatus();

        const float avail = ImGui::GetContentRegionAvail().x;
        constexpr float kLabelColW = 150.0f;
        const float kSliderW = avail - kLabelColW;

        for (int i = 0; i < 3; ++i)
        {
            ImGui::PushID(i);
            ImGui::Text("Light %d:", i + 1);

            // Row: Enable / Color / Show Spill Shield (2026-09-23, RedFalcon: "squeeze the top line
            // tighter and the spill shield text gets cut off. its end should align with the edge of
            // the sliders" -- then, same day, "put label text to the left of everything on the top
            // row? Right now looks like it says 'Enable Color'" -- Checkbox("Enable", ...)'s own
            // trailing label sat immediately against "Color"'s leading label with barely any visual
            // gap, reading as one run-on phrase). Every label now sits to the LEFT of its own
            // control (matching "Color"'s own convention, which never had this problem) via plain
            // ##-only checkboxes -- Enable/Color still sit close together at their natural width;
            // "Show Spill Shield" is still right-anchored so its checkbox's right edge lands exactly
            // at `avail`, the same right edge kSliderW's sliders use, regardless of label width.
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Enable");
            ImGui::SameLine();
            bool enabled = g_lightActive[i];
            if (ImGui::Checkbox("##enable", &enabled))
            {
                WriteLightEnableRequest(i + 1, enabled);
            }

            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Color");
            ImGui::SameLine();
            ImGui::BeginDisabled(!g_lightActive[i]);
            {
                float col[3] = { g_lightColor[i][0] / 255.0f, g_lightColor[i][1] / 255.0f, g_lightColor[i][2] / 255.0f };
                if (ImGui::ColorEdit3("##color", col, ImGuiColorEditFlags_NoInputs))
                {
                    WriteLightColorRequest(i + 1,
                        static_cast<int>(col[0] * 255.0f + 0.5f),
                        static_cast<int>(col[1] * 255.0f + 0.5f),
                        static_cast<int>(col[2] * 255.0f + 0.5f));
                }
            }
            ImGui::EndDisabled();

            constexpr const char* kShieldLabel = "Show Spill Shield";
            const float shieldW = ImGui::CalcTextSize(kShieldLabel).x + ImGui::GetStyle().ItemInnerSpacing.x
                + ImGui::GetFrameHeight();
            ImGui::SameLine(avail - shieldW);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(kShieldLabel);
            ImGui::SameLine();
            bool shieldVis = g_lightShieldVisible[i];
            ImGui::BeginDisabled(!g_lightActive[i]);
            if (ImGui::Checkbox("##shieldvis", &shieldVis))
            {
                WriteLightShieldVisibleRequest(i + 1, shieldVis);
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!g_lightActive[i]);

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Brightness");
            ImGui::SameLine(kLabelColW);
            ImGui::SetNextItemWidth(kSliderW);
            float brightness = g_lightBrightness[i];
            if (ImGui::SliderFloat("##brightness", &brightness, 10.0f, 3000.0f, "%.0f"))
            {
                WriteLightBrightnessRequest(i + 1, brightness);
            }

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Throw Distance");
            ImGui::SameLine(kLabelColW);
            ImGui::SetNextItemWidth(kSliderW);
            float throwDist = g_lightThrow[i];
            if (ImGui::SliderFloat("##throwdist", &throwDist, 300.0f, 2000.0f, "%.0f"))
            {
                WriteLightThrowRequest(i + 1, throwDist);
            }

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Spill Shield Distance");
            ImGui::SameLine(kLabelColW);
            ImGui::SetNextItemWidth(kSliderW);
            float shieldDist = g_lightShieldDist[i];
            if (ImGui::SliderFloat("##shielddist", &shieldDist, 10.0f, 100.0f, "%.0f"))
            {
                WriteLightShieldDistRequest(i + 1, shieldDist);
            }

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Spill Shield Size");
            ImGui::SameLine(kLabelColW);
            ImGui::SetNextItemWidth(kSliderW);
            float shieldSize = g_lightShieldSize[i];
            if (ImGui::SliderFloat("##shieldsize", &shieldSize, 0.5f, 5.0f, "%.2f"))
            {
                WriteLightShieldSizeRequest(i + 1, shieldSize);
            }

            ImGui::EndDisabled();

            if (i < 2) { ImGui::Separator(); }
            ImGui::PopID();
        }

        // Weather / Time / Freeze Time row (2026-09-22, RedFalcon: "under light 3, can we have a
        // separator like the others and two drop downs and a checkbox?").
        ImGui::Separator();
        pollPhotoFreezeTimeStatus();

        const float rowAvail = ImGui::GetContentRegionAvail().x;
        const float freezeW = ImGui::CalcTextSize("Freeze Time").x + ImGui::GetStyle().ItemInnerSpacing.x
            + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x * 2.0f;
        const float comboW = (rowAvail - freezeW - ImGui::GetStyle().ItemSpacing.x) / 2.0f;

        ImGui::SetNextItemWidth(comboW);
        if (ImGui::BeginCombo("##photoweather", "Weather"))
        {
            for (const char* name : kPhotoWeatherNames)
            {
                if (ImGui::Selectable(name))
                {
                    WritePhotoWeatherRequest(name);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(comboW);
        if (ImGui::BeginCombo("##phototime", "Time"))
        {
            for (int h = 0; h < 24; ++h)
            {
                char label[8];
                std::snprintf(label, sizeof(label), "%02d", h);
                if (ImGui::Selectable(label))
                {
                    WritePhotoTimeRequest(h);
                }
            }
            ImGui::EndCombo();
        }

        // Freeze Time is a ONE-WAY uncheck (2026-09-22, RedFalcon: "they should not be locked to
        // start with, except freeze time. i want it a one way uncheck once time has changed and
        // time has stopped") -- Weather/Time stay freely clickable at all times, but Freeze Time
        // itself starts (and stays) DISABLED/unclickable while time is running normally: there's
        // nothing to unfreeze yet, and the box can't be checked directly (freezing only happens as
        // a side effect of a Time selection converging and settling -- see lbphototime's own "ARRIVED
        // ... frozen exactly" step above). Once that happens, g_photoTimeFrozen goes true, the box
        // becomes enabled showing checked, and the ONLY interaction possible from there is
        // unchecking it (which resumes the normal running cycle) -- it can never go disabled-false
        // straight to checked-true by a click, only by time actually stopping on its own.
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Freeze Time");
        ImGui::SameLine();
        bool freeze = g_photoTimeFrozen;
        ImGui::BeginDisabled(!g_photoTimeFrozen);
        if (ImGui::Checkbox("##freezetime", &freeze))
        {
            WritePhotoFreezeTimeRequest(false);
        }
        ImGui::EndDisabled();
    }

    // "Camera" section (2026-09-22, Photo Mode tab's second half -- Lights shipped 2026-09-21, this
    // is "the next piece for this same tab" per that section's own closing note). 3-way mode switch
    // (Tripod/Selfie/First Person) built on the pre-existing lbphototripod/lbfirstperson Lua
    // plumbing from 2026-09-08. REWRITTEN same day after re-reading RedFalcon's original mockup
    // turned up real misses in the first pass: Tripod now snapshots the player's true first-person
    // eye pose (not the old fixed-distance placement), Selfie continuously tracks the player's face
    // every tick instead of a one-time placement, and the movement pad now works for ALL THREE modes
    // as an offset relative to each mode's own live base pose (Spawner._photoCamApplyPose/
    // PhotoCamAdjustOffset, spawner.lua) -- First Person gets positional-offset-only (RedFalcon:
    // rotation stays on the mouse there). Coords stays Tripod-only per the original mockup, and is
    // now a real edit popup (see DrawCameraCoordsPopup below), not a passive readout.
    enum class PhotoCamMode { Off, Tripod, Selfie, FirstPerson };
    PhotoCamMode g_photoCamMode = PhotoCamMode::Off;
    bool g_photoCamHasPose = false;
    float g_photoCamPos[3] = { 0.0f, 0.0f, 0.0f };
    float g_photoCamRot[3] = { 0.0f, 0.0f, 0.0f }; // Pitch, Yaw, Roll
    float g_photoCamFov = 70.0f;

    constexpr const char* PHOTOCAM_STATUS_PATH = "ue4ss/Mods/LivingBase/custom_photocam_status.txt";
    // Same "small file, no request/response pending-timer dance" convention as pollLightsStatus.
    auto pollPhotoCamStatus() -> void
    {
        std::ifstream f(PHOTOCAM_STATUS_PATH);
        if (!f)
        {
            g_photoCamMode = PhotoCamMode::Off;
            g_photoCamHasPose = false;
            return;
        }
        g_photoCamMode = PhotoCamMode::Off;
        g_photoCamHasPose = false;
        std::string line;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r') { line.pop_back(); }
            auto eq = line.find('=');
            if (eq == std::string::npos) { continue; }
            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            if (key == "MODE")
            {
                if (value == "TRIPOD") { g_photoCamMode = PhotoCamMode::Tripod; }
                else if (value == "SELFIE") { g_photoCamMode = PhotoCamMode::Selfie; }
                else if (value == "FIRSTPERSON") { g_photoCamMode = PhotoCamMode::FirstPerson; }
                else { g_photoCamMode = PhotoCamMode::Off; }
            }
            else if (key == "POS")
            {
                std::istringstream ps(value);
                std::string x, y, z;
                if (std::getline(ps, x, ',') && std::getline(ps, y, ',') && std::getline(ps, z))
                {
                    g_photoCamPos[0] = std::strtof(x.c_str(), nullptr);
                    g_photoCamPos[1] = std::strtof(y.c_str(), nullptr);
                    g_photoCamPos[2] = std::strtof(z.c_str(), nullptr);
                    g_photoCamHasPose = true;
                }
            }
            else if (key == "ROT")
            {
                std::istringstream rs(value);
                std::string p, yw, r;
                if (std::getline(rs, p, ',') && std::getline(rs, yw, ',') && std::getline(rs, r))
                {
                    g_photoCamRot[0] = std::strtof(p.c_str(), nullptr);
                    g_photoCamRot[1] = std::strtof(yw.c_str(), nullptr);
                    g_photoCamRot[2] = std::strtof(r.c_str(), nullptr);
                }
            }
            else if (key == "FOV") { g_photoCamFov = std::strtof(value.c_str(), nullptr); }
        }
    }

    constexpr const char* PHOTOCAM_MODE_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_photocam_mode_request.txt";
    auto WritePhotoCamModeRequest(const char* mode) -> void
    {
        std::ofstream f(PHOTOCAM_MODE_REQUEST_PATH, std::ios::trunc);
        if (!f) { return; }
        f << mode << "\n";
    }

    // APPENDED, not overwritten -- see Spawner side's BeltStrapPolls.photoCamMove for why (a held
    // repeat-button fires many times between polls, same reasoning as move_request.txt).
    constexpr const char* PHOTOCAM_MOVE_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_photocam_move_request.txt";
    auto QueuePhotoCamMove(const char* direction, float amount) -> void
    {
        std::ofstream f(PHOTOCAM_MOVE_REQUEST_PATH, std::ios::app);
        if (!f) { return; }
        f << "MOVE:" << direction << ":" << amount << "\n";
    }
    auto QueuePhotoCamRotate(const char* axis, float amount) -> void
    {
        std::ofstream f(PHOTOCAM_MOVE_REQUEST_PATH, std::ios::app);
        if (!f) { return; }
        f << "ROTATE:" << axis << ":" << amount << "\n";
    }

    constexpr const char* PHOTOCAM_FOV_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_photocam_fov_request.txt";
    auto WritePhotoCamFovRequest(float value) -> void
    {
        std::ofstream f(PHOTOCAM_FOV_REQUEST_PATH, std::ios::trunc);
        if (!f) { return; }
        f << value << "\n";
    }

    // "Toggle Target Highlight" (2026-09-23, RedFalcon: "a button... click it swaps between showing
    // and hiding the highlights used when targeting something for a cleaner picture") -- its own
    // small always-on status file (main.lua's BeltStrapPolls.publishHighlightStatus), separate from
    // custom_photocam_status.txt since this toggle is useful whether or not a camera mode is active
    // at all (that file gets deleted entirely while mode=="OFF").
    bool g_targetHighlightSuppressed = false;
    constexpr const char* HIGHLIGHT_STATUS_PATH = "ue4ss/Mods/LivingBase/custom_highlight_status.txt";
    auto pollHighlightStatus() -> void
    {
        std::ifstream f(HIGHLIGHT_STATUS_PATH);
        if (!f) { g_targetHighlightSuppressed = false; return; }
        std::string line;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r') { line.pop_back(); }
            if (line == "SUPPRESSED=1") { g_targetHighlightSuppressed = true; }
            else if (line == "SUPPRESSED=0") { g_targetHighlightSuppressed = false; }
        }
    }

    constexpr const char* HIGHLIGHT_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_highlight_request.txt";
    auto WriteHighlightRequest(bool suppressed) -> void
    {
        std::ofstream f(HIGHLIGHT_REQUEST_PATH, std::ios::trunc);
        if (!f) { return; }
        f << (suppressed ? "1" : "0") << "\n";
    }

    // Coords popup (2026-09-22, RedFalcon: "coords should also act the same as the coords in spawn
    // mode... a button you click on that brings up the ability to set them manually") -- same
    // Preview/Apply/Reset/Cancel shape as CoordsMenu.cpp's own "Edit Coordinates" window, just
    // scoped to the Tripod camera instead of a locked target: no target-identity tracking needed
    // (it's always "the" tripod), and the live pose to seed/compare against comes from
    // g_photoCamPos/g_photoCamRot (this file's own pollPhotoCamStatus) instead of MenuStatus.
    constexpr const char* PHOTOCAM_COORDS_REQUEST_PATH = "ue4ss/Mods/LivingBase/custom_photocam_coords_request.txt";
    auto WritePhotoCamCoordsRequest(float x, float y, float z, float pitch, float yaw, float roll) -> void
    {
        std::ofstream f(PHOTOCAM_COORDS_REQUEST_PATH, std::ios::trunc);
        if (!f) { return; }
        f << x << "," << y << "," << z << ":" << pitch << "," << yaw << "," << roll << "\n";
    }

    bool g_photoCoordsOpen = false;
    float g_photoCoordsOpenPos[3]{}, g_photoCoordsOpenRot[3]{}; // Pitch, Yaw, Roll -- snapshot at open
    float g_photoCoordsFieldPos[3]{}, g_photoCoordsFieldRot[3]{};

    auto OpenPhotoCamCoords() -> void
    {
        for (int i = 0; i < 3; ++i)
        {
            g_photoCoordsOpenPos[i] = g_photoCamPos[i];
            g_photoCoordsFieldPos[i] = g_photoCamPos[i];
            g_photoCoordsOpenRot[i] = g_photoCamRot[i];
            g_photoCoordsFieldRot[i] = g_photoCamRot[i];
        }
        g_photoCoordsOpen = true;
    }

    auto SendPhotoCamCoords(const float pos[3], const float rot[3]) -> void
    {
        // Rotation X/Y/Z on screen = Roll/Pitch/Yaw, matching CoordsMenu.cpp's own convention --
        // g_photoCamRot is stored Pitch/Yaw/Roll (index 0/1/2, matching the status file's own ROT=
        // field order), so the popup fields below index it the same way and this just forwards
        // straight through without reordering.
        WritePhotoCamCoordsRequest(pos[0], pos[1], pos[2], rot[0], rot[1], rot[2]);
    }

    auto DrawPhotoCamCoordsPopupImpl() -> void
    {
        if (!g_photoCoordsOpen) { return; }
        // Tripod exited out from under this popup (mode switch, target-unrelated auto-reset, etc.)
        // -- close without sending a stray revert-write, since there may be no tripod left to move.
        if (g_photoCamMode != PhotoCamMode::Tripod)
        {
            g_photoCoordsOpen = false;
            return;
        }
        bool stayOpen = true;
        ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Edit Camera Coordinates", &stayOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextDisabled("Tripod Camera");
            ImGui::Separator();
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputFloat("X", &g_photoCoordsFieldPos[0]);
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputFloat("Y", &g_photoCoordsFieldPos[1]);
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputFloat("Z", &g_photoCoordsFieldPos[2]);
            ImGui::Separator();
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputFloat("Rotation X (Pitch)", &g_photoCoordsFieldRot[0]);
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputFloat("Rotation Y (Yaw)", &g_photoCoordsFieldRot[1]);
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputFloat("Rotation Z (Roll)", &g_photoCoordsFieldRot[2]);
            ImGui::Separator();

            if (ImGui::Button("Reset", ImVec2(60.0f, 0.0f)))
            {
                for (int i = 0; i < 3; ++i)
                {
                    g_photoCoordsFieldPos[i] = g_photoCoordsOpenPos[i];
                    g_photoCoordsFieldRot[i] = g_photoCoordsOpenRot[i];
                }
                SendPhotoCamCoords(g_photoCoordsOpenPos, g_photoCoordsOpenRot);
            }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Move the camera back to where it was when this window opened, and reset these fields to match. Stays open."); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(60.0f, 0.0f)))
            {
                SendPhotoCamCoords(g_photoCoordsOpenPos, g_photoCoordsOpenRot);
                g_photoCoordsOpen = false;
            }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Move the camera back to where it was when this window opened, then close."); }
            ImGui::SameLine();
            if (ImGui::Button("Preview", ImVec2(60.0f, 0.0f)))
            {
                SendPhotoCamCoords(g_photoCoordsFieldPos, g_photoCoordsFieldRot);
            }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Move the camera to the typed values now, without closing."); }
            ImGui::SameLine();
            if (ImGui::Button("Apply", ImVec2(60.0f, 0.0f)))
            {
                SendPhotoCamCoords(g_photoCoordsFieldPos, g_photoCoordsFieldRot);
                g_photoCoordsOpen = false;
            }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Move the camera to the typed values and close -- final."); }
        }
        ImGui::End();

        if (!stayOpen && g_photoCoordsOpen)
        {
            SendPhotoCamCoords(g_photoCoordsOpenPos, g_photoCoordsOpenRot);
            g_photoCoordsOpen = false;
        }
    }

    // Precision: purely a LOCAL multiplier on the base move/rotate step sent per click/repeat-tick
    // -- unlike MoveMenu.cpp's own Precision slider, Lua doesn't need to know this at all, since
    // Spawner.MoveTripodCameraRelative/RotateTripodCamera already take an exact amount rather than
    // batching deltas from multiple sources the way EditNearestInFront does. Own 6-level scheme
    // (not shared with MoveMenu's g_precision_idx/PRECISION_SCALES) since the numbers mean something
    // different here (a direct step multiplier, not normalized against a 0.25 Lua-side baseline).
    constexpr const char* CAM_PRECISION_LABELS[6] = { "1/8", "1/4", "1/2", "1x (normal)", "2x", "4x" };
    constexpr float CAM_PRECISION_SCALES[6] = { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    int g_camPrecisionIdx = 3;

    constexpr float kCamMoveStepUU = 20.0f;
    constexpr float kCamRotateStepDeg = 3.0f;

    auto DrawCameraSectionImpl() -> void
    {
        pollPhotoCamStatus();
        pollHighlightStatus();

        const bool placementActive = MenuStatus::IsPlacementActive();
        // hasTripodOrSelfie gates Rotate/FOV/Coords (things only the shared CameraActor supports);
        // hasAnyCam gates the move pad/Reset -- First Person gets those too now (positional-offset
        // Reset + move, see spawner.lua's own PhotoCamSetMode/MoveFirstPersonRelative).
        const bool hasTripodOrSelfie = (g_photoCamMode == PhotoCamMode::Tripod) || (g_photoCamMode == PhotoCamMode::Selfie);
        const bool hasAnyCam = hasTripodOrSelfie || (g_photoCamMode == PhotoCamMode::FirstPerson);
        const float avail = ImGui::GetContentRegionAvail().x;
        const float thirdW = (avail - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
        // Taller buttons throughout this section (2026-09-23, RedFalcon: "the camera section be the
        // same height as the light section? The buttons are kinda short and cramped and theres room
        // to expand it some") -- Lights' own per-slot label/slider rows naturally run close to the
        // tab's full height; Camera's button-based content was noticeably shorter at ImGui's default
        // (~0) button height, leaving it visibly squatter next to Lights even though both columns
        // get the SAME allocated child height. A flat taller button height stretches Camera's real
        // content to use that same room instead of leaving it blank underneath.
        constexpr float kCamBtnH = 31.0f; // 2026-09-23, RedFalcon: 32 was "too tall now" -- caused a scrollbar

        // "Camera" heading (2026-09-23, RedFalcon's mockup) -- this column has no tab label of its
        // own now that it's the LEFT half of the Photo Mode tab (Lights, the right half, keeps its
        // own per-slot "Light N:" headings), so a plain section heading here is what tells the two
        // columns apart at a glance.
        ImGui::TextUnformatted("Camera");
        ImGui::Spacing();

        // Mode buttons: switching directly between the three is always allowed (mirrors
        // BarbieMenu::DrawCameraControls' Full Body/Face View "switching directly over" convention)
        // -- clicking the CURRENTLY active one is what turns it off. Placement/restore both disable
        // the whole row, matching every other camera control in this file.
        ImGui::BeginDisabled(placementActive || MenuStatus::IsRestoring());
        if (ImGui::Button(g_photoCamMode == PhotoCamMode::Tripod ? "Exit##tripod" : "Tripod", ImVec2(thirdW, kCamBtnH)))
        {
            WritePhotoCamModeRequest(g_photoCamMode == PhotoCamMode::Tripod ? "OFF" : "TRIPOD");
        }
        ImGui::SameLine();
        if (ImGui::Button(g_photoCamMode == PhotoCamMode::Selfie ? "Exit##selfie" : "Selfie", ImVec2(thirdW, kCamBtnH)))
        {
            WritePhotoCamModeRequest(g_photoCamMode == PhotoCamMode::Selfie ? "OFF" : "SELFIE");
        }
        ImGui::SameLine();
        if (ImGui::Button(g_photoCamMode == PhotoCamMode::FirstPerson ? "Exit##firstperson" : "First Person", ImVec2(thirdW, kCamBtnH)))
        {
            const bool turningOn = g_photoCamMode != PhotoCamMode::FirstPerson;
            WritePhotoCamModeRequest(turningOn ? "FIRSTPERSON" : "OFF");
            // 2026-09-23, RedFalcon: "first person mode was originally designed for regular view,
            // so it doesnt compensate for the camera change when the menu is open" -- First Person
            // is the ONE camera mode that genuinely needs real mouse-look to be worth anything
            // (Tripod/Selfie/Full Body/Face View are all positioned via button clicks, no mouse
            // input needed), but this companion window steals OS foreground focus the moment it's
            // opened (see StandaloneWindow.cpp's own g_previous_foreground_window comment) -- with
            // this window still focused, mouse movement never reaches the game at all, so turning
            // First Person on left the player unable to look around until they manually clicked back
            // into the game. Same "hand focus back to the game" convention Spawn/Replace already use
            // (SpawnMenu.cpp/BarbieMenu.cpp) -- only on the ON transition; turning it back OFF
            // doesn't need to steal focus back to this window.
            if (turningOn)
            {
                StandaloneWindow::ReturnFocusToGame();
            }
        }
        ImGui::EndDisabled();

        // Reset (2026-09-22, RedFalcon: "sets the camera to what would be the default position, used
        // to fix it after moving it around with the move keys" -- confirmed as a FRESH snapshot from
        // the player's current position, not the original activation spot). ONE PER MODE BUTTON
        // (2026-09-23, matching RedFalcon's own mockup: "Below each view button is a reset button")
        // -- each is only ever meaningful for its OWN mode (there's only one active mode at a time),
        // so only the Reset button directly under whichever mode is CURRENTLY active is enabled; the
        // other two stay greyed rather than silently doing nothing if clicked.
        const bool restoreOrPlacementBlocked = placementActive || MenuStatus::IsRestoring();
        ImGui::BeginDisabled(g_photoCamMode != PhotoCamMode::Tripod || restoreOrPlacementBlocked);
        if (ImGui::Button("Reset##tripod", ImVec2(thirdW, kCamBtnH))) { WritePhotoCamModeRequest("RESET"); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(g_photoCamMode != PhotoCamMode::Selfie || restoreOrPlacementBlocked);
        if (ImGui::Button("Reset##selfie", ImVec2(thirdW, kCamBtnH))) { WritePhotoCamModeRequest("RESET"); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(g_photoCamMode != PhotoCamMode::FirstPerson || restoreOrPlacementBlocked);
        if (ImGui::Button("Reset##firstperson", ImVec2(thirdW, kCamBtnH))) { WritePhotoCamModeRequest("RESET"); }
        ImGui::EndDisabled();

        // "Target Highlight" (2026-09-23, RedFalcon: "Underneath the resets i'd like a button that
        // says 'Toggle Target Highlight'..."; then, same day: gate it to the 3 camera views; then,
        // same day again: "reverse it so enabled is brighter and disabled is dimmed. then we can
        // just say Target Highlight on the button") -- label simplified to the plain state name
        // (no longer "Toggle ..."), and the lit/dim styling INVERTED from the first version -- lit
        // (CheckMark green) now means highlights are ON/showing, dim (default button color) means
        // they're OFF/hidden, matching how a normal "is this feature active" indicator reads (lit =
        // active), the opposite of the original "lit = suppressed" styling. Gated on hasAnyCam
        // (Tripod/Selfie/FirstPerson) -- Spawner.IsHoverHighlightEffectivelySuppressed() (spawner.lua)
        // enforces the SAME rule server-side (highlights always show outside those 3 modes,
        // regardless of the persisted flag's last value), so this is UI-level reinforcement, not the
        // only place it's enforced.
        ImGui::BeginDisabled(!hasAnyCam || MenuStatus::IsRestoring());
        if (!g_targetHighlightSuppressed)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_CheckMark]);
        }
        if (ImGui::Button("Target Highlight", ImVec2(avail, kCamBtnH)))
        {
            WriteHighlightRequest(!g_targetHighlightSuppressed);
        }
        if (!g_targetHighlightSuppressed)
        {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", g_targetHighlightSuppressed
                ? "Target highlights are currently HIDDEN -- click to show them again."
                : "Target highlights are currently shown -- click to hide them for a cleaner photo.");
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Movement pad -- relative to whatever each mode's own LIVE base pose currently is (a
        // static snapshot for Tripod, continuously re-tracked for Selfie -- see
        // Spawner._photoCamApplyPose's header), not raw world axes. Available in all 3 modes now;
        // Rotate is Tripod/Selfie only -- First Person's rotation stays on the mouse (RedFalcon:
        // "positional offset only... rotation already belongs entirely to the mouse"). Precision
        // scales the base step client-side before it's ever queued.
        //
        // Layout REWORKED 2026-09-23 (RedFalcon, from a mockup): a true D-pad cross for Forward/
        // Left/Right/Backward -- SAME "Dummy(cellW) + SameLine + button" recipe MoveMenu.cpp's own
        // Slide cross uses (see that file's Draw(), "Slide: a true D-pad cross") -- with Up/Down/
        // Coords as their own row directly underneath, matching that same Tools tab convention
        // exactly ("We had forward left right and backward in a cross and up down and coords
        // underneath, like on the tools tab"). Coords keeps its own narrower TRIPOD-only gate
        // layered on top of this row's broader hasAnyCam one, same two-gate shape the old standalone
        // Coords button used.
        const float moveStep = kCamMoveStepUU * CAM_PRECISION_SCALES[g_camPrecisionIdx];
        const float rotateStep = kCamRotateStepDeg * CAM_PRECISION_SCALES[g_camPrecisionIdx];
        const float padW = (avail - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
        ImGui::BeginDisabled(!hasAnyCam || placementActive || MenuStatus::IsRestoring());
        {
            ImGui::PushButtonRepeat(true);
            ImGui::Dummy(ImVec2(padW, kCamBtnH));
            ImGui::SameLine();
            if (ImGui::Button("Forward", ImVec2(padW, kCamBtnH))) { QueuePhotoCamMove("forward", moveStep); }

            if (ImGui::Button("Left", ImVec2(padW, kCamBtnH))) { QueuePhotoCamMove("left", moveStep); }
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(padW, kCamBtnH));
            ImGui::SameLine();
            if (ImGui::Button("Right", ImVec2(padW, kCamBtnH))) { QueuePhotoCamMove("right", moveStep); }

            ImGui::Dummy(ImVec2(padW, kCamBtnH));
            ImGui::SameLine();
            if (ImGui::Button("Backward", ImVec2(padW, kCamBtnH))) { QueuePhotoCamMove("back", moveStep); }
            ImGui::PopButtonRepeat();
        }
        ImGui::EndDisabled();

        ImGui::Spacing();

        ImGui::BeginDisabled(!hasAnyCam || placementActive || MenuStatus::IsRestoring());
        ImGui::PushButtonRepeat(true);
        if (ImGui::Button("Up", ImVec2(padW, kCamBtnH))) { QueuePhotoCamMove("up", moveStep); }
        ImGui::SameLine();
        if (ImGui::Button("Down", ImVec2(padW, kCamBtnH))) { QueuePhotoCamMove("down", moveStep); }
        ImGui::PopButtonRepeat();
        ImGui::SameLine();
        // Coords (2026-09-22, RedFalcon: "coords should also act the same as the coords in spawn
        // mode... a button you click on that brings up the ability to set them manually") -- TRIPOD
        // ONLY per the original mockup (Selfie's base re-derives itself every tick regardless of
        // what an absolute set would try to pin it to). Its own narrower gate layers on top of the
        // Up/Down row's broader hasAnyCam one -- BeginDisabled stacks, so First Person/Selfie leave
        // Up/Down clickable while Coords alone stays greyed out.
        ImGui::BeginDisabled(g_photoCamMode != PhotoCamMode::Tripod);
        if (ImGui::Button("Coords", ImVec2(padW, kCamBtnH)))
        {
            OpenPhotoCamCoords();
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();

        // Full 3-axis Rotate (2026-09-23, RedFalcon: "I'd like the rotation X Y and Z like on the
        // tools screen. Sometimes people like to take photos a little crooked so we should be able
        // to rotate in all 3 directions") -- SAME "<-" / axis-letter / "->" per-axis row shape as
        // MoveMenu.cpp's own Rotate section, X/Y/Z = Roll/Pitch/Yaw (Unreal's own FRotator
        // convention, matching that file's axisRow exactly). Roll is genuinely new here (spawner.lua
        // previously only tracked pitch/yaw offset -- see Spawner._photoCamOffsets' own header).
        ImGui::BeginDisabled(!hasTripodOrSelfie || placementActive || MenuStatus::IsRestoring());
        {
            ImGui::TextUnformatted("Rotate");
            ImGui::PushButtonRepeat(true);
            auto axisRow = [&](const char* label, const char* axisKey)
            {
                if (ImGui::Button(("<-##" + std::string(axisKey) + "_l").c_str(), ImVec2(padW, kCamBtnH)))
                {
                    QueuePhotoCamRotate(axisKey, -rotateStep);
                }
                ImGui::SameLine();
                ImGui::BeginDisabled();
                ImGui::Button(label, ImVec2(padW, kCamBtnH));
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button(("->##" + std::string(axisKey) + "_r").c_str(), ImVec2(padW, kCamBtnH)))
                {
                    QueuePhotoCamRotate(axisKey, rotateStep);
                }
            };
            axisRow("X", "roll");
            axisRow("Y", "pitch");
            axisRow("Z", "yaw");
            ImGui::PopButtonRepeat();
        }
        ImGui::EndDisabled();
        if (!hasTripodOrSelfie && hasAnyCam)
        {
            ImGui::TextDisabled("(First Person: rotation is mouse-only)");
        }

        ImGui::Spacing();
        ImGui::Separator();

        // FOV moved ABOVE Precision (2026-09-23, RedFalcon's mockup) -- only meaningful on the
        // tripod's own CameraActor (Spawner.SetTripodFOV), so gated to Tripod/Selfie same as Rotate.
        ImGui::BeginDisabled(!hasTripodOrSelfie || placementActive || MenuStatus::IsRestoring());
        ImGui::TextUnformatted("FOV");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(avail - ImGui::CalcTextSize("FOV").x - ImGui::GetStyle().ItemSpacing.x);
        float fov = g_photoCamFov;
        if (ImGui::SliderFloat("##camfov", &fov, 25.0f, 200.0f, "%.0f"))
        {
            WritePhotoCamFovRequest(fov);
        }
        ImGui::EndDisabled();

        ImGui::TextUnformatted("Precision");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(avail - ImGui::CalcTextSize("Precision").x - ImGui::GetStyle().ItemSpacing.x);
        ImGui::SliderInt("##camprecision", &g_camPrecisionIdx, 0, 5, CAM_PRECISION_LABELS[g_camPrecisionIdx]);
    }

    auto DrawTargetHeader() -> void
    {
        // Extra top padding (2026-09-16, RedFalcon: "the full body button overlaps the tab area a
        // little, so we'll want to move that whole area down a bit to ensure it stays outside the
        // tabs") -- Full Body (the top-most camera control button, further below) is positioned
        // ONE button-row above this function's own "Selected Target:" row; with no buffer at all
        // above that row, Full Body's own top edge landed close enough to the tab bar above it to
        // visibly overlap. This pushes EVERYTHING in this function down by exactly the height Full
        // Body needs, so its top edge clears the tab bar with room to spare.
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y));

        // Same "Selected Target" readout as MoveMenu.cpp -- see MenuStatus.hpp for how this gets
        // here. The whole point of RedFalcon's own request for this: always obvious which NPC an
        // Apply click is about to affect, matching the Tools tab's own target-lock convention.
        // Extracted out of Draw() (2026-09-12, RedFalcon: "let's move selected target to the top of
        // the custom tab") so StandaloneWindow.cpp can call it before BarbieMenu::Draw() runs, while
        // this file's own Body/Hair/cloth-color content still follows further down the tab.

        // Read Current also lives here now (2026-09-12, RedFalcon: "put the read current at the top
        // to the right of the target window") -- MOVED from its old spot just above the cloth-color
        // panel. Poll for a pending read's result once per frame, right here, since this is the
        // first thing drawn every frame regardless of which panel below actually owns a given piece
        // of state it populates (cloth colors, Physique, hair colors -- see pollReadCurrentResult's
        // own comment for the full "other than body type/mesh" scope RedFalcon asked for).
        pollReadCurrentResult();
        // Clothes unlock-state mirror (2026-09-14) -- same "once per frame regardless of which
        // panel below owns it" reasoning as pollReadCurrentResult above.
        pollClothesUnlockState();

        // Detect gate: target-change tracking (2026-09-14) -- see g_hasDetected's own header
        // comment. MenuStatus::TargetId() (not TargetLabel()) is the stable identity check.
        // Full view reset on UNSELECT specifically (2026-09-18, RedFalcon: "can we reset the
        // custom view when a target is unselected?") -- a genuinely different target (swapped,
        // not unselected) still just gets g_hasDetected re-armed as before, since the next Read
        // Current naturally overwrites every dropdown anyway; only the empty-targetId case (lock
        // fully released) calls ResetCustomViewState(), since nothing will repopulate it otherwise
        // and the view would keep showing the previous target's stale selections indefinitely.
        {
            const std::string& targetId = MenuStatus::TargetId();
            if (targetId != g_lastDetectTargetId)
            {
                const bool wasUnselected = targetId.empty() && !g_lastDetectTargetId.empty();
                g_lastDetectTargetId = targetId;
                g_hasDetected = false;
                if (wasUnselected)
                {
                    ResetCustomViewState();
                }
            }
        }
        // Barbie auto-detect (2026-09-14) -- see BARBIE_SPAWN_DONE_PATH's own header comment.
        // Cheap ifstream open, same "most frames find nothing" cost as every other status poll in
        // this bridge.
        {
            std::ifstream doneFile(BARBIE_SPAWN_DONE_PATH);
            if (doneFile)
            {
                doneFile.close();
                std::remove(BARBIE_SPAWN_DONE_PATH);
                requestReadCurrent();
            }
        }

        const bool hasTarget = !MenuStatus::TargetLabel().empty();
        constexpr float kReadBtnW = 150.0f;
        constexpr float kRowH = 28.0f;
        // Camera controls now live here too (2026-09-16, RedFalcon: "Move the camera buttons to the
        // right of the target window. Keep them the size and order they are now, so 'Face View'
        // would be aligned with 'Read Current'. No need for a camera label.") --
        // BarbieMenu::DrawCameraControls() draws Full Body then Face View stacked (each its own
        // frame-height row), so reserving `kCameraW` (matching that function's own button width,
        // kept in sync by hand -- BarbieMenu.cpp's own kPreviewSize) off the right edge here, then
        // jumping the cursor back up one button-row before drawing it, lands Face View (the SECOND
        // stacked button) exactly on Read Current's own row.
        constexpr float kCameraW = 110.0f; // BarbieMenu::kPreviewSize
        // "+"/"-" button (2026-09-14, RedFalcon: "next to the target text boxes, let's put a +
        // button that recreates pressing num +, and make it look like - when a target is selected
        // to imply untargeting... make sure the final result is the same width as just the target
        // box before") -- square, matches the row's own height, sits between the box and Read
        // Current; the box itself shrinks by this button's width + one extra gap so the combined
        // total (box + lock button + Read Current) still equals `avail`, same as the box+Read
        // Current combo alone did before this button existed.
        constexpr float kLockBtnW = kRowH;
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float avail = ImGui::GetContentRegionAvail().x - kCameraW - gap;
        const float boxW = avail - kReadBtnW - kLockBtnW - gap * 2.0f;
        const float cameraX = ImGui::GetCursorPosX() + avail + gap;

        // "Save Customizations" (2026-09-16) -- own row, directly ABOVE "Selected Target:"/Read
        // Current (RedFalcon: "I'd put the save button above Read Current"), same width as the
        // target-box+lock+Read-Current combo below it (`avail`, already computed above) so it
        // lines up edge-to-edge rather than stretching into the camera-control zone on the right.
        // Same gate as everything else in this tab -- it re-reads the target's FULL state itself
        // (Spawner.BuildCustomStateLines), independent of whatever this UI currently has cached,
        // but requiring a prior Read Current keeps the "detect before you touch it" convention
        // consistent rather than carving out an exception for this one button.
        ImGui::BeginDisabled(MenuStatus::IsRestoring() || !hasTarget || !g_hasDetected); // re-enabled 2026-09-18, see g_hasDetected's own header
        if (ImGui::Button("Save Customizations", ImVec2(avail, kRowH)))
        {
            WriteSaveCustomizationsRequest();
        }
        ImGui::EndDisabled();
        HoverTooltip("Save this target's current body/hair/clothes/belts/accessories/lantern/AI-toggle/pose "
                     "so it survives a world reload. Only works on a target placed via the Spawn Menu "
                     "(Barbie/statue/walker) -- a genuinely native NPC has nothing to key the save by.");

        ImGui::TextUnformatted("Selected Target:");
        const float targetRowY = ImGui::GetCursorPosY();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBgHovered]);
        ImGui::BeginChild("##custom_target_label", ImVec2(boxW, kRowH), true, ImGuiWindowFlags_NoScrollbar);
        DrawTruncatedText(MenuStatus::TargetLabel(), boxW - 16.0f);
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::Button(hasTarget ? "-" : "+", ImVec2(kLockBtnW, kRowH)))
        {
            WriteTargetLockToggleAction();
        }
        HoverTooltip(hasTarget ? "Release target lock" : "Lock the currently hovered target");

        ImGui::SameLine();
        // Non-character gate (2026-09-21, RedFalcon: "keep the detect button disabled for any non
        // character objects. The animals and decor should not be able to have their customizations
        // scanned... same for monsterous as well") -- MenuStatus::TargetIsCharacter() is true only
        // when the target has a real CompositeMeshComponent, the same signal TargetSex() already
        // relies on for "is this actually a customizable character." Everything downstream (Save
        // Customizations, the Accessories/Clothes sections) already gates on g_hasDetected, which can
        // now only become true via a successful Read Current -- so disabling this one button is
        // enough to cover the whole tab for non-character targets, no separate gates needed elsewhere.
        const bool isNonCharacterTarget = hasTarget && !MenuStatus::TargetIsCharacter();
        ImGui::BeginDisabled(MenuStatus::IsRestoring() || !hasTarget || isNonCharacterTarget);
        if (ImGui::Button(g_readPending ? "Reading..." : "Read Current", ImVec2(kReadBtnW, kRowH)))
        {
            requestReadCurrent();
        }
        ImGui::EndDisabled();
        HoverTooltip(isNonCharacterTarget
            ? "This target has no customizable body/hair/clothes (animal, decor, or creature) -- nothing here to detect."
            : "Populate Physique, Hair colors, and every cloth swatch below from the target's "
              "OWN current values (not its body type or mesh selections).");

        ImGui::TextDisabled("Press Num + (in-game, or from the Tools tab) to select/release target lock");

        // `targetRowY` (captured right after "Selected Target:" text, above) is already Read
        // Current's own row Y -- Full Body sits one button-row ABOVE that; Face View (drawn right
        // after it inside DrawCameraControls' own group, via the ordinary automatic newline) then
        // lands exactly on Read Current's row.
        //
        // Cursor explicitly restored afterward (2026-09-16 fix) -- BeginGroup/EndGroup only restores
        // the OUTER cursor to right after wherever BeginGroup was itself CALLED, which here is the
        // jumped-to camera position, not this function's own normal "next line" flow. Without this,
        // whatever StandaloneWindow.cpp draws right after calling DrawTargetHeader() (the new
        // scrolling child, see its own comment) would inherit the camera group's cursor position
        // instead of continuing on cleanly from the hint text above.
        {
            const ImVec2 afterHintPos = ImGui::GetCursorPos();
            const float cameraTopY = targetRowY - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.y;
            ImGui::SetCursorPos(ImVec2(cameraX, cameraTopY));
            ImGui::BeginGroup();
            BarbieMenu::DrawCameraControls();
            ImGui::EndGroup();
            ImGui::SetCursorPos(afterHintPos);
        }
    }

    // Externally-linked wrapper (2026-09-21, RedFalcon: "I want a new Photo Mode tab to have the
    // camera and lights in it") -- DrawLightsSectionImpl lives in the anonymous namespace above
    // (internal linkage, same as everything else that section's own state/polling needs), so a
    // plain outer-scope forwarder is what StandaloneWindow.cpp's new "Photo Mode" tab actually
    // calls -- same pattern DrawTargetHeader's own extraction already established.
    auto DrawLightsSection() -> void
    {
        ImGui::BeginDisabled(MenuStatus::IsRestoring());
        DrawLightsSectionImpl();
        ImGui::EndDisabled();
    }

    // Same externally-linked forwarder pattern as DrawLightsSection just above, for the Camera
    // section (2026-09-22) -- DrawCameraSectionImpl already handles its own per-control
    // BeginDisabled/EndDisabled scoping (placement/restoring gates differ per row), so this wrapper
    // doesn't wrap the whole thing in one, unlike DrawLightsSection.
    auto DrawCameraSection() -> void
    {
        DrawCameraSectionImpl();
    }

    // The Coords popup is a secondary window (own Begin()/End(), no-op when closed) meant to be
    // called unconditionally every frame regardless of which tab is active -- same convention as
    // CoordsMenu::Draw() (StandaloneWindow.cpp calls both right after the main tab bar's End()).
    auto DrawCameraCoordsPopup() -> void
    {
        DrawPhotoCamCoordsPopupImpl();
    }

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

        // pollReadCurrentResult() now runs inside DrawTargetHeader() (2026-09-12), which
        // StandaloneWindow.cpp calls before this function every frame -- not duplicated here.

        const bool hasTarget = !MenuStatus::TargetLabel().empty();
        const bool isFemale = MenuStatus::TargetSex() == "F";

        // Detect gate hint (2026-09-14) -- shown whenever a target IS locked but hasn't had a
        // completed Read Current yet, distinct from the existing "target-lock something first" hint
        // at the bottom of this function (that one covers !hasTarget; this covers hasTarget but
        // !g_hasDetected). Drawn OUTSIDE every BeginDisabled block below so it's always legible.
        if (hasTarget && !g_hasDetected && !MenuStatus::IsRestoring())
        {
            ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.25f, 1.0f),
                "Click \"Read Current\" above to detect this target before making changes.");
            ImGui::Spacing();
        }

        // ==== "Body" section (2026-09-12, RedFalcon: "where selected target used to be add a
        // 'Body' section with Physique in it. to the right of physique put a 'Make Ghost' button")
        // ==== Reordered 2026-09-14 (RedFalcon: "move Physique down a slot, so in line with 'Not
        // Reversible'... put in place of where Physique was a Skin Tone Swatch Selector") -- Skin
        // Tone now takes the row Physique used to occupy (with Make Ghost beside it, unchanged from
        // before); Physique moves down one row -- "Not reversible" is STILL positioned via the same
        // captured makeGhostX trick as always (a caption for Make Ghost, one row up), it just now
        // physically lands on Physique's row as a side effect of Physique being the row directly
        // below Make Ghost's, rather than a row of its own. kBodyControlW/kBodyLabelColW are shared
        // by all 3 rows (RedFalcon: "the dropdown left edge to start at the same spot as the left
        // edge of the color swatch for eye"/"eye color swatch to be the same width as the physique
        // drop down") -- Physique's own original width (140) is the reference every other control
        // now matches.
        // ==== Wrapped in a CollapsingHeader, shown by default (2026-09-16, RedFalcon: "Make each
        // section after target a windowshade as well... Second section is = Body - Show by default")
        // -- also folds in Hair (no separate header of its own any more, just a plain sub-label, no
        // rule line -- RedFalcon: "Get rid of the lines between sections").
        if (ImGui::CollapsingHeader("Body", ImGuiTreeNodeFlags_DefaultOpen))
        {
        ImGui::BeginDisabled(MenuStatus::IsRestoring() || !hasTarget || !g_hasDetected); // re-enabled 2026-09-18, see g_hasDetected's own header

        constexpr float kBodyLabelColW = 86.0f; // fits "Skin Tone"/"Eye Color" (9 chars) without crowding
        const float bodyStartX = ImGui::GetCursorPosX();
        // 4 equal, WINDOW-WIDTH-RESIZING columns (2026-09-16, RedFalcon: "treat it the same as the
        // belt and straps row, where it's all evenly distributed and in a row, nothing aligned right,
        // and they all fit in the space and resize as the window is adjusted") -- same
        // `kColW = GetContentRegionAvail().x / 4.0f` + `kGap` + `SameLine(baseX + i * kColW)` recipe
        // DrawBeltsAndStraps' own Belt/Sling/Strap/Frog row already uses (see that row's own header
        // comment for why this is `GetContentRegionAvail()`, read fresh every frame, rather than a
        // fixed pixel width -- that's what makes every control below actually shrink/grow as the
        // window is resized, not just sit at 4 fixed evenly-spaced START positions with fixed-width
        // controls, which was this section's first, still-not-resizing attempt).
        const float kBodyColW = ImGui::GetContentRegionAvail().x / 4.0f;
        const float kBodyGap = 10.0f; // same margin value as kGap in DrawBeltsAndStraps
        const float kBodySwatchW = kBodyColW - kBodyLabelColW - kBodyGap; // Skin Tone/Eye Color/Physique control width
        const float kBodyButtonW = kBodyColW - kBodyGap; // Toggle AI/Make Ghost fill their own column

        // --- Row 1: Skin Tone + Eye Color + Toggle AI + Make Ghost, 4 equal resizing columns ---
        // Eye Color MOVED up onto this same row (2026-09-16, RedFalcon: "Move 'Eye Color' up to the
        // right of 'Skin Tone'") -- was its own standalone row below Physique; internals unchanged,
        // just relocated.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Skin Tone");
        ImGui::SameLine(bodyStartX + kBodyLabelColW);
        if (FlatSwatchButton("##skintoneswatch", g_skinToneIdx, ImVec2(kBodySwatchW, ImGui::GetFrameHeight()), kSkinTones, kSkinToneCount))
        {
            ImGui::OpenPopup("##skintone_picker");
        }
        if (g_skinToneIdx >= 0)
        {
            HoverTooltip(kSkinTones[g_skinToneIdx].name);
        }
        else
        {
            HoverTooltip("Click to choose a skin tone (default: matches the body mesh/Physique)");
        }
        DrawFlatColorPickerPopup("##skintone_picker", g_skinToneIdx, kSkinTones, kSkinToneCount);
        if (g_skinToneIdx >= 0 && g_skinToneIdx != g_skinToneLastWritten)
        {
            g_skinToneLastWritten = g_skinToneIdx;
            WriteSkinToneRequest(kSkinTones[g_skinToneIdx].name);
        }

        ImGui::SameLine(bodyStartX + kBodyColW);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Eye Color");
        ImGui::SameLine(bodyStartX + kBodyColW + kBodyLabelColW);
        if (FlatSwatchButton("##eyecolorswatch", g_eyeColorIdx, ImVec2(kBodySwatchW, ImGui::GetFrameHeight()), kEyeColors, kEyeColorCount))
        {
            ImGui::OpenPopup("##eye_color_picker");
        }
        if (g_eyeColorIdx >= 0)
        {
            HoverTooltip(kEyeColors[g_eyeColorIdx].name);
        }
        else
        {
            HoverTooltip("Click to choose an eye color");
        }
        DrawFlatColorPickerPopup("##eye_color_picker", g_eyeColorIdx, kEyeColors, kEyeColorCount);
        if (g_eyeColorIdx >= 0 && g_eyeColorIdx != g_eyeColorLastWritten)
        {
            g_eyeColorLastWritten = g_eyeColorIdx;
            WriteEyeColorRequest(g_eyeColorIdx, kEyeColorGlowingIdx);
        }

        // "Make Ghost" owns column 3, the row's 4th equal slice -- its own X is still captured so
        // "Not reversible" below can align under it.
        const float makeGhostX = bodyStartX + 3.0f * kBodyColW;

        // "Toggle AI" owns column 2, immediately left of Make Ghost (2026-09-16, RedFalcon:
        // "move disable AI to the left of the ghost button instead" -- was its own row underneath
        // "Not reversible", moved here same day). Reflects/toggles `g_aiDisabled` (persistent
        // per-DLL-lifetime, same convention as g_lanternOn etc); label shows the ACTION the click
        // will take, matching Full Body/Face View/Lantern's own convention elsewhere in this bridge.
        ImGui::SameLine(bodyStartX + 2.0f * kBodyColW);
        // Greyed out for a statue/decor target (2026-09-16, RedFalcon: "set the ai toggle option to
        // be disabled if a target is a statue or decor") -- neither ever has a real AIController for
        // Spawner.SetAILogic's StartLogic/StopLogic to act on, so the button would otherwise silently
        // no-op. Nested inside this section's own outer BeginDisabled (IsRestoring/!hasTarget/
        // !g_hasDetected) rather than folded into it, since THAT one still needs Make Ghost/Skin
        // Tone/etc to stay interactive for a statue -- only this one button cares about the distinction.
        const bool aiToggleDisabled = MenuStatus::TargetIsStatic();
        ImGui::BeginDisabled(aiToggleDisabled);
        if (ImGui::Button(g_aiDisabled ? "Enable AI" : "Disable AI", ImVec2(kBodyButtonW, 0.0f)))
        {
            g_aiDisabled = !g_aiDisabled;
            WriteAIToggleRequest(!g_aiDisabled);
        }
        ImGui::EndDisabled();
        HoverTooltip(aiToggleDisabled
            ? "Statues and decor have no AI to toggle."
            : "Starts/stops this target's own AI logic (StopLogic/StartLogic) -- reversible, unlike Make Ghost.");

        ImGui::SameLine(makeGhostX);
        // Distinct red styling so this button doesn't read as just another normal action -- RedFalcon:
        // "call out it's not reversable."
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.10f, 0.10f, 1.0f));
        if (ImGui::Button("Make Ghost", ImVec2(kBodyButtonW, 0.0f)))
        {
            WriteMakeGhostRequest();
        }
        ImGui::PopStyleColor(3);
        HoverTooltip("Permanently reskins the target as a ghost (skin/hair/armor materials + skeleton mesh). NOT REVERSIBLE.");

        // --- Row 2: Physique + "Not reversible" (same line, at makeGhostX) ---
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Physique");
        ImGui::SameLine(bodyStartX + kBodyLabelColW);
        ImGui::SetNextItemWidth(kBodySwatchW);
        {
            const char* currentPhysique = (g_physique_index >= 0) ? kPhysiqueOptions[g_physique_index].label : "(select)";
            if (ImGui::BeginCombo("##physique", currentPhysique))
            {
                for (int i = 0; i < static_cast<int>(std::size(kPhysiqueOptions)); ++i)
                {
                    const bool selected = (g_physique_index == i);
                    if (ImGui::Selectable(kPhysiqueOptions[i].label, selected))
                    {
                        g_physique_index = i;
                        WritePhysiqueRequest(kPhysiqueOptions[i].lua_size);
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::SameLine(makeGhostX);
        ImGui::TextColored(ImVec4(0.80f, 0.35f, 0.35f, 1.0f), "Not reversible");

        // --- Row 3: Height (2026-09-18, RedFalcon's height-slider idea, 3ft-8ft range) ---
        // Ground-compensated on the Lua side (CS.setActorScaleGrounded) so dragging this doesn't
        // sink the target's feet into the floor or lift them off it -- see that function's own
        // header comment (spawner.lua) for the bounds-based math.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Height");
        ImGui::SameLine(bodyStartX + kBodyLabelColW);
        ImGui::SetNextItemWidth(kBodySwatchW);
        if (ImGui::SliderFloat("##height", &g_heightFeet, 3.0f, 8.0f, "%.2f ft"))
        {
            WriteHeightRequest(g_heightFeet);
        }
        HoverTooltip("Uniform actor scale, expressed as feet (3'-8'). Anchored on LongBen (1.2 scale = 7ft) and Marita (0.95 scale = 5ft) -- scale 1.0 = 5'10\".");

        ImGui::EndDisabled(); // IsRestoring() || !hasTarget
        } // CollapsingHeader("Body")

        // ==== "Hair" section (2026-09-12, RedFalcon's Other/HairCategories.xlsx spec), own
        // CollapsingHeader (2026-09-16, RedFalcon: "i forgot about Hair between Body and Clothes,
        // make that a window shade too") -- shown by default, same as Body/Clothes/Belts and Straps.
        if (ImGui::CollapsingHeader("Hair", ImGuiTreeNodeFlags_DefaultOpen))
        {
        ImGui::BeginDisabled(MenuStatus::IsRestoring() || !hasTarget || !g_hasDetected); // re-enabled 2026-09-18, see g_hasDetected's own header

        // One shared width for every dropdown in this section (2026-09-12, RedFalcon: "make all the
        // drop downs the same width so they line up better") -- Hair used to be wider (220) than
        // the 4 facial rows (150) since it sits alone on its own line with more room to spare; now
        // uniform so the whole section reads as one consistent column of controls. kHairLabelColW is
        // the second half of "line up" (RedFalcon reiterated it was still crooked after just fixing
        // the width) -- see DrawHairRow's own comment for why only the first-of-line rows get it.
        constexpr float kHairDropdownW = 150.0f;
        constexpr float kHairLabelColW = 78.0f; // fits "Whiskers"/"Mustache" without crowding
        constexpr float kHairLabelColW2 = 66.0f; // fits "Mustache"/"Eyebrows" without crowding

        // Hair itself is never sex-gated (RedFalcon: "all hair works on women's heads, so if there
        // isnt a woman mesh, just use the male") -- always enabled whenever a target is locked.
        // Eyebrows (2026-09-14, RedFalcon: "add an eyebrows dropdown and color next to Hair,
        // matching mustache and beard below so it's symmetric") sits in the second column of this
        // SAME line, also never sex-gated (real Female eyebrow art exists, unlike Beard/Mustache/
        // Whiskers) -- so it's drawn here, OUTSIDE the isFemale BeginDisabled block below, not folded
        // into "Facial Hair". col2X is measured once, right after Hair's own row (identical total
        // width to Sets'/Whiskers' rows further down, all three share kHairDropdownW/kHairLabelColW),
        // and reused for every second-column row in this whole section (Eyebrows here, Mustache/
        // Beard below) so they all line up at the same absolute X.
        DrawHairRow(g_hairRow, kHairDropdownW, kHairLabelColW);
        ImGui::SameLine();
        ImGui::Spacing();
        ImGui::SameLine();
        const float col2X = ImGui::GetCursorPosX();
        DrawHairRow(g_eyebrowsRow, kHairDropdownW, col2X + kHairLabelColW2);

        ImGui::Spacing();
        ImGui::TextUnformatted("Facial Hair");
        // Facial hair (Sets/Mustache/Whiskers/Beard) IS sex-gated (RedFalcon: "no need to facial
        // hair for women so we will disable it on a selected female").
        ImGui::BeginDisabled(isFemale);
        DrawHairRow(g_setsRow, kHairDropdownW, kHairLabelColW);
        ImGui::SameLine();
        ImGui::Spacing();
        ImGui::SameLine();
        DrawHairRow(g_mustacheRow, kHairDropdownW, col2X + kHairLabelColW2);

        DrawHairRow(g_whiskersRow, kHairDropdownW, kHairLabelColW);
        ImGui::SameLine();
        ImGui::Spacing();
        ImGui::SameLine();
        DrawHairRow(g_beardRow, kHairDropdownW, col2X + kHairLabelColW2);
        ImGui::EndDisabled(); // isFemale
        if (isFemale)
        {
            ImGui::TextDisabled("Facial hair disabled -- target is female.");
        }

        ImGui::EndDisabled(); // IsRestoring() || !hasTarget
        } // CollapsingHeader("Hair")

        // ==== "Clothes" section (2026-09-16: given its own CollapsingHeader + name -- RedFalcon:
        // "Make each section after target a windowshade as well... Third section = Clothes - Show
        // by default"; this panel previously had no heading of its own at all). No separator line
        // before it any more either (RedFalcon: "Get rid of the lines between sections").
        // Read Current button itself MOVED to DrawTargetHeader() (2026-09-12, RedFalcon: "put the
        // read current at the top to the right of the target window") -- this panel still gets
        // populated by it, just no longer draws its own copy of the button.
        // Detect-gated too (2026-09-14) -- same g_hasDetected requirement as the Body/Hair section
        // above; see its own header comment.
        if (ImGui::CollapsingHeader("Clothes", ImGuiTreeNodeFlags_DefaultOpen))
        {
        ImGui::BeginDisabled(MenuStatus::IsRestoring() || !hasTarget || !g_hasDetected); // re-enabled 2026-09-18, see g_hasDetected's own header

        constexpr float kSwatchW = 75.0f; // half of the original 150 (RedFalcon, 2026-09-08)
        constexpr float kSwatchH = 26.0f;
        constexpr float kLabelW = 70.0f;
        constexpr float kClothesDropdownW = 180.0f;
        const float swatchGap = ImGui::GetStyle().ItemSpacing.x;

        // "Outfit" row (2026-09-14, RedFalcon: "an additional 'Outfits' type with no colors" --
        // widened same day with its own 3 "apply to all" swatches + red X, see the function's own
        // header comment) -- standalone, drawn once above the per-category loop.
        DrawClothesOutfitRow(kClothesDropdownW, kLabelW, kSwatchW, kSwatchH, swatchGap);

        for (int i = 0; i < kCategoryCount; ++i)
        {
            const Category& cat = kCategories[i];
            ImGui::PushID(i);

            // Greyed out when this target has no swappable slot here at all (2026-09-16, RedFalcon:
            // "if a target doesnt have a swappable item in the hat slot, Grey out the hat selectors.
            // If they dont have a swapable belt, grey out the belt selector") -- same
            // BeginDisabled(!visible) convention DrawBeltsAndStraps already uses for Belt/Sling/
            // Strap/Frog, driven here by g_clothesSlotAvailable (a real BuildedCompositeMeshes entry
            // for this BodyPart, or not -- Spawner.SetBodyPartMesh can never swap a slot that doesn't
            // exist). The label itself stays outside this disable so the row is still legible while
            // greyed.
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(cat.label);
            ImGui::SameLine(kLabelW);
            ImGui::BeginDisabled(!g_clothesSlotAvailable[i]);

            // Item-name dropdown (2026-09-14, RedFalcon: "I want each body category in it's own
            // dropdown") -- sits between the label and the color swatches, same row.
            DrawClothesItemCombo(i, kClothesDropdownW);
            ImGui::SameLine(0.0f, swatchGap);

            // Always reserve all 3 swatch-width positions (2026-09-16, RedFalcon: "Align the waist
            // and cape X with the others in the group") -- a 1-slot category (Waist/Cape) used to
            // draw only its own single swatch and place its red X right after it, landing well
            // short of where a 3-slot category's own X sits. Looping 0..3 unconditionally and
            // drawing an invisible `Dummy` for the 2 positions a 1-slot category doesn't use keeps
            // every row's X at the SAME absolute column regardless of slotCount.
            for (int s = 0; s < 3; ++s)
            {
                if (s > 0)
                {
                    ImGui::SameLine(0.0f, swatchGap);
                }
                ImGui::PushID(s);
                const bool realSlot = (cat.slotCount == 3) || (s == cat.displayCol);
                if (!realSlot)
                {
                    ImGui::Dummy(ImVec2(kSwatchW, kSwatchH));
                    ImGui::PopID();
                    continue;
                }
                const int slotIdx = (cat.slotCount == 1) ? 0 : s;
                if (GradientSwatchButton("row_swatch", g_selected[i][slotIdx], ImVec2(kSwatchW, kSwatchH)))
                {
                    ImGui::OpenPopup("##color_picker");
                }
                if (g_selected[i][slotIdx] >= 0)
                {
                    HoverTooltip(kClothColors[g_selected[i][slotIdx]].name);
                }
                else
                {
                    HoverTooltip("Click to choose a color");
                }
                DrawColorPickerPopup("##color_picker", g_selected[i][slotIdx]);
                // Realtime apply (2026-09-14, RedFalcon: "i also want clothing color to be real
                // time") -- fires the SAME writeColorRequest the old Apply button used to, but
                // immediately on change instead of waiting for a click. writeColorRequest sends the
                // FULL current g_selected state every time (not just this one slot), so resending
                // it for a single slot's change is safe/idempotent for every other already-applied
                // row.
                if (g_selected[i][slotIdx] != g_lastWrittenClothColor[i][slotIdx])
                {
                    g_lastWrittenClothColor[i][slotIdx] = g_selected[i][slotIdx];
                    writeColorRequest();
                }
                ImGui::PopID();
            }

            // Red "X" (2026-09-14) -- replaces the old "(Remove)" dropdown entry (was index 0 of
            // this row's own item combo). Resets both the item selection AND (Legs/Torso only,
            // mirroring Spawner.RemoveClothesItem's own underwear-substitution logic server-side)
            // whatever the resulting equipped item turns out to be, via the same
            // requestReadCurrent() the combo's own picks already trigger.
            ImGui::SameLine(0.0f, swatchGap);
            if (RemoveXButton("##clothes_remove"))
            {
                g_clothesSelected[i] = -1;
                WriteClothesItemRequest(cat.luaBodyPart, "(Remove)");
                requestReadCurrent();
            }
            HoverTooltip("Remove");

            ImGui::EndDisabled(); // !g_clothesSlotAvailable[i]
            ImGui::PopID();
        }
        ImGui::EndDisabled(); // IsRestoring() || !hasTarget
        } // CollapsingHeader("Clothes")

        // "Belts and Straps" section (2026-09-15), own CollapsingHeader (2026-09-16, RedFalcon:
        // "Fourth Section = Belts and Straps - Show by default") -- same target/detect gate as
        // Clothes above it.
        if (ImGui::CollapsingHeader("Belts and Straps", ImGuiTreeNodeFlags_DefaultOpen))
        {
        ImGui::BeginDisabled(MenuStatus::IsRestoring() || !hasTarget || !g_hasDetected); // re-enabled 2026-09-18, see g_hasDetected's own header
        DrawBeltsAndStraps();
        ImGui::EndDisabled();
        }

        // "Poses and Actions" section (2026-09-16, RedFalcon: "next up lets bring over the poses").
        // Same target/detect gate as Belts and Straps above -- the current-pose readout and Hand
        // dropdowns are only meaningful once a Read Current has actually populated them, though the
        // pose tree's own "+" buttons would work regardless (same "target-locked" requirement the
        // Tools tab's own Spawn/Replace already has).
        if (ImGui::CollapsingHeader("Poses and Actions", ImGuiTreeNodeFlags_DefaultOpen))
        {
        ImGui::BeginDisabled(MenuStatus::IsRestoring() || !hasTarget || !g_hasDetected); // re-enabled 2026-09-18, see g_hasDetected's own header
        DrawPosesAndActions();
        ImGui::EndDisabled();
        }

        if (!hasTarget)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Target-lock something first (Num +) to enable this panel.");
        }
    }
} // namespace RC::LivingBaseSpawnMenu::CustomMenu
