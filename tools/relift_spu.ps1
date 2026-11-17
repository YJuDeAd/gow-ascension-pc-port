<#
.SYNOPSIS
    Re-lifts all God of War: Ascension SPU sources from pristine EBOOT using upstream merged lifter pipeline.
.DESCRIPTION
    Executes full SPU pipeline:
    1. Extracts SPU ELF images and WWS policy module from pristine EBOOT.
    2. Lifts 33 WWS Job Modules with --merge-chunks and --xregs.
    3. Lifts 42 SPU Position-Independent Code (PIC) fragments with --xregs.
    4. Builds 5 SPU workload images with --merge-chunks and --xregs.
    5. Installs generated sources to src/gen/spu/.
#>
[CmdletBinding()]
param(
    [string]$OutDir = "$PSScriptRoot\..\scratch\spu_relifted",
    [string]$InstallDir = "$PSScriptRoot\..\src\gen\spu",
    [string]$EbootPath = "$PSScriptRoot\..\vfs\PS3_GAME\USRDIR\EBOOT.ELF",
    [string]$JobmodsDir = "D:\gow-bilal-asia\work\jobmods"
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..").Path
$ExtractedSpu = Join-Path $Root "scratch\extracted_spu"
$ExtraFuncsJson = Join-Path $PSScriptRoot "jobmods_extra_funcs.json"
$JobFragmentsJson = Join-Path $PSScriptRoot "job_fragments.json"

Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host " God of War: Ascension SPU Lifter Pipeline (--merge-chunks, --xregs)" -ForegroundColor Cyan
Write-Host " Root:       $Root"
Write-Host " EBOOT:      $EbootPath"
Write-Host " OutDir:     $OutDir"
Write-Host " InstallDir: $InstallDir"
Write-Host "================================================================================"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $ExtractedSpu | Out-Null
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

$env:SPU_LIFT_XREGS = "1"
$env:LIFT_JOBMODS_MERGE = "1"
$env:LIFT_MERGE_CHUNKS = "1"

$sw = [System.Diagnostics.Stopwatch]::StartNew()

# Step 1: Extract SPU ELFs & Policy Module
Write-Host "`n>>> [1/4] Extracting SPU images from pristine EBOOT..." -ForegroundColor Yellow
python "$Root\ps3recomp\tools\extract_spu_images.py" "$EbootPath" -o "$ExtractedSpu"
if ($LASTEXITCODE -ne 0) { throw "extract_spu_images.py failed ($LASTEXITCODE)" }

python "$PSScriptRoot\extract_spu_pm.py" "$EbootPath" "$ExtractedSpu\spu_wws_pm_at_0077CA80.elf" `
    --address 0x77CA80 --size 12288 --fnv 0xC8D774963A96DCF2
if ($LASTEXITCODE -ne 0) { throw "extract_spu_pm.py failed ($LASTEXITCODE)" }

# Step 2: Lift WWS Job Modules
Write-Host "`n>>> [2/4] Lifting WWS Job Modules (33 modules with --merge-chunks and --xregs)..." -ForegroundColor Yellow
python "$PSScriptRoot\lift_jobmods.py" `
    --jobmods "$JobmodsDir" `
    --out "$OutDir" `
    --register "$OutDir\jobmods_register.c" `
    --symbol-prefix "ascension" `
    --extra-funcs-json "$ExtraFuncsJson" `
    --merge-chunks `
    --xregs
if ($LASTEXITCODE -ne 0) { throw "lift_jobmods.py failed ($LASTEXITCODE)" }

# Step 3: Lift SPU PIC Fragments
Write-Host "`n>>> [3/4] Lifting SPU PIC Fragments with --xregs..." -ForegroundColor Yellow
python "$PSScriptRoot\lift_spu_pic.py" `
    --elf "$EbootPath" `
    --fragments "$JobFragmentsJson" `
    --out "$OutDir" `
    --register "$OutDir\pic_register.c" `
    --symbol-prefix "ascension" `
    --xregs
if ($LASTEXITCODE -ne 0) { throw "lift_spu_pic.py failed ($LASTEXITCODE)" }

# Step 4: Build SPU Workloads
Write-Host "`n>>> [4/4] Building SPU Workloads (5 ELFs with --merge-chunks and --xregs)..." -ForegroundColor Yellow
python "$PSScriptRoot\build_spu_workloads.py" `
    --images "$ExtractedSpu" `
    --lifted "$OutDir" `
    --out "$OutDir\spu_workloads.c" `
    --register-fn "ascension_spu_register_all" `
    --constructor `
    --title "ascension" `
    --relift `
    --merge-chunks `
    --extra-funcs "spu_wws_pm_at_0077CA80=0xA2C,0xA68,0x3368,0x3378"
if ($LASTEXITCODE -ne 0) { throw "build_spu_workloads.py failed ($LASTEXITCODE)" }

# Step 5: Install to destination
Write-Host "`n>>> Installing generated files to $InstallDir..." -ForegroundColor Yellow
Copy-Item -Recurse -Force "$OutDir\*" "$InstallDir\"

$sw.Stop()
Write-Host "`n================================================================================" -ForegroundColor Green
Write-Host (" SPU Re-lift successfully completed in {0:N2}s" -f $sw.Elapsed.TotalSeconds) -ForegroundColor Green
Write-Host " Target directory: $InstallDir" -ForegroundColor Green
Write-Host "================================================================================"
