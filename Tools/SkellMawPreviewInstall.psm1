Set-StrictMode -Version Latest

$script:ExpectedPluginNames = @(
    "Butterfly Delay",
    "Constrictor",
    "Dark Forest",
    "Hypnosis",
    "Night Bloom",
    "Serpents Maw",
    "Shed Skin"
)

function Get-SkellMawPreviewPluginNames {
    [CmdletBinding()]
    param()
    return @($script:ExpectedPluginNames)
}

function Get-SkellMawDefaultVst3Path {
    [CmdletBinding()]
    param()
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        throw "LOCALAPPDATA is unavailable; pass an explicit -Vst3Path."
    }
    return (Join-Path $env:LOCALAPPDATA "Programs\Common\VST3")
}

function Get-SkellMawDefaultBackupRoot {
    [CmdletBinding()]
    param()
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        throw "LOCALAPPDATA is unavailable; pass an explicit -BackupRoot."
    }
    return (Join-Path $env:LOCALAPPDATA "SkellAudio\Backups\SkellMaw")
}

function Get-SkellMawTreeHash {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
    $root = [System.IO.Path]::GetFullPath($resolved)
    $rootItem = Get-Item -LiteralPath $root -Force -ErrorAction Stop
    if (-not $rootItem.PSIsContainer) {
        throw "Expected a directory: $Path"
    }
    if (($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing a reparse-point directory: $Path"
    }

    $items = @(Get-ChildItem -LiteralPath $root -Force -Recurse -ErrorAction Stop)
    foreach ($item in $items) {
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing reparse point inside bundle: $($item.FullName)"
        }
    }

    $files = @($items | Where-Object { -not $_.PSIsContainer } | Sort-Object -Property FullName)
    if ($files.Count -eq 0) {
        throw "Bundle contains no files: $Path"
    }

    $trimChars = [char[]]@(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    )
    $rows = foreach ($file in $files) {
        $relative = $file.FullName.Substring($root.Length).TrimStart($trimChars).Replace("\", "/")
        $fileHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
        ($relative + [Environment]::NewLine + $fileHash)
    }

    $payload = [System.Text.Encoding]::UTF8.GetBytes(($rows -join [Environment]::NewLine))
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return [System.Convert]::ToHexString($sha.ComputeHash($payload)).ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
}

function Test-SkellMawAmd64Pe {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $stream = [System.IO.File]::Open(
        $Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::Read
    )
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 0x40) {
            throw "VST3 module is too small to contain a valid PE header: $Path"
        }
        $dosMagic = [System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(2))
        if ($dosMagic -ne "MZ") {
            throw "VST3 module is missing its DOS MZ header: $Path"
        }

        $stream.Position = 0x3c
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0x40 -or $peOffset -gt ($stream.Length - 26)) {
            throw "VST3 module has an invalid PE header offset: $Path"
        }

        $stream.Position = $peOffset
        $signature = [System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
        if ($signature -ne ('PE' + [char]0 + [char]0)) {
            throw "VST3 module is missing its PE signature: $Path"
        }
        $machine = $reader.ReadUInt16()
        $stream.Position = $peOffset + 24
        $optionalMagic = $reader.ReadUInt16()

        if ($machine -ne 0x8664) {
            throw ("VST3 module must be AMD64 (0x8664); found 0x{0:x4}: {1}" -f $machine, $Path)
        }
        if ($optionalMagic -ne 0x20b) {
            throw ("VST3 module must use a PE32+ optional header (0x20b); found 0x{0:x4}: {1}" -f $optionalMagic, $Path)
        }
    }
    finally {
        $reader.Dispose()
    }
}

