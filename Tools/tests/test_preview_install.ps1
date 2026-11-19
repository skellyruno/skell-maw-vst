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

function Get-TestClassId {
    param([string]$Seed)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $digest = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Seed))
        return [System.BitConverter]::ToString($digest).Replace("-", "").Substring(0, 32)
    }
    finally {
        $sha.Dispose()
    }
}

function New-TestPeModule {
    param(
        [string]$Path,
        [UInt16]$Machine = 0x8664,
        [UInt16]$OptionalMagic = 0x20B
    )
    $data = New-Object byte[] 256
    $data[0] = 0x4D
    $data[1] = 0x5A
    [System.BitConverter]::GetBytes([int]128).CopyTo($data, 0x3C)
    $data[128] = 0x50
    $data[129] = 0x45
    $data[130] = 0
    $data[131] = 0
    [System.BitConverter]::GetBytes([UInt16]$Machine).CopyTo($data, 132)
    [System.BitConverter]::GetBytes([UInt16]$OptionalMagic).CopyTo($data, 152)
    [System.IO.File]::WriteAllBytes($Path, $data)
}

function Update-TestPackageChecksums {
    param([string]$Path)
    $sumPath = Join-Path $Path "SHA256SUMS.txt"
    $sumLines = @(
        Get-ChildItem -LiteralPath $Path -File -Force -Recurse |
            Where-Object { $_.FullName -ne $sumPath } |
            Sort-Object -Property FullName |
            ForEach-Object {
                $relative = $_.FullName.Substring($Path.Length).TrimStart([char[]]@("\", "/")).Replace("\", "/")
                $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                ($hash + "  " + $relative)
            }
    )
    Set-Content -LiteralPath $sumPath -Value ($sumLines -join [Environment]::NewLine) -Encoding ascii
}

function New-TestPackage {
    param([string]$Path)

    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    $names = @(Get-SkellMawPreviewPluginNames)
    $plugins = @()
    foreach ($name in $names) {
        $bundle = Join-Path $Path ($name + ".vst3")
        $resources = Join-Path $bundle "Contents\Resources"
        $moduleDirectory = Join-Path $bundle "Contents\x86_64-win"
        New-Item -ItemType Directory -Path $resources -Force | Out-Null
        New-Item -ItemType Directory -Path $moduleDirectory -Force | Out-Null

        $processorCid = Get-TestClassId -Seed ($name + "|processor")
        $controllerCid = Get-TestClassId -Seed ($name + "|controller")
        $classIds = @($processorCid, $controllerCid) | Sort-Object
        $moduleInfo = [ordered]@{
            Name = $name
            Version = "1.0.0"
            "Factory Info" = [ordered]@{
                Vendor = "Skell Audio"
                URL = ""
                "E-Mail" = ""
            }
            Classes = @(
                [ordered]@{
                    CID = $processorCid
                    Category = "Audio Module Class"
                    Name = $name
                    Vendor = "Skell Audio"
                    Version = "1.0.0"
                },
                [ordered]@{
                    CID = $controllerCid
                    Category = "Component Controller Class"
                    Name = $name
                    Vendor = "Skell Audio"
                    Version = "1.0.0"
                }
            )
        }
        $moduleInfo | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath (Join-Path $resources "moduleinfo.json") -Encoding utf8
        New-TestPeModule -Path (Join-Path $moduleDirectory ($name + ".vst3"))

        $plugins += [ordered]@{
            name = $name
            version = "1.0.0"
            class_ids = $classIds
        }
    }

    $tools = Join-Path $Path "Tools"
    New-Item -ItemType Directory -Path $tools -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $toolsRoot "Install-SkellMawPreview.ps1") -Destination $tools
    Copy-Item -LiteralPath (Join-Path $toolsRoot "SkellMawPreviewInstall.psm1") -Destination $tools
    Set-Content -LiteralPath (Join-Path $Path "PREVIEW-ONLY.txt") -Value "Preview only." -Encoding utf8
    Set-Content -LiteralPath (Join-Path $Path "WINDOWS-VST3-INSTALL.txt") -Value "Private preview install and rollback guide." -Encoding utf8

    $info = [ordered]@{
        product = "Skell Maw"
        vendor = "Skell Audio"
        release_status = "preview-only; not a commercial release"
        format = "VST3"
        platform = "Windows x86_64"
        version = "1.0.0"
        source_commit = "test-source"
        plugins = $plugins
    }
    $info | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Path "build-info.json") -Encoding utf8
    Update-TestPackageChecksums -Path $Path
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

    # Commercial metadata must remain an installable private preview until signing/publication.
    # These are synthetic test fixtures only; no real owner license or contact data is supplied.
    $commercialCandidatePath = Join-Path $tempRoot "commercial-candidate-package"
    New-Item -ItemType Directory -Path $commercialCandidatePath -Force | Out-Null
    Get-ChildItem -LiteralPath $packagePath -Force | Copy-Item -Destination $commercialCandidatePath -Recurse -Force
    $commercialInfoPath = Join-Path $commercialCandidatePath "build-info.json"
    $commercialInfo = Get-Content -LiteralPath $commercialInfoPath -Raw | ConvertFrom-Json
    $commercialInfo.release_status = "preview-only; not a commercial release"
    $commercialInfo.commercial_candidate = $true
    $commercialInfo | ConvertTo-Json -Depth 12 |
        Set-Content -LiteralPath $commercialInfoPath -Encoding utf8
    foreach ($pluginName in (Get-SkellMawPreviewPluginNames)) {
        $moduleInfoPath = Join-Path $commercialCandidatePath ($pluginName + ".vst3\Contents\Resources\moduleinfo.json")
        $moduleInfo = Get-Content -LiteralPath $moduleInfoPath -Raw | ConvertFrom-Json
        $moduleInfo.'Factory Info'.URL = "https://example.invalid/skell"
        $moduleInfo.'Factory Info'.'E-Mail' = "release@example.invalid"
        $moduleInfo | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $moduleInfoPath -Encoding utf8
    }
    $noticePath = Join-Path $commercialCandidatePath "THIRD_PARTY_NOTICES\juce\LICENSE.txt"
    New-Item -ItemType Directory -Path (Split-Path -Parent $noticePath) -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $commercialCandidatePath "LICENSE") -Value "Synthetic test-only owner license." -Encoding utf8
    Set-Content -LiteralPath $noticePath -Value "Synthetic test-only dependency notice." -Encoding utf8
    Update-TestPackageChecksums -Path $commercialCandidatePath
    $commercialPackage = Test-SkellMawPreviewPackage -PackagePath $commercialCandidatePath
    Assert-True -Condition ($commercialPackage.Info.commercial_candidate -eq $true) -Message "preview installer accepts an explicitly marked private commercial candidate"
    Assert-True -Condition ($commercialPackage.Info.release_status -eq "preview-only; not a commercial release") -Message "commercial candidate remains preview-only for the existing installer"
    Assert-True -Condition (Test-Path -LiteralPath (Join-Path $commercialCandidatePath "LICENSE") -PathType Leaf) -Message "commercial candidate carries its owner license"
    Assert-True -Condition (Test-Path -LiteralPath $noticePath -PathType Leaf) -Message "commercial candidate carries third-party notices"

    $metadataTamperedPath = Join-Path $tempRoot "metadata-tampered-package"
    Copy-Item -LiteralPath $packagePath -Destination $metadataTamperedPath -Recurse
    $badMetadataPath = Join-Path $metadataTamperedPath "Dark Forest.vst3\Contents\Resources\moduleinfo.json"
    $badMetadata = Get-Content -LiteralPath $badMetadataPath -Raw | ConvertFrom-Json
    $badMetadata.Name = "Wrong Plugin"
    $badMetadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $badMetadataPath -Encoding utf8
    Update-TestPackageChecksums -Path $metadataTamperedPath
    Assert-Throws -Action { Test-SkellMawPreviewPackage -PackagePath $metadataTamperedPath } -Message "metadata identity mismatch is rejected even when package checksums are recomputed"

    $wrongMachinePath = Join-Path $tempRoot "wrong-machine-package"
    Copy-Item -LiteralPath $packagePath -Destination $wrongMachinePath -Recurse
    New-TestPeModule -Path (Join-Path $wrongMachinePath "Constrictor.vst3\Contents\x86_64-win\Constrictor.vst3") -Machine 0x14c
    Update-TestPackageChecksums -Path $wrongMachinePath
    Assert-Throws -Action { Test-SkellMawPreviewPackage -PackagePath $wrongMachinePath } -Message "non-AMD64 VST3 module is rejected even when package checksums are recomputed"

    $pe32Path = Join-Path $tempRoot "pe32-package"
    Copy-Item -LiteralPath $packagePath -Destination $pe32Path -Recurse
    New-TestPeModule -Path (Join-Path $pe32Path "Night Bloom.vst3\Contents\x86_64-win\Night Bloom.vst3") -Machine 0x8664 -OptionalMagic 0x10b
    Update-TestPackageChecksums -Path $pe32Path
    Assert-Throws -Action { Test-SkellMawPreviewPackage -PackagePath $pe32Path } -Message "PE32 rather than PE32+ VST3 module is rejected"

    $unexpectedBundle = Join-Path $packagePath "Unexpected.vst3"
    New-Item -ItemType Directory -Path $unexpectedBundle -Force | Out-Null
    Assert-Throws -Action { Test-SkellMawPreviewPackage -PackagePath $packagePath } -Message "unexpected VST3 bundle is rejected"
    Remove-Item -LiteralPath $unexpectedBundle -Recurse -Force
    $package = Test-SkellMawPreviewPackage -PackagePath $packagePath

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

    $systemRollbackRoot = Join-Path $tempRoot "system-rollback"
    New-Item -ItemType Directory -Path $systemRollbackRoot -Force | Out-Null
    $systemRollbackManifestPath = Join-Path $systemRollbackRoot "install-manifest.json"
    $systemRollbackManifest = Get-Content -LiteralPath $secondInstall.ManifestPath -Raw | ConvertFrom-Json
    $systemRollbackManifest.target_directory = $systemTarget
    $systemRollbackManifest.backup_directory = $systemRollbackRoot
    $systemRollbackManifest | ConvertTo-Json -Depth 12 |
        Set-Content -LiteralPath $systemRollbackManifestPath -Encoding utf8
    Assert-Throws -Action { Restore-SkellMawPreview -ManifestPath $systemRollbackManifestPath -Confirm:$false } -Message "rollback refuses a Program Files destination even when the manifest is edited"

    $nestedRollbackRoot = Join-Path $targetPath ".skell-maw-backup-guard"
    New-Item -ItemType Directory -Path $nestedRollbackRoot -Force | Out-Null
    $nestedRollbackManifestPath = Join-Path $nestedRollbackRoot "install-manifest.json"
    $nestedRollbackManifest = Get-Content -LiteralPath $secondInstall.ManifestPath -Raw | ConvertFrom-Json
    $nestedRollbackManifest.target_directory = $targetPath
    $nestedRollbackManifest.backup_directory = $nestedRollbackRoot
    $nestedRollbackManifest | ConvertTo-Json -Depth 12 |
        Set-Content -LiteralPath $nestedRollbackManifestPath -Encoding utf8
    Assert-Throws -Action { Restore-SkellMawPreview -ManifestPath $nestedRollbackManifestPath -Confirm:$false } -Message "rollback refuses a backup directory inside the VST3 scan path"
    Remove-Item -LiteralPath $nestedRollbackRoot -Recurse -Force

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
