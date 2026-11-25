# Logs ABZU's main window state every 100 ms (size, minimized, foreground, title) for testing
# startup behaviour. Prints only changes, with time since the script started.
# usage: watch_window.ps1 -Seconds 40 -Out log.txt
param([int]$Seconds = 40, [string]$Out)
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class WW {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
  public struct RECT { public int L, T, R, B; }
}
"@
[WW]::SetProcessDPIAware() | Out-Null
$sw = [Diagnostics.Stopwatch]::StartNew()
$last = ""
$lines = @()
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
    $p = Get-Process AbzuGame-Win64-Shipping -ErrorAction SilentlyContinue | Select-Object -First 1
    $s = "no process"
    if ($p) {
        $p.Refresh()
        $h = $p.MainWindowHandle
        if ($h -ne 0) {
            $wr = New-Object WW+RECT; $cr = New-Object WW+RECT
            [WW]::GetWindowRect($h, [ref]$wr) | Out-Null; [WW]::GetClientRect($h, [ref]$cr) | Out-Null
            $style = "{0:X8}" -f [WW]::GetWindowLong($h, -16)
            $s = "'$($p.MainWindowTitle)' pos=$($wr.L),$($wr.T) client=$($cr.R)x$($cr.B) min=$([WW]::IsIconic($h)) vis=$([WW]::IsWindowVisible($h)) fg=$([WW]::GetForegroundWindow() -eq $h) style=$style"
        } else { $s = "process, no window" }
    }
    if ($s -ne $last) {
        $line = "{0,6:F1}s  {1}" -f $sw.Elapsed.TotalSeconds, $s
        $lines += $line
        $last = $s
    }
    Start-Sleep -Milliseconds 100
}
if ($Out) { $lines | Set-Content -Encoding utf8 $Out }
$lines