function Test-SkellMawPreviewPackage {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$PackagePath
    )

    $root = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $PackagePath -ErrorAction Stop).Path)
    $rootItem = Get-Item -LiteralPath $root -Force -ErrorAction Stop
    if (-not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Preview package root must be a normal directory."
    }
    $requiredFiles = @(
        "build-info.json",
        "PREVIEW-ONLY.txt",
        "SHA256SUMS.txt",
        "WINDOWS-VST3-INSTALL.txt",
        "Tools\Install-SkellMawPreview.ps1",
        "Tools\SkellMawPreviewInstall.psm1"
    )
    foreach ($relative in $requiredFiles) {
        $path = Join-Path $root $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Preview package is missing required file: $relative"
        }
    }

    $allItems = @(Get-ChildItem -LiteralPath $root -Force -Recurse -ErrorAction Stop)
    foreach ($item in $allItems) {
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Preview package contains a reparse point: $($item.FullName)"
        }
    }

    $info = Get-Content -LiteralPath (Join-Path $root "build-info.json") -Raw -ErrorAction Stop |
        ConvertFrom-Json -ErrorAction Stop
    if ($info.product -ne "Skell Maw" -or $info.format -ne "VST3" -or $info.platform -ne "Windows x86_64") {
        throw "Package identity must be Skell Maw / Windows x86_64 / VST3."
    }
    if ($info.release_status -ne "preview-only; not a commercial release") {
        throw "Only explicitly preview-only packages may be installed by this helper."
    }
    if ([string]::IsNullOrWhiteSpace([string]$info.version)) {
        throw "Package version is missing."
    }

    $expected = @($script:ExpectedPluginNames | Sort-Object)
    $expectedBundles = @($expected | ForEach-Object { $_ + ".vst3" })
    $bundles = @(
        Get-ChildItem -LiteralPath $root -Directory -Force -ErrorAction Stop |
            Where-Object { $_.Name.EndsWith(".vst3", [System.StringComparison]::OrdinalIgnoreCase) } |
            ForEach-Object { $_.Name } |
            Sort-Object
    )
    if (($bundles -join [Environment]::NewLine) -ne ($expectedBundles -join [Environment]::NewLine)) {
        throw "Package must contain exactly the seven expected VST3 bundles; found: $($bundles -join ', ')"
    }

    $pluginMetadata = @($info.plugins)
    if ($pluginMetadata.Count -ne $expected.Count) {
        throw "build-info.json must describe exactly seven VST3 plugins."
    }
    $metadataNames = @($pluginMetadata | ForEach-Object { [string]$_.name } | Sort-Object)
    if (($metadataNames -join [Environment]::NewLine) -ne ($expected -join [Environment]::NewLine)) {
        throw "build-info.json plugin inventory does not match the seven expected VST3 bundles."
    }
    if ([string]::IsNullOrWhiteSpace([string]$info.vendor)) {
        throw "Package vendor identity is missing."
    }

    $pluginInfoByName = @{}
    foreach ($pluginInfo in $pluginMetadata) {
        $pluginName = [string]$pluginInfo.name
        if ($pluginInfoByName.ContainsKey($pluginName)) {
            throw "build-info.json contains a duplicate plugin entry: $pluginName"
        }
        $pluginInfoByName[$pluginName] = $pluginInfo
    }

    $expectedCategories = @("Audio Module Class", "Component Controller Class") | Sort-Object
    $seenClassIds = @{}
    foreach ($name in $script:ExpectedPluginNames) {
        $bundle = Join-Path $root ($name + ".vst3")
        $metadata = Join-Path $bundle "Contents\Resources\moduleinfo.json"
        $moduleDirectory = Join-Path $bundle "Contents\x86_64-win"
        if (-not (Test-Path -LiteralPath $metadata -PathType Leaf)) {
            throw "Bundle is missing moduleinfo.json: $name"
        }
        if (-not (Test-Path -LiteralPath $moduleDirectory -PathType Container)) {
            throw "Bundle is missing its Windows x64 module directory: $name"
        }
        $modules = @(Get-ChildItem -LiteralPath $moduleDirectory -File -Filter "*.vst3" -ErrorAction Stop)
        if ($modules.Count -ne 1 -or $modules[0].Name -ne ($name + ".vst3")) {
            throw "Bundle must contain exactly one correctly named Windows VST3 module: $name"
        }

        $pluginInfo = $pluginInfoByName[$name]
        if ([string]$pluginInfo.version -ne [string]$info.version) {
            throw "build-info.json version mismatch for plugin: $name"
        }
        $moduleInfo = Get-Content -LiteralPath $metadata -Raw -ErrorAction Stop |
            ConvertFrom-Json -ErrorAction Stop
        if ([string]$moduleInfo.Name -ne $name) {
            throw "VST3 moduleinfo.json plugin name mismatch: $name"
        }
        if ([string]$moduleInfo.Version -ne [string]$info.version) {
            throw "VST3 moduleinfo.json version mismatch: $name"
        }
        $factoryInfo = $moduleInfo.'Factory Info'
        if ($null -eq $factoryInfo -or [string]$factoryInfo.Vendor -ne [string]$info.vendor) {
            throw "VST3 moduleinfo.json vendor mismatch: $name"
        }

        $classes = @($moduleInfo.Classes)
        if ($classes.Count -ne 2) {
            throw "VST3 moduleinfo.json must contain exactly two classes: $name"
        }
        $localClassIds = @()
        $categories = @()
        foreach ($class in $classes) {
            if ($null -eq $class) {
                throw "VST3 moduleinfo.json contains an empty class entry: $name"
            }
            $category = [string]$class.Category
            $classId = [string]$class.CID
            if ($category -notin $expectedCategories) {
                throw "VST3 moduleinfo.json contains an unexpected class category for $name"
            }
            if ($classId -notmatch '^[0-9A-Fa-f]{32}$') {
                throw "VST3 moduleinfo.json contains an invalid class ID for $name"
            }
            if ([string]$class.Name -ne $name -or
                [string]$class.Vendor -ne [string]$info.vendor -or
                [string]$class.Version -ne [string]$info.version) {
                throw "VST3 moduleinfo.json class identity mismatch: $name"
            }
            $normalizedClassId = $classId.ToUpperInvariant()
            if ($seenClassIds.ContainsKey($normalizedClassId)) {
                throw "Duplicate VST3 class ID across preview bundles: $normalizedClassId"
            }
            $seenClassIds[$normalizedClassId] = $name
            $localClassIds += $normalizedClassId
            $categories += $category
        }

        $actualCategories = @($categories | Sort-Object -Unique)
        if (($actualCategories -join [Environment]::NewLine) -ne
            ($expectedCategories -join [Environment]::NewLine)) {
            throw "VST3 moduleinfo.json must contain one processor and one controller class: $name"
        }
        $expectedClassIds = @($pluginInfo.class_ids | ForEach-Object { ([string]$_).ToUpperInvariant() } | Sort-Object)
        $actualClassIds = @($localClassIds | Sort-Object)
        if ($expectedClassIds.Count -ne 2 -or
            ($expectedClassIds -join [Environment]::NewLine) -ne
            ($actualClassIds -join [Environment]::NewLine)) {
            throw "VST3 moduleinfo.json class IDs do not match build-info.json: $name"
        }

        Test-SkellMawAmd64Pe -Path $modules[0].FullName
    }

    $sumPath = Join-Path $root "SHA256SUMS.txt"
    $sumLines = @(Get-Content -LiteralPath $sumPath -ErrorAction Stop)
    if ($sumLines.Count -eq 0) {
        throw "SHA256SUMS.txt is empty."
    }

    $trimChars = [char[]]@(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    )
    $rootPrefix = $root.TrimEnd($trimChars) + [System.IO.Path]::DirectorySeparatorChar
    $listed = @{}
    foreach ($line in $sumLines) {
        if ($line -notmatch '^([0-9a-fA-F]{64})  (.+)$') {
            throw "Malformed SHA256SUMS.txt entry: $line"
        }
        $expectedHash = $Matches[1].ToLowerInvariant()
        $relative = $Matches[2].Replace("/", "\")
        if ([System.IO.Path]::IsPathRooted($relative) -or
            $relative -match '(^|[\\/])\.\.([\\/]|$)' -or
            $relative -match '^[A-Za-z]:') {
            throw "Unsafe path in SHA256SUMS.txt: $relative"
        }
        if ($relative -eq "SHA256SUMS.txt") {
            throw "SHA256SUMS.txt must not checksum itself."
        }

        $fullPath = [System.IO.Path]::GetFullPath((Join-Path $root $relative))
        if (-not $fullPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Checksum path escapes the package directory: $relative"
        }
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            throw "Checksum entry refers to a missing file: $relative"
        }

        $key = $relative.Replace("\", "/").ToLowerInvariant()
        if ($listed.ContainsKey($key)) {
            throw "Duplicate checksum entry: $relative"
        }
        $listed[$key] = $true

        $actualHash = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
        if ($actualHash -ne $expectedHash) {
            throw "Checksum mismatch: $relative"
        }
    }

    $actualFiles = @(
        Get-ChildItem -LiteralPath $root -File -Force -Recurse -ErrorAction Stop |
            Where-Object { $_.FullName -ne $sumPath }
    )
    foreach ($file in $actualFiles) {
        $relative = $file.FullName.Substring($root.Length).TrimStart($trimChars).Replace("\", "/")
        if (-not $listed.ContainsKey($relative.ToLowerInvariant())) {
            throw "File is not covered by SHA256SUMS.txt: $relative"
        }
    }

    return [pscustomobject]@{
        Root = $root
        Info = $info
        PluginNames = @($script:ExpectedPluginNames)
    }
}

function Write-SkellMawManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [object]$Manifest
    )

    $json = ($Manifest | ConvertTo-Json -Depth 12) + [Environment]::NewLine
    $encoding = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($Path, $json, $encoding)
}

