// LivingBaseSpawnMenu: Windows DLL entry point (UE4SS loads this module).
//
// The actual mod logic lives in SpawnMenuMod. This file exists to satisfy the DLL entry
// requirements UE4SS expects from a C++ mod.

#include <SpawnMenuMod.hpp>

extern "C"
{
    __declspec(dllexport) RC::CppUserModBase* start_mod()
    {
        return new RC::LivingBaseSpawnMenu::SpawnMenuMod();
    }

    __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
