# Saves the game window as a PNG, even when it is behind other windows
# (PrintWindow with PW_RENDERFULLCONTENT).
#
#   .\scripts\windows\snap.ps1 -Out shot.png
param([Parameter(Mandatory = $true)][string]$Out)

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class SnapWin {
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    public struct RECT { public int L, T, R, B; }
}
'@
$p = Get-Process AscensionRecomp -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Host "no game window"; exit 1 }
$r = New-Object SnapWin+RECT
[SnapWin]::GetClientRect($p.MainWindowHandle, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ([Math]::Max(1, $r.R - $r.L)), ([Math]::Max(1, $r.B - $r.T))
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[SnapWin]::PrintWindow($p.MainWindowHandle, $dc, 3) | Out-Null   # PW_CLIENTONLY | PW_RENDERFULLCONTENT
$g.ReleaseHdc($dc)
$bmp.Save($Out)
Write-Host "saved $Out ($($bmp.Width)x$($bmp.Height))"
