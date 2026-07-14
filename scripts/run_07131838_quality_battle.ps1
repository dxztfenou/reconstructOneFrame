[CmdletBinding()]
param(
    [int]$FirstFrame = 0,
    [int]$LastFrame = 478,
    [ValidateSet('metal', 'non-metal')]
    [string]$ScanMode = 'metal',
    [ValidateSet('res1f-first', 'legacy-first')]
    [string]$ProviderOrder = 'res1f-first',
    [bool]$Clear255Enabled = $true,
    [string]$RunLabel = 'full_0_478_v1',
    [string]$SourceRoot = 'D:\code\dentalscanserviceinterface\build\Release\output_test\output 07131838_teeth\Upper',
    [string]$CalibrationRoot = 'D:\Data\Calib\2607021545_mach6',
    [string]$OutputRoot = 'D:\code\reconstructOneFrame\output\07131838_quality_battle'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$res1fRoot = 'D:\code\reconstructOneFrame'
$dssiRoot = 'D:\code\_worktrees\dssi-adapt-res1f'
$legacyRoot = 'D:\code\teethscanalgorithm3x4-main'
$replayExe = Join-Path $dssiRoot 'build\ServiceInterface\Release\reconstruction_provider_replay.exe'
$res1fDll = Join-Path $res1fRoot 'build\Release\reconstructOneFrame.dll'
$res1fConfigSource = Join-Path $res1fRoot 'config\reconsAlgPara.json'
$res1fCalibration = Join-Path $CalibrationRoot 'calibResult.json'
$legacyDllSource = Join-Path $legacyRoot 'build\bin\Release\TeethScanAlgorithm.dll'
$legacyLogSource = Join-Path $legacyRoot 'build\bin\Release\Log.dll'
$legacyOpenCvSource = 'D:\code\ThirdPartyLibraries\opencv453\x64\vc15\bin\opencv_world453.dll'
$legacyTbbSource = 'D:\code\ThirdPartyLibraries\oneapi-tbb-2022.0.0\redist\intel64\vc14\tbb12.dll'
$legacyTensorRtSource = 'D:\code\ThirdPartyLibraries\TensorRT-8.6.0.12\bin\nvinfer.dll'
$legacyConfigSource = Join-Path $legacyRoot 'config\reconsAlgPara.json'
$legacyCalibration = Join-Path $CalibrationRoot 'calibParams.yml'

foreach ($requiredPath in @(
        $replayExe,
        $res1fDll,
        $res1fConfigSource,
        $res1fCalibration,
        $legacyDllSource,
        $legacyLogSource,
        $legacyOpenCvSource,
        $legacyTbbSource,
        $legacyTensorRtSource,
        $legacyConfigSource,
        $legacyCalibration,
        $SourceRoot)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required path does not exist: $requiredPath"
    }
}
if ($FirstFrame -gt $LastFrame) {
    throw 'FirstFrame must be less than or equal to LastFrame.'
}

function Set-JsonProperty {
    param(
        [Parameter(Mandatory)] [object]$Object,
        [Parameter(Mandatory)] [string]$Name,
        [Parameter(Mandatory)] $Value
    )
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        $Object | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
    }
    else {
        $property.Value = $Value
    }
}

function Write-JsonConfig {
    param(
        [Parameter(Mandatory)] [object]$Value,
        [Parameter(Mandatory)] [string]$Path
    )
    [System.IO.File]::WriteAllText(
        $Path,
        ($Value | ConvertTo-Json -Depth 64) + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false))
}

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

$legacyRuntimeRoot = Join-Path $OutputRoot 'legacy_runtime_latest'
$legacyRuntimeBin = Join-Path $legacyRuntimeRoot 'bin'
New-Item -ItemType Directory -Path $legacyRuntimeBin -Force | Out-Null
foreach ($runtimeFile in @(
        $legacyDllSource,
        $legacyLogSource,
        $legacyOpenCvSource,
        $legacyTbbSource,
        $legacyTensorRtSource)) {
    Copy-Item -LiteralPath $runtimeFile -Destination $legacyRuntimeBin -Force
}
$legacyDll = Join-Path $legacyRuntimeBin 'TeethScanAlgorithm.dll'

$res1fConfig = Join-Path $OutputRoot 'res1f_quality_enabled.json'
$res1fConfigObject = Get-Content -LiteralPath $res1fConfigSource -Raw | ConvertFrom-Json
Set-JsonProperty $res1fConfigObject 'qualityInfoEnabled' $true
Set-JsonProperty $res1fConfigObject 'qualityInfoReasonChannelEnabled' $true
Set-JsonProperty $res1fConfigObject 'qualityInfoUseCandidateCount' $false
Set-JsonProperty $res1fConfigObject 'qualityInfoUseMatchCost' $true
Set-JsonProperty $res1fConfigObject 'qualityInfoUseModulation' $true
Set-JsonProperty $res1fConfigObject 'clear255' $Clear255Enabled
Set-JsonProperty $res1fConfigObject 'clear255DilateRadius' 3
Set-JsonProperty $res1fConfigObject 'colorHighlightCompressionEnabled' $true
Write-JsonConfig $res1fConfigObject $res1fConfig

