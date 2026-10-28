# tools/apply_patches.ps1 - Apply all patches in sequence to ps3recomp submodule
param(
    [string]$TargetDir = "ps3recomp"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$PatchesDir = Join-Path $Root "patches"
if ([System.IO.Path]::IsPathRooted($TargetDir)) {
    $Target = $TargetDir
} else {
    $Target = Join-Path $Root $TargetDir
}

if (-not (Test-Path $Target)) {
    Write-Error "Target directory does not exist: $Target"
    exit 1
}

Write-Host "[apply_patches] Applying patches from $PatchesDir to $Target..."

$seriesDir = Join-Path $PatchesDir "series"
if (Test-Path $seriesDir) {
    $patchFiles = Get-ChildItem -Path $seriesDir -Filter "*.patch" | Sort-Object Name
} else {
    $patchFiles = Get-ChildItem -Path $PatchesDir -Filter "ps3recomp-00*.patch" | Sort-Object Name
}
$patchPaths = $patchFiles | ForEach-Object { $_.FullName }

Push-Location $Target
try {
    try { & git am --abort *>$null } catch {}
    Write-Host "  -> Applying $($patchFiles.Count) patches via git apply..."
    foreach ($patch in $patchFiles) {
        Write-Host "  -> Applying $($patch.Name)..."
        & git apply --ignore-whitespace --whitespace=nowarn $patch.FullName
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "git apply failed for $($patch.Name), trying with 3-way merge..."
            & git apply --3way --ignore-whitespace $patch.FullName
            if ($LASTEXITCODE -ne 0) {
                Write-Error "Failed to apply $($patch.Name)"
                exit 1
            }
        }
    }
} finally {
    Pop-Location
}

$vblankPatch = Join-Path $PatchesDir "vblank-pacing.patch"
if (Test-Path $vblankPatch) {
    Write-Host "  -> Applying vblank-pacing.patch..."
    Push-Location $Target
    try {
        & git apply --ignore-whitespace --whitespace=nowarn $vblankPatch
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "git apply returned $LASTEXITCODE on vblank-pacing.patch (may already be partially applied)"
        }
    } finally {
        Pop-Location
    }
}

Write-Host "[apply_patches] All patches applied successfully!"
