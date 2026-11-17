$ErrorActionPreference = "Stop"
$toolsRoot = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $toolsRoot "SkellMawPreviewInstall.psm1") -Force -ErrorAction Stop

$script:Assertions = 0

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "ASSERTION FAILED: $Message"
    }
    $script:Assertions++
}

function Assert-Throws {
    param([scriptblock]$Action, [string]$Message)
    $thrown = $false
    try {
        & $Action
    }
    catch {
        $thrown = $true
    }
    Assert-True -Condition $thrown -Message $Message
}

function New-TestPackage {
    param([string]$Path)

    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    $names = @(Get-SkellMawPreviewPluginNames)
    foreach ($name in $names) {
        $bundle = Join-Path $Path ($name + ".vst3")
        $resources = Join-Path $bundle "Contents\Resources"
        $moduleDirectory = Join-Path $bundle "Contents\x86_64-win"
        New-Item -ItemType Directory -Path $resources -Force | Out-Null
        New-Item -ItemType Directory -Path $moduleDirectory -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $resources "moduleinfo.json") -Value "{}" -Encoding utf8
        Set-Content -LiteralPath (Join-Path $moduleDirectory ($name + ".vst3")) -Value ("test-module-" + $name) -Encoding ascii
    }

    $tools = Join-Path $Path "Tools"
    New-Item -ItemType Directory -Path $tools -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $toolsRoot "Install-SkellMawPreview.ps1") -Destination $tools
    Copy-Item -LiteralPath (Join-Path $toolsRoot "SkellMawPreviewInstall.psm1") -Destination $tools
    Set-Content -LiteralPath (Join-Path $Path "PREVIEW-ONLY.txt") -Value "Preview only." -Encoding utf8
    Set-Content -LiteralPath (Join-Path $Path "WINDOWS-VST3-INSTALL.txt") -Value "Private preview install and rollback guide." -Encoding utf8

    $plugins = @($names | ForEach-Object { [ordered]@{ name = $_; version = "1.0.0" } })
    $info = [ordered]@{
        product = "Skell Maw"
        release_status = "preview-only; not a commercial release"
        format = "VST3"
        platform = "Windows x86_64"
        version = "1.0.0"
        source_commit = "test-source"
        plugins = $plugins
    }
    $info | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Path "build-info.json") -Encoding utf8

    $sumLines = @(
        Get-ChildItem -LiteralPath $Path -File -Force -Recurse |
            Sort-Object -Property FullName |
            ForEach-Object {
                $relative = $_.FullName.Substring($Path.Length).TrimStart([char[]]@("\", "/")).Replace("\", "/")
                $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                ($hash + "  " + $relative)
            }
    )
    Set-Content -LiteralPath (Join-Path $Path "SHA256SUMS.txt") -Value ($sumLines -join [Environment]::NewLine) -Encoding ascii
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("SkellMawPreviewTests-" + [guid]::NewGuid().ToString("N"))
$packagePath = Join-Path $tempRoot "package"
$targetPath = Join-Path $tempRoot "user-vst3"
$backupRoot = Join-Path $tempRoot "backups"
$whatIfTarget = Join-Path $tempRoot "whatif-vst3"
$tamperedTarget = Join-Path $tempRoot "tampered-vst3"

try {
    New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
    New-TestPackage -Path $packagePath

    $package = Test-SkellMawPreviewPackage -PackagePath $packagePath
    Assert-True -Condition ($package.PluginNames.Count -eq 7) -Message "package validator accepts exactly seven preview bundles"

    $whatIfResult = Install-SkellMawPreview -PackagePath $packagePath -Vst3Path $whatIfTarget -BackupRoot (Join-Path $tempRoot "whatif-backups") -WhatIf -Confirm:$false
    Assert-True -Condition ($whatIfResult.Status -eq "whatif") -Message "WhatIf reports a dry run"
    Assert-True -Condition (-not (Test-Path -LiteralPath $whatIfTarget)) -Message "WhatIf creates no destination directory"

    $systemTarget = Join-Path $env:ProgramFiles "SkellMawPreviewTestVST3"
    Assert-Throws -Action { Install-SkellMawPreview -PackagePath $packagePath -Vst3Path $systemTarget -BackupRoot (Join-Path $tempRoot "system-backups") -Confirm:$false } -Message "system-wide Program Files installation is refused"
    Assert-True -Condition (-not (Test-Path -LiteralPath $systemTarget)) -Message "refused system install creates no plugin directory"

    $nestedBackup = Join-Path $targetPath "backup-inside-vst3"
    Assert-Throws -Action { Install-SkellMawPreview -PackagePath $packagePath -Vst3Path $targetPath -BackupRoot $nestedBackup -Confirm:$false } -Message "backup directory inside the VST3 scan path is refused"

    New-Item -ItemType Directory -Path $targetPath -Force | Out-Null
    $oldBundle = Join-Path $targetPath "Dark Forest.vst3"
    New-Item -ItemType Directory -Path $oldBundle -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $oldBundle "legacy-preset-state.txt") -Value "known-good prior install" -Encoding utf8
    $oldHash = Get-SkellMawTreeHash -Path $oldBundle

    $installed = Install-SkellMawPreview -PackagePath $packagePath -Vst3Path $targetPath -BackupRoot $backupRoot -Confirm:$false
    Assert-True -Condition ($installed.Status -eq "installed") -Message "preview install completes"
    Assert-True -Condition ((@(Get-ChildItem -LiteralPath $targetPath -Directory -Filter "*.vst3")).Count -eq 7) -Message "all seven VST3 bundles are installed"
    Assert-True -Condition (Test-Path -LiteralPath $installed.ManifestPath -PathType Leaf) -Message "install manifest is written"
    $manifest = Get-Content -LiteralPath $installed.ManifestPath -Raw | ConvertFrom-Json
    Assert-True -Condition ((Get-SkellMawTreeHash -Path $manifest.entries[2].backup_path) -eq $oldHash) -Message "pre-existing VST3 bundle is backed up byte-for-byte"

    $rolledBack = Restore-SkellMawPreview -ManifestPath $installed.ManifestPath -Confirm:$false
    Assert-True -Condition ($rolledBack.Status -eq "rolled-back") -Message "rollback completes"
    Assert-True -Condition ((Get-SkellMawTreeHash -Path $oldBundle) -eq $oldHash) -Message "previous VST3 bundle is restored"
    Assert-True -Condition ((@(Get-ChildItem -LiteralPath $targetPath -Directory -Filter "*.vst3")).Count -eq 1) -Message "newly added preview bundles are moved out of the scan folder"
    Assert-True -Condition ((@(Get-ChildItem -LiteralPath $rolledBack.ArchivedPreviewDirectory -Directory -Filter "*.vst3")).Count -eq 7) -Message "preview bundles are preserved in the rollback archive"

    $secondInstall = Install-SkellMawPreview -PackagePath $packagePath -Vst3Path $targetPath -BackupRoot $backupRoot -Confirm:$false
    $modifiedBundle = Join-Path $targetPath "Hypnosis.vst3"
    Set-Content -LiteralPath (Join-Path $modifiedBundle "user-change.txt") -Value "keep this user change" -Encoding utf8
    Assert-Throws -Action { Restore-SkellMawPreview -ManifestPath $secondInstall.ManifestPath -Confirm:$false } -Message "rollback refuses to overwrite a bundle changed after install"
    Assert-True -Condition (Test-Path -LiteralPath (Join-Path $modifiedBundle "user-change.txt")) -Message "refused rollback preserves the user change"

    $tamperedFile = Join-Path $packagePath "Constrictor.vst3\Contents\x86_64-win\Constrictor.vst3"
    Add-Content -LiteralPath $tamperedFile -Value "tampered"
    Assert-Throws -Action { Test-SkellMawPreviewPackage -PackagePath $packagePath } -Message "package checksum mismatch is rejected"
    Assert-Throws -Action { Install-SkellMawPreview -PackagePath $packagePath -Vst3Path $tamperedTarget -BackupRoot (Join-Path $tempRoot "tampered-backups") -Confirm:$false } -Message "tampered package cannot start installation"
    Assert-True -Condition (-not (Test-Path -LiteralPath $tamperedTarget)) -Message "tampered package fails before touching the destination"

    Write-Output ("PASS: " + $script:Assertions + " Skell Maw private-preview install/rollback assertions")
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
