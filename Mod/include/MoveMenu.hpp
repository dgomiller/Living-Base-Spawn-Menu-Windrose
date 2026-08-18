#pragma once

// MoveMenu: a side panel of held-repeat buttons for nudging the currently targeted (or locked)
// object -- the concrete fix for LivingBase's documented "slow to nudge via repeated key presses"
// problem (many keydown events never reach the mod at all in this UE4SS build; a UI button held
// down has no such drop rate). Also carries: a live "Selected Target" readout (reads
// MenuStatus::TargetLabel(), truncated with a full-text hover tooltip), an In-Game Keys toggle
// (reads MenuStatus::IsEnabled(), mirrors the in-game Insert key -- but only gates LivingBase's
// OWN keyboard keys, not this panel's own buttons; see its own comment in MoveMenu.cpp), full
// 3-axis rotation (X/Y/Z = Roll/Pitch/Yaw, one row each), the precision slider, Despawn/Undo, and
// Delete All (behind its own confirmation popup). Sends named ACTIONS (e.g. "UP", "ROTZ_L",
// "ACTION:TOGGLE_ENABLE") or a raw precision scale, never raw
// distances/angles for the spatial ones -- main.lua's own Config.LIVE_EDIT_*_STEP values stay the
// single source of truth for step sizes, same division of responsibility as SpawnMenu (this side
// only ever signals intent, Lua still owns what it means). MenuStatus.hpp is the one place this
// mod reads state BACK from Lua, rather than only ever sending requests.

namespace RC::LivingBaseSpawnMenu::MoveMenu
{
    // Draws the held-repeat button grid into the CURRENT ImGui window/column -- call this from
    // inside an existing ImGui::Begin()/End() pair, same as SpawnMenu::Draw().
    auto Draw() -> void;
} // namespace RC::LivingBaseSpawnMenu::MoveMenu
