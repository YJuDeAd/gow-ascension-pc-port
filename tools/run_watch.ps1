# tools/run_watch.ps1
$env:PPU_WWATCH = "0x71600484"
$env:PPU_GUARD_EA = "0x71600484"
$env:GOW_NO_WWS_HACK = "1"

New-Item -ItemType Directory -Force build\logs\task1_watch | Out-Null
$outLog = "build\logs\task1_watch\run_01_stdout.log"
$errLog = "build\logs\task1_watch\run_01_stderr.log"

Write-Host "[watch] Starting gow_ascension with watch on 0x71600484 for 60s..."
$p = Start-Process -FilePath "build\gow_ascension.exe" -ArgumentList "vfs\PS3_GAME\USRDIR\EBOOT.ELF" -PassThru -RedirectStandardOutput $outLog -RedirectStandardError $errLog

for ($i = 1; $i -le 60; $i++) {
    Start-Sleep -Seconds 1
    if ($p.HasExited) {
        Write-Host ("[watch] Process exited early at {0}s with code {1}" -f $i, $p.ExitCode)
        break
    }
    if ($i % 10 -eq 0) {
        Write-Host ("[watch] Elapsed: {0}s / 60s" -f $i)
    }
}

if (!$p.HasExited) {
    Stop-Process -Id $p.Id -Force
    Write-Host "[watch] Process stopped after 60s."
}
