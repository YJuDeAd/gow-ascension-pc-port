$elf = "vfs\PS3_GAME\USRDIR\EBOOT.ELF"
$proc = Start-Process -FilePath "build\gow_ascension.exe" -ArgumentList @($elf, "--renderer=rpcs3-rsx") -PassThru -RedirectStandardError "scratch\rpcs3_60s_stderr.log" -RedirectStandardOutput "scratch\rpcs3_60s_stdout.log"
Start-Sleep -Seconds 60
if (!$proc.HasExited) {
    Stop-Process -Id $proc.Id -Force
}
Start-Sleep -Seconds 1
Write-Output "=== RPCS3-RSX 60S RUN COMPLETE ==="
