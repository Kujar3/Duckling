# Installs the built Duckling VST3 (and CLAP, if built) into the standard plugin folders so FL Studio finds them.
#
# Usage (from the Duckling folder, in PowerShell):
#   powershell -ExecutionPolicy Bypass -File scripts\install-plugin.ps1                 (all users, asks for admin once)
#   powershell -ExecutionPolicy Bypass -File scripts\install-plugin.ps1 -CurrentUser    (no admin needed)
#
# All users:    C:\Program Files\Common Files\VST3\Duckling.vst3   and   ...\Common Files\CLAP\Duckling.clap
#               (the standard VST3 folder: FL Studio scans it by default)
# -CurrentUser: %LOCALAPPDATA%\Programs\Common\VST3\Duckling.vst3  and   ...\Programs\Common\CLAP\Duckling.clap
#               (add that VST3 folder in FL Studio: Options > Manage plugins > Plugin search paths)
# -BuildDir picks another build folder (default: build). -DestRoot installs into <DestRoot>\VST3 and <DestRoot>\CLAP
# instead (for testing the script).
#
# Close FL Studio (or every project that uses Duckling) first: Windows does not let a plugin file be replaced while a
# program has it loaded. If it is open anyway, the installed version is left as it was.

param(
    [string]$BuildDir = "build",
    [switch]$CurrentUser,
    [string]$DestRoot = "",
    [switch]$Elevated   # internal: set when the script re-runs itself with administrator rights
)

$ErrorActionPreference = "Stop"

function Finish([int]$code) {
    # The elevated copy runs in its own window: keep it open long enough to read.
    if ($Elevated) { Start-Sleep -Seconds $(if ($code -eq 0) { 4 } else { 15 }) }
    exit $code
}

# Installs $source (the VST3 bundle folder or the CLAP file) as $destDir\$name without ever leaving a broken copy:
# the new version is copied next to it first ($name.new), the installed one is renamed to $name.old (while FL Studio
# has the plugin loaded this rename fails and the installed copy stays intact), the new one is renamed into place, and
# only then is the old one deleted. A $name.old left by an interrupted run is restored, never thrown away, while
# $name itself is missing.
function Install-Replacing([string]$source, [string]$destDir, [string]$name) {
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    $target = Join-Path $destDir $name
    $staging = Join-Path $destDir "$name.new"
    $backup = Join-Path $destDir "$name.old"

    if (-not (Test-Path -LiteralPath $target) -and (Test-Path -LiteralPath $backup)) {
        Rename-Item -LiteralPath $backup -NewName $name
    }

    foreach ($leftover in @($staging, $backup)) {
        if (Test-Path -LiteralPath $leftover) { Remove-Item -LiteralPath $leftover -Recurse -Force }
    }

    try {
        Copy-Item -LiteralPath $source -Destination $staging -Recurse

        if (Test-Path -LiteralPath $target) { Rename-Item -LiteralPath $target -NewName "$name.old" }

        try {
            Rename-Item -LiteralPath $staging -NewName $name
        } catch {
            if (Test-Path -LiteralPath $backup) { Rename-Item -LiteralPath $backup -NewName $name }
            throw
        }
    } catch {
        if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue }
        throw
    }

    Write-Host "Installed $target"

    if (Test-Path -LiteralPath $backup) {
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue

        if (Test-Path -LiteralPath $backup) {
            Write-Host "  The previous version is still loaded: restart FL Studio to use the new one ($backup can be deleted then)."
        }
    }
}

$root = Split-Path -Parent $PSScriptRoot
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$buildPath = [System.IO.Path]::GetFullPath($buildPath)
if ($buildPath.Length -gt 3) { $buildPath = $buildPath.TrimEnd([char[]]@('\', '/')) }
$artefacts = Join-Path $buildPath "Duckling_artefacts\Release"
$vst3 = Join-Path $artefacts "VST3\Duckling.vst3"
$clap = Join-Path $artefacts "CLAP\Duckling.clap"

if (-not (Test-Path -LiteralPath $vst3)) {
    Write-Host "Build not found: $vst3" -ForegroundColor Red
    Write-Host "Build it first:  scripts\build.bat $BuildDir Duckling_VST3 Duckling_CLAP"
    Finish 1
}

if ($DestRoot -ne "") {
    $commonDir = $DestRoot
} elseif ($CurrentUser) {
    $commonDir = Join-Path $env:LOCALAPPDATA "Programs\Common"
} else {
    # The 64-bit "Common Files" folder, also from a 32-bit PowerShell.
    $commonDir = if ($env:CommonProgramW6432) { $env:CommonProgramW6432 } else { $env:CommonProgramFiles }
}

$vst3Dest = Join-Path $commonDir "VST3"
$clapDest = Join-Path $commonDir "CLAP"

# Program Files needs administrator rights: re-run this script elevated (one UAC prompt) and pass on its result.
$needsAdmin = ($DestRoot -eq "") -and (-not $CurrentUser)
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

if ($needsAdmin -and -not $isAdmin -and -not $Elevated) {
    Write-Host "Asking for administrator rights to copy into $commonDir ..."
    # A path that still ends in a backslash (a drive root) would escape the closing quote: double it.
    $quotedBuildPath = if ($buildPath.EndsWith('\')) { $buildPath + '\' } else { $buildPath }

    try {
        $child = Start-Process powershell -Verb RunAs -Wait -PassThru -ArgumentList @(
            "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"",
            "-BuildDir", "`"$quotedBuildPath`"", "-Elevated")
    } catch {
        Write-Host "Administrator rights were not granted, so nothing was installed." -ForegroundColor Yellow
        Write-Host "To install for your user only (no admin), run:"
        Write-Host "  powershell -ExecutionPolicy Bypass -File scripts\install-plugin.ps1 -CurrentUser"
        exit 1
    }

    if ($child.ExitCode -eq 0) { Write-Host "Installed." } else { Write-Host "Install failed (see the other window)." -ForegroundColor Red }
    exit $child.ExitCode
}

try {
    Install-Replacing $vst3 $vst3Dest "Duckling.vst3"

    if (Test-Path -LiteralPath $clap) {
        Install-Replacing $clap $clapDest "Duckling.clap"
    }
} catch {
    Write-Host ""
    Write-Host "Could not install the plugin: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "If FL Studio is open, close it (a loaded plugin can't be replaced) and run this script again."
    Write-Host "The version installed before (if any) was left as it was."
    Finish 1
}

Write-Host ""
Write-Host "Done. In FL Studio: Options > Manage plugins > Find installed plugins."
if ($CurrentUser) {
    Write-Host "First add $vst3Dest under Plugin search paths in that window (FL doesn't scan it by default)."
    Write-Host "If FL still doesn't list Duckling, install it for all users instead (run this script without -CurrentUser)."
}
Write-Host "Then open the Mixer (F9), select the track you want to duck (bass, pads, ...) and add Duckling to one of its slots."
Finish 0
