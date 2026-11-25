# Abzu Screen Resolution Fix

A resolution-only fix that makes ABZÛ (PC) display correctly on ultrawide (21:9) and super-ultrawide (32:9)
monitors. Nothing else about the game is changed.

**Status:** step 1 (settings-file resolution) done. Step 2 (FOV-correcting DLL) not started.

## Notes

- Steam app 384190. Unreal Engine 4.12.5. Install: `D:\SteamLibrary\steamapps\common\ABZU`.
- User config: `%LOCALAPPDATA%\AbzuGame\Saved\Config\WindowsNoEditor\GameUserSettings.ini`.
- Originals: `backup/original-config/`. Save backup: `backup/savegames/`.

## Step 1: settings-file resolution (done)
`config/GameUserSettings.5120x1440.ini` sets `ResolutionSizeX/Y`, `DesiredScreenWidth/Height` and
`LastUserConfirmedResolutionSizeX/Y` to 5120x1440. The game opens briefly as a small window, then
switches to true fullscreen at 5120x1440. Menus are not stretched.

## Baseline framing (screenshots/compare-*.jpg)
Same save, same camera, captured with Steam F12 at native resolution.

| Aspect | Result |
|---|---|
| 4:3 (1920x1440) | Reference framing. |
| 16:9 (2560x1440) | Correct Hor+: same vertical framing as 4:3, more scene at the sides. |
| 32:9 (5120x1440) | **Wrong.** Gains only a little width, and the vertical view collapses: the diver is about 1.6x larger and the fins are cut off at the bottom. |

So the game keeps the vertical FOV steady up to 16:9, then gives it up past 16:9.
**Target for the fix:** keep the 16:9 vertical FOV at every aspect wider than 16:9 (pure Hor+),
continuing the game's own 4:3 to 16:9 behaviour. Leave 16:9 and narrower untouched.