function Install-SkellMawPreview {
    [CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = "High")]
    param(
        [Parameter(Mandatory = $true)]
        [string]$PackagePath,
        [string]$Vst3Path,
        [string]$BackupRoot
    )

    if ($env:OS -ne "Windows_NT") {
        throw "The Skell Maw preview installer supports Windows only."
    }

    $package = Test-SkellMawPreviewPackage -PackagePath $PackagePath
    if ([string]::IsNullOrWhiteSpace($Vst3Path)) {
        $Vst3Path = Get-SkellMawDefaultVst3Path
    }
    if ([string]::IsNullOrWhiteSpace($BackupRoot)) {
        $BackupRoot = Get-SkellMawDefaultBackupRoot
    }

    $target = [System.IO.Path]::GetFullPath($Vst3Path)
    $backupBase = [System.IO.Path]::GetFullPath($BackupRoot)
    $trimChars = [char[]]@(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    )
    $targetPrefix = $target.TrimEnd($trimChars) + [System.IO.Path]::DirectorySeparatorChar
    $packagePrefix = $package.Root.TrimEnd($trimChars) + [System.IO.Path]::DirectorySeparatorChar
    $backupPrefix = $backupBase.TrimEnd($trimChars) + [System.IO.Path]::DirectorySeparatorChar

    if ($target -eq $package.Root -or $target.StartsWith($packagePrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        $package.Root.StartsWith($targetPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "The VST3 destination and preview package must be separate directories."
    }
    if ($backupBase -eq $target -or
        $backupBase.StartsWith($targetPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        $target.StartsWith($backupPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "BackupRoot and Vst3Path must be separate directories."
    }
    if ($backupBase -eq $package.Root -or
        $backupBase.StartsWith($packagePrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        $package.Root.StartsWith($backupPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "BackupRoot and preview package must be separate directories."
    }
    if (Test-Path -LiteralPath $backupBase) {
        $backupItem = Get-Item -LiteralPath $backupBase -Force -ErrorAction Stop
        if (-not $backupItem.PSIsContainer -or
            ($backupItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "BackupRoot must be a normal directory."
        }
    }
    $programFilesX86 = [System.Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
    foreach ($systemPath in @($env:ProgramFiles, $programFilesX86)) {
        if (-not [string]::IsNullOrWhiteSpace($systemPath)) {
            $systemRoot = [System.IO.Path]::GetFullPath($systemPath).TrimEnd($trimChars)
            if ($target -eq $systemRoot -or
                $target.StartsWith(($systemRoot + [System.IO.Path]::DirectorySeparatorChar), [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "System-wide Program Files installation is disabled; use the per-user VST3 directory."
            }
        }
    }
    if ($target -eq [System.IO.Path]::GetPathRoot($target)) {
        throw "Refusing to install into a drive root."
    }
    if (Test-Path -LiteralPath $target) {
        $targetItem = Get-Item -LiteralPath $target -Force -ErrorAction Stop
        if (-not $targetItem.PSIsContainer -or ($targetItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "VST3 destination must be a normal directory: $target"
        }
    }

    if (-not $PSCmdlet.ShouldProcess($target, "Install seven Skell Maw VST3 preview bundles with verified backups")) {
        return [pscustomobject]@{ Status = "whatif"; TargetDirectory = $target; ManifestPath = $null }
    }

    if (-not (Test-Path -LiteralPath $target -PathType Container)) {
        New-Item -ItemType Directory -Path $target -Force -ErrorAction Stop | Out-Null
    }
    New-Item -ItemType Directory -Path $backupBase -Force -ErrorAction Stop | Out-Null

    $runId = (Get-Date -Format "yyyyMMdd-HHmmss-fff") + "-" + [guid]::NewGuid().ToString("N")
    $runRoot = Join-Path $backupBase ("install-" + $runId)
    New-Item -ItemType Directory -Path $runRoot -ErrorAction Stop | Out-Null
    $previousRoot = Join-Path $runRoot "PreviousBundles"
    $rollbackRoot = Join-Path $runRoot "RolledBackPreview"
    $stageRoot = Join-Path $target (".skell-maw-stage-" + $runId)
    New-Item -ItemType Directory -Path $stageRoot -ErrorAction Stop | Out-Null

    $entries = [System.Collections.Generic.List[object]]::new()
    $manifestPath = Join-Path $runRoot "install-manifest.json"
    $manifest = $null

    try {
        foreach ($name in $script:ExpectedPluginNames) {
            $sourceBundle = Join-Path $package.Root ($name + ".vst3")
            $destination = Join-Path $target ($name + ".vst3")
            $stagedPath = Join-Path $stageRoot ($name + ".vst3")
            $hadPrevious = $false
            $backupPath = ""
            $originalHash = ""

            if (Test-Path -LiteralPath $destination) {
                $destinationItem = Get-Item -LiteralPath $destination -Force -ErrorAction Stop
                if (-not $destinationItem.PSIsContainer -or
                    ($destinationItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                    throw "Existing destination is not a safe VST3 directory: $destination"
                }
                $hadPrevious = $true
                $originalHash = Get-SkellMawTreeHash -Path $destination
                if (-not (Test-Path -LiteralPath $previousRoot -PathType Container)) {
                    New-Item -ItemType Directory -Path $previousRoot -ErrorAction Stop | Out-Null
                }
                $backupPath = Join-Path $previousRoot ($name + ".vst3")
                Copy-Item -LiteralPath $destination -Destination $backupPath -Recurse -ErrorAction Stop
                if ((Get-SkellMawTreeHash -Path $backupPath) -ne $originalHash) {
                    throw "Backup verification failed for existing bundle: $name"
                }
            }

            Copy-Item -LiteralPath $sourceBundle -Destination $stagedPath -Recurse -ErrorAction Stop
            $sourceHash = Get-SkellMawTreeHash -Path $sourceBundle
            $installedHash = Get-SkellMawTreeHash -Path $stagedPath
            if ($installedHash -ne $sourceHash) {
                throw "Staged bundle verification failed: $name"
            }

            $entries.Add([ordered]@{
                name = $name
                destination = $destination
                had_previous = $hadPrevious
                backup_path = $backupPath
                original_hash = $originalHash
                installed_hash = $installedHash
                staged_path = $stagedPath
                installed = $false
                rolled_back = $false
            })
        }

        $manifest = [ordered]@{
            schema_version = 1
            product = "Skell Maw"
            release_status = "preview-only; not a commercial release"
            package_version = [string]$package.Info.version
            package_source_commit = [string]$package.Info.source_commit
            created_utc = [DateTime]::UtcNow.ToString("o")
            status = "prepared"
            target_directory = $target
            backup_directory = $runRoot
            entries = @($entries.ToArray())
        }
        Write-SkellMawManifest -Path $manifestPath -Manifest $manifest

        foreach ($entry in $entries) {
            $destination = [string]$entry.destination
            $oldPath = Join-Path $stageRoot ("old-" + [guid]::NewGuid().ToString("N") + ".vst3")
            if (Test-Path -LiteralPath $destination) {
                Move-Item -LiteralPath $destination -Destination $oldPath -ErrorAction Stop
            }
            try {
                Move-Item -LiteralPath $entry.staged_path -Destination $destination -ErrorAction Stop
            }
            catch {
                if (Test-Path -LiteralPath $oldPath) {
                    Move-Item -LiteralPath $oldPath -Destination $destination -ErrorAction Stop
                }
                throw
            }

            $entry.installed = $true
            Write-SkellMawManifest -Path $manifestPath -Manifest $manifest
            if (Test-Path -LiteralPath $oldPath) {
                Remove-Item -LiteralPath $oldPath -Recurse -Force -ErrorAction Stop
            }
        }

        $manifest.status = "installed"
        Write-SkellMawManifest -Path $manifestPath -Manifest $manifest
        return [pscustomobject]@{
            Status = "installed"
            TargetDirectory = $target
            BackupDirectory = $runRoot
            ManifestPath = $manifestPath
            Plugins = @($script:ExpectedPluginNames)
        }
    }
    catch {
        $installError = $_.Exception.Message
        $rollbackErrors = [System.Collections.Generic.List[string]]::new()

        if ($null -ne $manifest) {
            if (-not (Test-Path -LiteralPath $rollbackRoot -PathType Container)) {
                New-Item -ItemType Directory -Path $rollbackRoot -Force -ErrorAction Stop | Out-Null
            }
            for ($index = $entries.Count - 1; $index -ge 0; $index--) {
                $entry = $entries[$index]
                if (-not $entry.installed) {
                    continue
                }
                try {
                    $destination = [string]$entry.destination
                    if (-not (Test-Path -LiteralPath $destination -PathType Container)) {
                        throw "Installed bundle is missing during rollback: $($entry.name)"
                    }
                    if ((Get-SkellMawTreeHash -Path $destination) -ne [string]$entry.installed_hash) {
                        throw "Installed bundle changed during rollback; refusing to overwrite: $($entry.name)"
                    }
                    $previewArchive = Join-Path $rollbackRoot ($entry.name + ".vst3")
                    if ($entry.had_previous) {
                        $restoreStage = Join-Path $stageRoot ("restore-" + [guid]::NewGuid().ToString("N") + ".vst3")
                        Copy-Item -LiteralPath $entry.backup_path -Destination $restoreStage -Recurse -ErrorAction Stop
                        if ((Get-SkellMawTreeHash -Path $restoreStage) -ne [string]$entry.original_hash) {
                            throw "Verified backup changed during rollback: $($entry.name)"
                        }
                        Move-Item -LiteralPath $destination -Destination $previewArchive -ErrorAction Stop
                        try {
                            Move-Item -LiteralPath $restoreStage -Destination $destination -ErrorAction Stop
                        }
                        catch {
                            if (Test-Path -LiteralPath $previewArchive) {
                                Move-Item -LiteralPath $previewArchive -Destination $destination -ErrorAction Stop
                            }
                            throw
                        }
                    }
                    else {
                        Move-Item -LiteralPath $destination -Destination $previewArchive -ErrorAction Stop
                    }
                    $entry.installed = $false
                    $entry.rolled_back = $true
                }
                catch {
                    $rollbackErrors.Add($_.Exception.Message)
                }
            }
            $manifest.status = if ($rollbackErrors.Count -eq 0) { "failed-and-rolled-back" } else { "rollback-incomplete" }
            try {
                Write-SkellMawManifest -Path $manifestPath -Manifest $manifest
            }
            catch {
                $rollbackErrors.Add("Could not update rollback manifest: " + $_.Exception.Message)
            }
        }

        if ($rollbackErrors.Count -gt 0) {
            throw ("Install failed: " + $installError + ". Automatic rollback needs attention: " +
                ($rollbackErrors -join "; ") + ". Preserve and inspect manifest: " + $manifestPath)
        }
        throw ("Install failed and prior changes were rolled back: " + $installError +
            ". Backup/manifest retained at: " + $manifestPath)
    }
    finally {
        if (Test-Path -LiteralPath $stageRoot) {
            Remove-Item -LiteralPath $stageRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Restore-SkellMawPreview {
    [CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = "High")]
    param(
        [Parameter(Mandatory = $true)]
        [string]$ManifestPath
    )

    if ($env:OS -ne "Windows_NT") {
        throw "The Skell Maw preview rollback helper supports Windows only."
    }

    $manifestFullPath = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $ManifestPath -ErrorAction Stop).Path)
    $manifest = Get-Content -LiteralPath $manifestFullPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ($manifest.product -ne "Skell Maw" -or $manifest.schema_version -ne 1) {
        throw "Unsupported Skell Maw install manifest."
    }
    if ($manifest.status -notin @("installed", "rolling-back", "rollback-incomplete")) {
        throw "Manifest is not an installed preview (status: $($manifest.status))."
    }

    $target = [System.IO.Path]::GetFullPath([string]$manifest.target_directory)
    $runRoot = [System.IO.Path]::GetFullPath([string]$manifest.backup_directory)
    $entries = @($manifest.entries)
    $manifestDirectory = [System.IO.Path]::GetFullPath((Split-Path -Parent $manifestFullPath))
    if ($manifestDirectory -ne $runRoot) {
        throw "Manifest must remain inside its original backup directory."
    }

    $expectedNames = @($script:ExpectedPluginNames | Sort-Object)
    $manifestNames = @($entries | ForEach-Object { [string]$_.name } | Sort-Object)
    if ($entries.Count -ne 7 -or
        ($manifestNames -join [Environment]::NewLine) -ne ($expectedNames -join [Environment]::NewLine)) {
        throw "Manifest must contain exactly the seven expected plugin entries."
    }

    $trimChars = [char[]]@(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    )
    if ($target -eq [System.IO.Path]::GetPathRoot($target)) {
        throw "Refusing to roll back into a drive root."
    }
    if (Test-Path -LiteralPath $target) {
        $targetItem = Get-Item -LiteralPath $target -Force -ErrorAction Stop
        if (-not $targetItem.PSIsContainer -or ($targetItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Manifest VST3 destination is not a normal directory."
        }
    }

    foreach ($entry in $entries) {
        $name = [string]$entry.name
        if ($name -notin $script:ExpectedPluginNames) {
            throw "Manifest contains an unexpected plugin name: $name"
        }
        $expectedDestination = [System.IO.Path]::GetFullPath((Join-Path $target ($name + ".vst3")))
        if ([System.IO.Path]::GetFullPath([string]$entry.destination) -ne $expectedDestination) {
            throw "Manifest destination does not match its plugin name: $name"
        }
        if ([string]$entry.installed_hash -notmatch '^[0-9a-fA-F]{64}$') {
            throw "Manifest installed checksum is invalid: $name"
        }
        if ($entry.had_previous) {
            $expectedBackup = [System.IO.Path]::GetFullPath((Join-Path $runRoot ("PreviousBundles\" + $name + ".vst3")))
            if ([System.IO.Path]::GetFullPath([string]$entry.backup_path) -ne $expectedBackup -or
                [string]$entry.original_hash -notmatch '^[0-9a-fA-F]{64}$') {
                throw "Manifest backup path or checksum is invalid: $name"
            }
        }
    }

    foreach ($entry in $entries) {
        if ($entry.rolled_back) {
            continue
        }
        $destination = [string]$entry.destination
        if (-not (Test-Path -LiteralPath $destination -PathType Container)) {
            if (-not $entry.had_previous) {
                continue
            }
            throw "Installed bundle is missing; refusing incomplete rollback: $($entry.name)"
        }

        $currentHash = Get-SkellMawTreeHash -Path $destination
        if ($entry.had_previous) {
            if ($currentHash -ne [string]$entry.installed_hash -and
                $currentHash -ne [string]$entry.original_hash) {
                throw "Bundle changed after preview install; refusing to overwrite user changes: $($entry.name)"
            }
            if ($currentHash -ne [string]$entry.original_hash) {
                if (-not (Test-Path -LiteralPath ([string]$entry.backup_path) -PathType Container)) {
                    throw "Original backup is missing: $($entry.name)"
                }
                if ((Get-SkellMawTreeHash -Path ([string]$entry.backup_path)) -ne [string]$entry.original_hash) {
                    throw "Original backup checksum mismatch: $($entry.name)"
                }
            }
        }
        elseif ($currentHash -ne [string]$entry.installed_hash) {
            throw "Bundle changed after preview install; refusing to move user changes: $($entry.name)"
        }
    }

    if (-not $PSCmdlet.ShouldProcess($target, "Restore the previous VST3 bundles or move new preview bundles into the rollback archive")) {
        return [pscustomobject]@{ Status = "whatif"; TargetDirectory = $target; ManifestPath = $manifestFullPath }
    }

    $rolledBackRoot = Join-Path $runRoot "RolledBackPreview"
    if (-not (Test-Path -LiteralPath $rolledBackRoot -PathType Container)) {
        New-Item -ItemType Directory -Path $rolledBackRoot -Force -ErrorAction Stop | Out-Null
    }
    $manifest.status = "rolling-back"
    Write-SkellMawManifest -Path $manifestFullPath -Manifest $manifest

    try {
        for ($index = $entries.Count - 1; $index -ge 0; $index--) {
            $entry = $entries[$index]
            if ($entry.rolled_back) {
                continue
            }
            $destination = [string]$entry.destination
            if (-not (Test-Path -LiteralPath $destination -PathType Container)) {
                $entry.rolled_back = $true
                Write-SkellMawManifest -Path $manifestFullPath -Manifest $manifest
                continue
            }

            $currentHash = Get-SkellMawTreeHash -Path $destination
            if ($entry.had_previous -and $currentHash -eq [string]$entry.original_hash) {
                $entry.rolled_back = $true
                Write-SkellMawManifest -Path $manifestFullPath -Manifest $manifest
                continue
            }

            $previewArchive = Join-Path $rolledBackRoot ($entry.name + ".vst3")
            if ($entry.had_previous) {
                $restoreStage = Join-Path $target (".skell-maw-restore-" + [guid]::NewGuid().ToString("N") + ".vst3")
                Copy-Item -LiteralPath ([string]$entry.backup_path) -Destination $restoreStage -Recurse -ErrorAction Stop
                if ((Get-SkellMawTreeHash -Path $restoreStage) -ne [string]$entry.original_hash) {
                    throw "Restore staging checksum mismatch: $($entry.name)"
                }
                Move-Item -LiteralPath $destination -Destination $previewArchive -ErrorAction Stop
                try {
                    Move-Item -LiteralPath $restoreStage -Destination $destination -ErrorAction Stop
                }
                catch {
                    if (Test-Path -LiteralPath $previewArchive) {
                        Move-Item -LiteralPath $previewArchive -Destination $destination -ErrorAction Stop
                    }
                    throw
                }
            }
            else {
                Move-Item -LiteralPath $destination -Destination $previewArchive -ErrorAction Stop
            }

            $entry.rolled_back = $true
            Write-SkellMawManifest -Path $manifestFullPath -Manifest $manifest
        }

        $manifest.status = "rolled-back"
        Write-SkellMawManifest -Path $manifestFullPath -Manifest $manifest
        return [pscustomobject]@{
            Status = "rolled-back"
            TargetDirectory = $target
            ManifestPath = $manifestFullPath
            ArchivedPreviewDirectory = $rolledBackRoot
        }
    }
    catch {
        $manifest.status = "rollback-incomplete"
        try {
            Write-SkellMawManifest -Path $manifestFullPath -Manifest $manifest
        }
        catch {
            Write-Warning ("Could not update rollback manifest: " + $_.Exception.Message)
        }
        throw ("Rollback stopped safely: " + $_.Exception.Message + ". Preserve manifest: " + $manifestFullPath)
    }
}

Export-ModuleMember -Function Get-SkellMawPreviewPluginNames, Get-SkellMawDefaultVst3Path, Get-SkellMawDefaultBackupRoot, Get-SkellMawTreeHash, Test-SkellMawPreviewPackage, Install-SkellMawPreview, Restore-SkellMawPreview
