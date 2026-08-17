#pragma once

// LivingBaseSpawnMenu: UE4SS mod entry point.
//
// Renders via UE4SS's own register_tab console window -- see the long comment at the top of
// SpawnMenuMod.cpp for why (pivoted away from a raw D3D12 Present-hook overlay after confirming
// it conflicts with Windrose's NVIDIA Streamline DLSS-G swapchain wrapper).

#include <Mod/CppUserModBase.hpp>

namespace RC::LivingBaseSpawnMenu
{
    class SpawnMenuMod : public CppUserModBase
    {
      public:
        SpawnMenuMod();

        auto on_unreal_init() -> void override;
        auto on_update() -> void override;

      private:
        bool m_logged_first_update{};
    };
} // namespace RC::LivingBaseSpawnMenu
