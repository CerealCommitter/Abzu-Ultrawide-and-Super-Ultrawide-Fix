#pragma once
#include <windows.h>

// Settings stored in AbzuFix.ini next to the DLL.
struct FixSettings {
    bool enabled = true;      // FOV fix on/off
    bool log = false;
    bool showWindow = true;   // show the settings window at every launch
    int resX = 0, resY = 0;   // 0 = native (current desktop resolution)
    wchar_t aspect[32] = L"0";  // AspectRatio: "0" = full screen, or "21:9", "16:9", "2.37"...
};

void LoadFixSettings(const wchar_t* iniPath, FixSettings& s);
void SaveFixSettings(const wchar_t* iniPath, const FixSettings& s);

// Parses an AspectRatio string; returns 0 for "full screen".
float ParseAspect(const wchar_t* text);

// The primary display's current resolution.
void DesktopResolution(int& w, int& h);

// Writes the chosen resolution (native if 0) into the game's GameUserSettings.ini.
void ApplyResolutionToGame(const FixSettings& s);

// Shows the launch window (unless disabled and Shift isn't held), then writes the chosen
// resolution into the game's GameUserSettings.ini. Returns false if the player chose Quit.
bool RunLaunchSettings(const wchar_t* iniPath, FixSettings& s);
