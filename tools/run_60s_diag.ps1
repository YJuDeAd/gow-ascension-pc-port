$elf = "vfs\PS3_GAME\USRDIR\EBOOT.ELF"
$args = @($elf, "--renderer=d3d12")
$proc = Start-Process -FilePath "build\gow_ascension.exe" -ArgumentList $args -PassThru -RedirectStandardError "scratch\run60s_stderr.log" -RedirectStandardOutput "scratch\run60s_stdout.log"
Start-Sleep -Seconds 60
if (!$proc.HasExited) {
    Stop-Process -Id $proc.Id -Force
}
Start-Sleep -Seconds 1
Write-Output "=== 60S CHECKPOINT & PROGRESSION AUDIT ==="
Get-Content "scratch\run60s_stderr.log" | Select-String -Pattern "CP[1-4]|CHECKPOINT|cinematic|movie|menu|ReadyCountStore|WID8-TIMELINE|ALL-LANES-AUDIT|TASK4-WORKLOAD" | Select-Object -Last 50
