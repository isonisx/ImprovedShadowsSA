#include "StaticVehicleShadows.h"

#include <plugin.h>
#include <cstdint>
#include <windows.h>

namespace
{
    bool __cdecl IgnoreVehicleShadowQualityGate()
    {
        return false;
    }
}

void InstallStaticVehicleShadows()
{
    using namespace plugin;
    constexpr std::uintptr_t storeShadowForVehicle = 0x70BDA0;
    constexpr std::uintptr_t graphicsHighQuality = 0x70F9B0;
    const auto expectedTarget = reinterpret_cast<void*>(GetGlobalAddress(graphicsHighQuality));

    // Bypass only the quality-based early exit in StoreShadowForVehicle.
    for (auto address = storeShadowForVehicle; address + 5 <= storeShadowForVehicle + 0x40; ++address)
    {
        if (patch::TranslateCallOffset<void*>(address) == expectedTarget)
        {
            patch::RedirectCall(address, IgnoreVehicleShadowQualityGate);
            return;
        }
    }

    OutputDebugStringA("ImprovedShadowsSA: vehicle-shadow quality check not found; static vehicle shadow patch skipped.\n");
}
