$elf = "vfs\PS3_GAME\USRDIR\EBOOT.ELF"
$proc = Start-Process -FilePath "build\gow_ascension.exe" -ArgumentList $elf -PassThru -RedirectStandardError "scratch\wid8_diag_stderr.log" -RedirectStandardOutput "scratch\wid8_diag_stdout.log"
Start-Sleep -Seconds 16
if (!$proc.HasExited) {
    Stop-Process -Id $proc.Id -Force
}
Start-Sleep -Seconds 1
Write-Output "--- CAPTURED LOG ---"
Get-Content "scratch\wid8_diag_stderr.log" | Select-String -Pattern "WID8|brbDriver|ALL-LANES-AUDIT" -Context 0,8 | Select-Object -First 40
