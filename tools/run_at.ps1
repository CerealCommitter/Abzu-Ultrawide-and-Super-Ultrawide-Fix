# Test helper: set ABZU's resolution in the user settings file, launch through Steam,
# and bring the game window to the front once it reaches true fullscreen.
# usage: run_at.ps1 -Width 5120 -Height 1440
param([int]$Width = 5120, [int]$Height = 1440, [int]$WaitSeconds = 60, [switch]$Windowed)

$f = "$env:LOCALAPPDATA\AbzuGame\Saved\Config\WindowsNoEditor\GameUserSettings.ini"
# Start from the repo's known-good copy: the game can drop resolution lines when it saves.
$lines = Get-Content (Join-Path $PSScriptRoot "..\config\GameUserSettings.5120x1440.ini")
$lines = $lines -replace '^ResolutionSizeX=.*', "ResolutionSizeX=$Width" `
                -replace '^ResolutionSizeY=.*', "ResolutionSizeY=$Height" `
                -replace '^DesiredScreenWidth=.*', "DesiredScreenWidth=$Width" `
                -replace '^DesiredScreenHeight=.*', "DesiredScreenHeight=$Height" `
                -replace '^LastUserConfirmedResolutionSizeX=.*', "LastUserConfirmedResolutionSizeX=$Width" `
                -replace '^LastUserConfirmedResolutionSizeY=.*', "LastUserConfirmedResolutionSizeY=$Height"
if ($Windowed) { $lines = $lines -replace '^Fullscreen=.*', 'Fullscreen=OFF' }
Set-Content -Encoding ascii $f $lines

Add-Type @"
using System; using System.Runtime.InteropServices;
public class AbzuWin {
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  public struct RECT { public int L, T, R, B; }
}
"@
Start-Process "${env:ProgramFiles(x86)}\Steam\steam.exe" -ArgumentList "-applaunch", "384190"
$deadline = (Get-Date).AddSeconds($WaitSeconds)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 2
    $p = Get-Process AbzuGame-Win64-Shipping -ErrorAction SilentlyContinue
    if (-not $p -or $p.MainWindowHandle -eq 0) { continue }
    $r = New-Object AbzuWin+RECT
    [AbzuWin]::GetClientRect($p.MainWindowHandle, [ref]$r) | Out-Null
    if ($r.R -eq $Width -and $r.B -eq $Height) {
        [AbzuWin]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
        [AbzuWin]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
        "fullscreen ${Width}x${Height} reached"
        exit 0
    }
    # still in the startup window: keep it restored and in front
    [AbzuWin]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
    [AbzuWin]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
}
"timeout waiting for ${Width}x${Height}"
exit 1
