<#
.SYNOPSIS
    Removes DS5Dongle Windows build outputs and, optionally, its managed tools.

.DESCRIPTION
    By default, removes this checkout's build directory and UF2 files generated
    next to the Windows build script. Use explicit switches to also remove the
    Desktop UF2 copies or the builder-owned %USERPROFILE%\.ds5-build directory.

    System-wide tools installed through winget are deliberately left installed
    because they may be shared with other projects.

.PARAMETER Dependencies
    Also remove %USERPROFILE%\.ds5-build, including the managed Pico SDK, ARM
    toolchain, MinGW host compiler, portable tools, and standalone clone.

.PARAMETER Desktop
    Also remove ds5-bridge UF2 files copied to the current user's Desktop.

.PARAMETER All
    Equivalent to specifying both -Dependencies and -Desktop in addition to the
    default local-output cleanup.

.PARAMETER Force
    Allow -Dependencies to remove a managed standalone clone with uncommitted
    files. This does not bypass PowerShell's -WhatIf or -Confirm behavior.

.EXAMPLE
    pwsh .\clean-windows.ps1

.EXAMPLE
    pwsh .\clean-windows.ps1 -All -WhatIf

.EXAMPLE
    pwsh .\clean-windows.ps1 -Dependencies
#>

[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Medium')]
param(
    [switch]$Dependencies,
    [switch]$Desktop,
    [switch]$All,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = $Utf8NoBom
[Console]::OutputEncoding = $Utf8NoBom
$OutputEncoding = $Utf8NoBom

function Info([string]$Message) {
    Write-Host "[ds5-clean] $Message" -ForegroundColor Cyan
}

function Ok([string]$Message) {
    Write-Host "[ds5-clean] $Message" -ForegroundColor Green
}

function Get-FullPath([string]$Path) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    $rootPath = [IO.Path]::GetPathRoot($fullPath)
    if ([string]::Equals($fullPath, $rootPath,
            [StringComparison]::OrdinalIgnoreCase)) {
        return $fullPath
    }

    return $fullPath.TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
}

function Assert-ExactPath([string]$Path, [string]$ExpectedPath) {
    $resolved = Get-FullPath $Path
    $expected = Get-FullPath $ExpectedPath
    if (-not [string]::Equals($resolved, $expected,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected path '$resolved' (expected '$expected')."
    }
    return $resolved
}

function Remove-OwnedDirectory {
    param(
        [Parameter(Mandatory)] [string]$Path,
        [Parameter(Mandatory)] [string]$ExpectedPath,
        [Parameter(Mandatory)] [string]$Description
    )

    $target = Assert-ExactPath $Path $ExpectedPath
    if (-not (Test-Path -LiteralPath $target)) {
        Info "Already clean: $Description"
        return
    }

    $item = Get-Item -LiteralPath $target -Force
    if (-not $item.PSIsContainer) {
        throw "Refusing to recursively remove '$target' because it is not a directory."
    }
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to recursively remove reparse-point directory '$target'."
    }

    if ($PSCmdlet.ShouldProcess($target, "Remove $Description recursively")) {
        Remove-Item -LiteralPath $target -Recurse -Force
        Ok "Removed ${Description}: $target"
    }
}

function Remove-OwnedFile {
    param(
        [Parameter(Mandatory)] [string]$Path,
        [Parameter(Mandatory)] [string]$Description
    )

    $target = Get-FullPath $Path
    if (-not (Test-Path -LiteralPath $target)) {
        return
    }

    $item = Get-Item -LiteralPath $target -Force
    if ($item.PSIsContainer) {
        throw "Refusing to remove '$target' because it is a directory."
    }

    if ($PSCmdlet.ShouldProcess($target, "Remove $Description")) {
        Remove-Item -LiteralPath $target -Force
        Ok "Removed ${Description}: $target"
    }
}

if ($All) {
    $Dependencies = $true
    $Desktop = $true
}

$repoCandidate = Get-FullPath (Join-Path $PSScriptRoot '..')
$isCheckout = ((Test-Path -LiteralPath (Join-Path $repoCandidate 'CMakeLists.txt')) -and
               (Test-Path -LiteralPath (Join-Path $repoCandidate 'src')))

if ($isCheckout) {
    $buildDir = Join-Path $repoCandidate 'build'
    Remove-OwnedDirectory -Path $buildDir -ExpectedPath $buildDir `
        -Description 'repository build directory'
} else {
    Info "No checkout found next to this script; local build outputs were skipped."
}

foreach ($name in @('ds5-bridge.uf2', 'ds5-bridge-debug.uf2',
                     'ds5-bridge-wake.uf2')) {
    Remove-OwnedFile -Path (Join-Path $PSScriptRoot $name) `
        -Description "generated firmware $name"
}

if ($Desktop) {
    $desktopPath = [Environment]::GetFolderPath('Desktop')
    if ([string]::IsNullOrWhiteSpace($desktopPath)) {
        Info 'Desktop path is unavailable; Desktop UF2 cleanup was skipped.'
    } else {
        foreach ($name in @('ds5-bridge.uf2', 'ds5-bridge-debug.uf2',
                             'ds5-bridge-wake.uf2')) {
            Remove-OwnedFile -Path (Join-Path $desktopPath $name) `
                -Description "Desktop firmware $name"
        }
    }
}

if ($Dependencies) {
    if ([string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        throw 'USERPROFILE is unavailable; refusing to resolve the dependency directory.'
    }

    $userProfile = Get-FullPath $env:USERPROFILE
    $toolsHome = Get-FullPath (Join-Path $userProfile '.ds5-build')
    $expectedToolsHome = Join-Path $userProfile '.ds5-build'
    $managedClone = Join-Path $toolsHome 'DS5Dongle'

    if ((Test-Path -LiteralPath (Join-Path $managedClone '.git')) -and -not $Force) {
        $git = Get-Command git -ErrorAction SilentlyContinue
        if (-not $git) {
            throw "The managed clone exists at '$managedClone', but git is unavailable. " +
                  'Re-run with -Force only if discarding that clone is intended.'
        }

        $cloneStatus = @(& git -C $managedClone status --porcelain 2>$null)
        if ($LASTEXITCODE -ne 0) {
            throw "Could not inspect '$managedClone'. Re-run with -Force only if deletion is intended."
        }
        if ($cloneStatus.Count -gt 0) {
            throw "The managed clone at '$managedClone' has uncommitted files. " +
                  'Commit/copy them first, or re-run with -Force to discard them.'
        }
    }

    Remove-OwnedDirectory -Path $toolsHome -ExpectedPath $expectedToolsHome `
        -Description 'builder dependency environment'
    Info 'System tools installed by winget were left installed.'
}

if ($WhatIfPreference) {
    Ok 'Cleanup preview complete; no files were removed.'
} else {
    Ok 'Cleanup complete.'
}
