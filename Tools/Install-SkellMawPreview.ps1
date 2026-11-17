[CmdletBinding()]
param(
    [string]$PackagePath,
    [string]$Vst3Path,
    [string]$BackupRoot,
    [string]$ManifestPath,
    [switch]$Rollback,
    [switch]$WhatIf,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$modulePath = Join-Path $PSScriptRoot "SkellMawPreviewInstall.psm1"
Import-Module -Name $modulePath -Force -ErrorAction Stop

if ($Rollback -or -not [string]::IsNullOrWhiteSpace($ManifestPath)) {
    if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
        throw "Rollback requires -ManifestPath <install-manifest.json>."
    }
    $arguments = @{ ManifestPath = $ManifestPath }
    if ($WhatIf) { $arguments.WhatIf = $true }
    if ($Force) { $arguments.Confirm = $false }
    $result = Restore-SkellMawPreview @arguments
}
else {
    if ([string]::IsNullOrWhiteSpace($PackagePath)) {
        $PackagePath = Split-Path -Parent $PSScriptRoot
    }
    $arguments = @{ PackagePath = $PackagePath }
    if (-not [string]::IsNullOrWhiteSpace($Vst3Path)) { $arguments.Vst3Path = $Vst3Path }
    if (-not [string]::IsNullOrWhiteSpace($BackupRoot)) { $arguments.BackupRoot = $BackupRoot }
    if ($WhatIf) { $arguments.WhatIf = $true }
    if ($Force) { $arguments.Confirm = $false }
    $result = Install-SkellMawPreview @arguments
}

$result
