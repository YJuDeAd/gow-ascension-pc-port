$elf = "vfs\PS3_GAME\USRDIR\EBOOT.ELF"
$proc = Start-Process -FilePath "build\gow_ascension.exe" -ArgumentList $elf -PassThru -RedirectStandardError "scratch\run35s_stderr.log" -RedirectStandardOutput "scratch\run35s_stdout.log"
Start-Sleep -Seconds 35
if (!$proc.HasExited) {
    Stop-Process -Id $proc.Id -Force
}
Start-Sleep -Seconds 1
Write-Output "--- SUMMARY ---"
Get-Content "scratch\run35s_stderr.log" | Select-String -Pattern "WID8-TIMELINE|ReadyCountStore|frame |ALL-LANES-AUDIT" | Select-Object -Last 30
