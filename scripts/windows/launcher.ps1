# Opens the game's launcher, launcher\AscensionLauncher.exe (settings, mods, PLAY).
#   .\scripts\windows\launcher.ps1          the window
#   .\scripts\windows\launcher.ps1 -Play    start the game with the saved settings, no window
param([switch]$Play)
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Exe = Join-Path $Root "launcher\AscensionLauncher.exe"
if ($Play) { Start-Process $Exe -ArgumentList "--play" -Wait } else { Start-Process $Exe }
