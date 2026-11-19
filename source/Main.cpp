#include <plugin.h>
#include <windows.h>

#include "Settings.h"
#include "StencilShadows.h"
#include "StaticVehicleShadows.h"

using namespace plugin;

struct Main
{
    Main()
    {
        if (!IsGameVersion10us())
        {
            OutputDebugStringA("ImprovedShadowsSA: GTA SA 1.0 US is required.\n");
            return;
        }

        const Settings settings = LoadSettings();
        InstallImprovedStencilShadows(settings.shadowColorDivisor, settings.shadowOpacity);
        if (settings.forceStaticVehicleShadows)
            InstallStaticVehicleShadows();
    }
} gInstance;
