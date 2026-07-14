[CmdletBinding()]
param(
    [string]$RunLabel = 'full_0_294',
    [string]$OutputRoot = 'D:\code\reconstructOneFrame\output\07131555_architecture_battle'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-Percentile {
    param(
        [Parameter(Mandatory)] [double[]]$Values,
        [Parameter(Mandatory)] [ValidateRange(0.0, 1.0)] [double]$Probability
    )
    if ($Values.Count -eq 0) {
        return $null
    }
    $sorted = $Values | Sort-Object
    $position = ($sorted.Count - 1) * $Probability
    $lower = [Math]::Floor($position)
    $upper = [Math]::Ceiling($position)
    if ($lower -eq $upper) {
        return [double]$sorted[$lower]
    }
    $weight = $position - $lower
    return [double]$sorted[$lower] * (1.0 - $weight) + [double]$sorted[$upper] * $weight
}

function Get-MetricStats {
    param([Parameter(Mandatory)] [double[]]$Values)
    if ($Values.Count -eq 0) {
        return $null
    }
    [ordered]@{
        mean = [Math]::Round(($Values | Measure-Object -Average).Average, 6)
        p50 = [Math]::Round((Get-Percentile -Values $Values -Probability 0.50), 6)
        p90 = [Math]::Round((Get-Percentile -Values $Values -Probability 0.90), 6)
        p99 = [Math]::Round((Get-Percentile -Values $Values -Probability 0.99), 6)
        min = [Math]::Round(($Values | Measure-Object -Minimum).Minimum, 6)
        max = [Math]::Round(($Values | Measure-Object -Maximum).Maximum, 6)
    }
}

function Import-ReplayRows {
    param([Parameter(Mandatory)] [string]$Path)
    @(Import-Csv -LiteralPath $Path | ForEach-Object {
        [pscustomobject]@{
            frameId = [int]$_.frameId
            status = [int]$_.status
            loadMs = [double]$_.loadMs
            processMs = [double]$_.processMs
            validDepthPoints = [long]$_.validDepthPoints
            error = $_.error
            summary = $_.summary
        }
    })
}

$res1fCsv = Join-Path $OutputRoot ("res1f_{0}.csv" -f $RunLabel)
$legacyCsv = Join-Path $OutputRoot ("legacy_{0}.csv" -f $RunLabel)
$metadataPath = Join-Path $OutputRoot ("run_metadata_{0}.json" -f $RunLabel)
foreach ($path in @($res1fCsv, $legacyCsv, $metadataPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required benchmark artifact does not exist: $path"
    }
}

$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
$rowsByBackend = [ordered]@{
    res1f = Import-ReplayRows -Path $res1fCsv
    legacy = Import-ReplayRows -Path $legacyCsv
}
$summaries = [ordered]@{}
foreach ($backend in $rowsByBackend.Keys) {
    $rows = @($rowsByBackend[$backend])
    $successful = @($rows | Where-Object status -eq 0)
    $warm = @($successful | Where-Object frameId -ne $rows[0].frameId)
    $runMetadata = @($metadata.runs | Where-Object backend -eq $backend)[0]
    $summaries[$backend] = [ordered]@{
        frames = $rows.Count
        succeeded = $successful.Count
        failed = $rows.Count - $successful.Count
        successRate = [Math]::Round($successful.Count / [double]$rows.Count, 9)
        totalValidDepthPoints = [long](($successful.validDepthPoints | Measure-Object -Sum).Sum)
        warmFrameCount = $warm.Count
        warmLoadMs = Get-MetricStats -Values ([double[]]$warm.loadMs)
        warmProcessMs = Get-MetricStats -Values ([double[]]$warm.processMs)
        wallMs = [double]$runMetadata.wallMs
        wallFps = [Math]::Round($rows.Count * 1000.0 / [double]$runMetadata.wallMs, 6)
    }
}

$res1fByFrame = @{}
foreach ($row in $rowsByBackend.res1f) { $res1fByFrame[$row.frameId] = $row }
$legacyByFrame = @{}
foreach ($row in $rowsByBackend.legacy) { $legacyByFrame[$row.frameId] = $row }
$paired = foreach ($frameId in $res1fByFrame.Keys | Sort-Object) {
    if ($legacyByFrame.ContainsKey($frameId)) {
        $newRow = $res1fByFrame[$frameId]
        $oldRow = $legacyByFrame[$frameId]
        [pscustomobject]@{
            frameId = $frameId
            res1fStatus = $newRow.status
            legacyStatus = $oldRow.status
            res1fPoints = $newRow.validDepthPoints
            legacyPoints = $oldRow.validDepthPoints
            pointDifference = $newRow.validDepthPoints - $oldRow.validDepthPoints
            processDifferenceMs = $newRow.processMs - $oldRow.processMs
        }
    }
}
$comparable = @($paired | Where-Object { $_.res1fStatus -eq 0 -and $_.legacyStatus -eq 0 })
$res1fTotal = [long](($comparable.res1fPoints | Measure-Object -Sum).Sum)
$legacyTotal = [long](($comparable.legacyPoints | Measure-Object -Sum).Sum)
$comparison = [ordered]@{
    comparableFrames = $comparable.Count
    res1fFasterFrames = @($comparable | Where-Object processDifferenceMs -lt 0).Count
    legacyFasterFrames = @($comparable | Where-Object processDifferenceMs -gt 0).Count
    tiedProcessFrames = @($comparable | Where-Object processDifferenceMs -eq 0).Count
    res1fMorePointsFrames = @($comparable | Where-Object pointDifference -gt 0).Count
    legacyMorePointsFrames = @($comparable | Where-Object pointDifference -lt 0).Count
    tiedPointFrames = @($comparable | Where-Object pointDifference -eq 0).Count
    res1fComparablePoints = $res1fTotal
    legacyComparablePoints = $legacyTotal
    res1fToLegacyPointRatio = if ($legacyTotal -eq 0) { $null } else { [Math]::Round($res1fTotal / [double]$legacyTotal, 9) }
}

$summary = [ordered]@{
    runLabel = $RunLabel
    sourceRoot = $metadata.sourceRoot
    generatedAt = (Get-Date).ToString('o')
    warmupPolicy = 'Exclude the first frame id from successful-frame latency statistics.'
    providers = $summaries
    comparison = $comparison
}
$summaryPath = Join-Path $OutputRoot ("summary_{0}.json" -f $RunLabel)
[System.IO.File]::WriteAllText(
    $summaryPath,
    ($summary | ConvertTo-Json -Depth 12) + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false))
Write-Host ($summary | ConvertTo-Json -Depth 12)
Write-Host "summary=$summaryPath"
