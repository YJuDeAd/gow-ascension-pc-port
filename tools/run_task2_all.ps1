$ErrorActionPreference = "Stop"

$oldExe = "C:\IfItWorksItWorks\Project\gow-pre019\build\gow_ascension.exe"
$oldWork = "C:\IfItWorksItWorks\Project\gow-pre019"
$newExe = "C:\IfItWorksItWorks\Project\gowassension\build\gow_ascension.exe"
$newWork = "C:\IfItWorksItWorks\Project\gowassension"

$harness = "C:\IfItWorksItWorks\Project\gowassension\tools\run_harness.ps1"

Write-Host "=== TASK 2: 3 COLD RUNS (40s each) OF OLD EXE ==="
for ($i = 1; $i -le 3; $i++) {
    Write-Host "`n--- Running OLD exe Run $i / 3 (40s) ---"
    $errLog = "C:\IfItWorksItWorks\Project\gowassension\build\logs\task2_old_run$i.stderr.log"
    $outLog = "C:\IfItWorksItWorks\Project\gowassension\build\logs\task2_old_run$i.stdout.log"
    & powershell -ExecutionPolicy Bypass -File $harness -ExePath $oldExe -WorkDir $oldWork -StdErrLog $errLog -StdOutLog $outLog -DurationSec 40
    Start-Sleep -Seconds 2
}

Write-Host "`n=== TASK 2: 3 COLD RUNS (40s each) OF NEW EXE ==="
for ($i = 1; $i -le 3; $i++) {
    Write-Host "`n--- Running NEW exe Run $i / 3 (40s) ---"
    $errLog = "C:\IfItWorksItWorks\Project\gowassension\build\logs\task2_new_run$i.stderr.log"
    $outLog = "C:\IfItWorksItWorks\Project\gowassension\build\logs\task2_new_run$i.stdout.log"
    & powershell -ExecutionPolicy Bypass -File $harness -ExePath $newExe -WorkDir $newWork -StdErrLog $errLog -StdOutLog $outLog -DurationSec 40
    Start-Sleep -Seconds 2
}

Write-Host "`nAll 6 runs complete!"
