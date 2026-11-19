#pragma once

struct Settings
{
    float shadowColorDivisor = 8.0f;
    int shadowOpacity = 100;
    bool forceStaticVehicleShadows = false;
};

Settings LoadSettings();
