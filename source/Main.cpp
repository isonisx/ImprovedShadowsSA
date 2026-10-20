#include <plugin.h>
#include <windows.h>

using namespace plugin;

struct Main
{
    Main()
    {
        if (!IsGameVersion10us())
        {
            OutputDebugStringA("SA.ImprovedShadows: GTA SA 1.0 US is required.\n");
            return;
        }
    }
} gInstance;