$legacyRuntimeConfigDirectory = Join-Path $legacyRuntimeRoot 'config'
New-Item -ItemType Directory -Path $legacyRuntimeConfigDirectory -Force | Out-Null
$legacyConfig = Join-Path $legacyRuntimeConfigDirectory 'reconsAlgPara.json'
$legacyConfigObject = Get-Content -LiteralPath $legacyConfigSource -Raw | ConvertFrom-Json
Set-JsonProperty $legacyConfigObject 'calibParamsPath' $legacyCalibration.Replace('\', '/')
Set-JsonProperty $legacyConfigObject 'qualityInfoEnabled' $true
Set-JsonProperty $legacyConfigObject 'qualityInfoReasonChannelEnabled' $true
Set-JsonProperty $legacyConfigObject 'clear255' $Clear255Enabled
Set-JsonProperty $legacyConfigObject 'clear255DilateRadius' 3
Set-JsonProperty $legacyConfigObject 'colorHighlightCompressionEnabled' $true
Write-JsonConfig $legacyConfigObject $legacyConfig

function Invoke-ProviderReplay {
    param(
        [Parameter(Mandatory)] [string]$Backend,
        [Parameter(Mandatory)] [string]$DllPath,
        [Parameter(Mandatory)] [string]$ConfigPath,
        [Parameter(Mandatory)] [string]$CalibrationPath,
        [Parameter(Mandatory)] [string]$WorkingDirectory
    )

    $csvPath = Join-Path $OutputRoot ("{0}_{1}.csv" -f $Backend, $RunLabel)
    $logPath = Join-Path $OutputRoot ("{0}_{1}.log" -f $Backend, $RunLabel)
    $arguments = @(
        $Backend,
        $DllPath,
        $ConfigPath,
        $CalibrationPath,
        $SourceRoot,
        $FirstFrame,
        $LastFrame,
        $csvPath,
        $ScanMode)

    Push-Location -LiteralPath $WorkingDirectory
    try {
        $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        $consoleOutput = & $replayExe @arguments 2>&1
        $exitCode = $LASTEXITCODE
        $stopwatch.Stop()
    }
    finally {
        Pop-Location
    }
    $consoleText = ($consoleOutput | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
    [System.IO.File]::WriteAllText(
        $logPath,
        $consoleText + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false))
    $consoleOutput | ForEach-Object { Write-Host $_ }
    if ($exitCode -ne 0) {
        throw "$Backend replay failed with exit code $exitCode. See $logPath"
    }

    [pscustomobject]@{
        backend = $Backend
        scanMode = $ScanMode
        firstFrame = $FirstFrame
        lastFrame = $LastFrame
        wallMs = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds, 6)
        csv = $csvPath
        log = $logPath
        dll = $DllPath
        dllSha256 = (Get-FileHash -LiteralPath $DllPath -Algorithm SHA256).Hash
        config = $ConfigPath
        configSha256 = (Get-FileHash -LiteralPath $ConfigPath -Algorithm SHA256).Hash
        calibration = $CalibrationPath
        calibrationSha256 = (Get-FileHash -LiteralPath $CalibrationPath -Algorithm SHA256).Hash
        workingDirectory = $WorkingDirectory
    }
}

if ($ProviderOrder -eq 'res1f-first') {
    $runs = @(
        Invoke-ProviderReplay -Backend 'res1f' -DllPath $res1fDll `
            -ConfigPath $res1fConfig -CalibrationPath $res1fCalibration `
            -WorkingDirectory $res1fRoot
        Invoke-ProviderReplay -Backend 'legacy' -DllPath $legacyDll `
            -ConfigPath $legacyConfig -CalibrationPath $legacyCalibration `
            -WorkingDirectory $legacyRuntimeRoot
    )
}
else {
    $runs = @(
        Invoke-ProviderReplay -Backend 'legacy' -DllPath $legacyDll `
            -ConfigPath $legacyConfig -CalibrationPath $legacyCalibration `
            -WorkingDirectory $legacyRuntimeRoot
        Invoke-ProviderReplay -Backend 'res1f' -DllPath $res1fDll `
            -ConfigPath $res1fConfig -CalibrationPath $res1fCalibration `
            -WorkingDirectory $res1fRoot
    )
}

$metadataPath = Join-Path $OutputRoot ("run_metadata_{0}.json" -f $RunLabel)
$metadata = [ordered]@{
    generatedAt = (Get-Date).ToString('o')
    sourceRoot = $SourceRoot
    sourceFrameCount = $LastFrame - $FirstFrame + 1
    providerOrder = $ProviderOrder
    clear255Enabled = $Clear255Enabled
    res1fCommit = (git -C $res1fRoot rev-parse HEAD).Trim()
    legacyCommit = (git -C $legacyRoot rev-parse HEAD).Trim()
    legacyOriginMain = (git -C $legacyRoot rev-parse origin/main).Trim()
    res1fConfigSource = $res1fConfigSource
    legacyConfigSource = $legacyConfigSource
    runs = $runs
}
Write-JsonConfig $metadata $metadataPath
Write-Host "metadata=$metadataPath"
