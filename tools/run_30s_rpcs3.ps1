$elf = "vfs\PS3_GAME\USRDIR\EBOOT.ELF"
$proc = Start-Process -FilePath "build\gow_ascension.exe" -ArgumentList @($elf, "--renderer=rpcs3-rsx") -PassThru -RedirectStandardError "scratch\rpcs3_stderr.log" -RedirectStandardOutput "scratch\rpcs3_stdout.log"
Start-Sleep -Seconds 30
if (!$proc.HasExited) {
    Stop-Process -Id $proc.Id -Force
}
Start-Sleep -Seconds 1
Write-Output "=== RPCS3-RSX 30S RUN COMPLETE ==="
