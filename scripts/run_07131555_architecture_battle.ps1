[CmdletBinding()]
param(
    [int]$FirstFrame = 0,
    [int]$LastFrame = 294,
    [ValidateSet('metal', 'non-metal')]
    [string]$ScanMode = 'metal',
    [ValidateSet('res1f-first', 'legacy-first')]
    [string]$ProviderOrder = 'res1f-first',
    [string]$RunLabel = 'full_0_294',
    [string]$SourceRoot = 'D:\code\dentalscanserviceinterface\build\Release\output_test\output 07131555_teeth\Upper',
    [string]$CalibrationRoot = 'D:\Data\Calib\2607021545_mach6',
    [string]$OutputRoot = 'D:\code\reconstructOneFrame\output\07131555_architecture_battle'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$res1fRoot = 'D:\code\reconstructOneFrame'
$dssiRoot = 'D:\code\_worktrees\dssi-adapt-res1f'
$legacyRoot = 'D:\code\teethscanalgorithm3x4'
$replayExe = Join-Path $dssiRoot 'build\ServiceInterface\Release\reconstruction_provider_replay.exe'
$res1fDll = Join-Path $res1fRoot 'build\Release\reconstructOneFrame.dll'
$res1fConfig = Join-Path $res1fRoot 'config\reconsAlgPara.json'
$res1fCalibration = Join-Path $CalibrationRoot 'calibResult.json'
$legacyDll = Join-Path $legacyRoot 'build\bin\Release\TeethScanAlgorithm.dll'
$legacyConfigSource = 'D:\code\dentalscanserviceinterface\build\Release\reconsAlgPara.json'
$legacyCalibration = Join-Path $CalibrationRoot 'calibParams.yml'

foreach ($requiredPath in @(
        $replayExe,
        $res1fDll,
        $res1fConfig,
        $res1fCalibration,
        $legacyDll,
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

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$legacyRuntimeRoot = Join-Path $OutputRoot 'legacy_runtime_2607021545'
$legacyRuntimeConfigDirectory = Join-Path $legacyRuntimeRoot 'config'
New-Item -ItemType Directory -Path $legacyRuntimeConfigDirectory -Force | Out-Null
$legacyConfig = Join-Path $legacyRuntimeConfigDirectory 'reconsAlgPara.json'
$configObject = Get-Content -LiteralPath $legacyConfigSource -Raw | ConvertFrom-Json
$configObject.calibParamsPath = $legacyCalibration.Replace('\', '/')
$configJson = $configObject | ConvertTo-Json -Depth 64
[System.IO.File]::WriteAllText(
    $legacyConfig,
    $configJson + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false))

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
    legacyConfigSource = $legacyConfigSource
    legacyConfigSourceSha256 = (Get-FileHash -LiteralPath $legacyConfigSource -Algorithm SHA256).Hash
    runs = $runs
}
[System.IO.File]::WriteAllText(
    $metadataPath,
    ($metadata | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false))
Write-Host "metadata=$metadataPath"
