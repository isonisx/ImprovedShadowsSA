#include <plugin.h>
#include <windows.h>

#include "StencilShadows.h"

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

        InstallImprovedStencilShadows();
    }
} gInstance;
