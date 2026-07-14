[CmdletBinding()]
param(
    [string]$RunLabel = 'full_0_478_v1',
    [string]$OutputRoot = 'D:\code\reconstructOneFrame\output\07131838_quality_battle'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-Percentile {
    param([double[]]$Values, [double]$Probability)
    if ($Values.Count -eq 0) { return $null }
    $sorted = $Values | Sort-Object
    $position = ($sorted.Count - 1) * $Probability
    $lower = [Math]::Floor($position)
    $upper = [Math]::Ceiling($position)
    if ($lower -eq $upper) { return [double]$sorted[$lower] }
    $weight = $position - $lower
    [double]$sorted[$lower] * (1.0 - $weight) + [double]$sorted[$upper] * $weight
}

function Get-MetricStats {
    param([double[]]$Values)
    if ($Values.Count -eq 0) { return $null }
    [ordered]@{
        mean = [Math]::Round(($Values | Measure-Object -Average).Average, 6)
        p50 = [Math]::Round((Get-Percentile $Values 0.50), 6)
        p90 = [Math]::Round((Get-Percentile $Values 0.90), 6)
        p99 = [Math]::Round((Get-Percentile $Values 0.99), 6)
        min = [Math]::Round(($Values | Measure-Object -Minimum).Minimum, 6)
        max = [Math]::Round(($Values | Measure-Object -Maximum).Maximum, 6)
    }
}

function Get-PearsonCorrelation {
    param([double[]]$Left, [double[]]$Right)
    if ($Left.Count -ne $Right.Count -or $Left.Count -lt 2) { return $null }
    $leftMean = ($Left | Measure-Object -Average).Average
    $rightMean = ($Right | Measure-Object -Average).Average
    $covariance = 0.0
    $leftVariance = 0.0
    $rightVariance = 0.0
    for ($index = 0; $index -lt $Left.Count; ++$index) {
        $leftDelta = $Left[$index] - $leftMean
        $rightDelta = $Right[$index] - $rightMean
        $covariance += $leftDelta * $rightDelta
        $leftVariance += $leftDelta * $leftDelta
        $rightVariance += $rightDelta * $rightDelta
    }
    if ($leftVariance -le 0.0 -or $rightVariance -le 0.0) { return $null }
    [Math]::Round($covariance / [Math]::Sqrt($leftVariance * $rightVariance), 9)
}

function Import-ReplayRows {
    param([string]$Path)
    @(Import-Csv -LiteralPath $Path | ForEach-Object {
        [pscustomobject]@{
            frameId = [int]$_.frameId
            status = [int]$_.status
            loadMs = [double]$_.loadMs
            processMs = [double]$_.processMs
            validDepthPoints = [long]$_.validDepthPoints
            qualityAvailable = [int]$_.qualityAvailable -ne 0
            qualityScore = [double]$_.qualityScore
            qualityReasonBits = [uint32]$_.qualityReasonBits
            qualityType = [int]$_.qualityType
            error = $_.error
            summary = $_.summary
        }
    })
}

function Get-ReasonBitCounts {
    param([object[]]$Rows)
    $result = [ordered]@{}
    for ($bit = 0; $bit -lt 16; ++$bit) {
        $mask = [uint32](1 -shl $bit)
        $count = @($Rows | Where-Object { ($_.qualityReasonBits -band $mask) -ne 0U }).Count
        if ($count -gt 0) { $result["bit$bit"] = $count }
    }
    $result
}

$res1fCsv = Join-Path $OutputRoot ("res1f_{0}.csv" -f $RunLabel)
$legacyCsv = Join-Path $OutputRoot ("legacy_{0}.csv" -f $RunLabel)
$metadataPath = Join-Path $OutputRoot ("run_metadata_{0}.json" -f $RunLabel)
foreach ($path in @($res1fCsv, $legacyCsv, $metadataPath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing benchmark artifact: $path" }
}

$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
$rowsByBackend = [ordered]@{
    res1f = Import-ReplayRows $res1fCsv
    legacy = Import-ReplayRows $legacyCsv
}
$providers = [ordered]@{}
foreach ($backend in $rowsByBackend.Keys) {
    $rows = @($rowsByBackend[$backend])
    $successful = @($rows | Where-Object status -eq 0)
    $warm = @($successful | Where-Object frameId -ne $rows[0].frameId)
    $qualityRows = @($successful | Where-Object qualityAvailable)
    $runMetadata = @($metadata.runs | Where-Object backend -eq $backend)[0]
    $providers[$backend] = [ordered]@{
        frames = $rows.Count
        succeeded = $successful.Count
        failed = $rows.Count - $successful.Count
        totalValidDepthPoints = [long](($successful.validDepthPoints | Measure-Object -Sum).Sum)
        warmLoadMs = Get-MetricStats ([double[]]$warm.loadMs)
        warmProcessMs = Get-MetricStats ([double[]]$warm.processMs)
        wallMs = [double]$runMetadata.wallMs
        wallFps = [Math]::Round($rows.Count * 1000.0 / [double]$runMetadata.wallMs, 6)
        qualityFrames = $qualityRows.Count
        qualityScore = Get-MetricStats ([double[]]$qualityRows.qualityScore)
        qualityTypes = @($qualityRows | Group-Object qualityType | ForEach-Object {
            [ordered]@{ type = [int]$_.Name; frames = $_.Count }
        })
        reasonBitFrameCounts = Get-ReasonBitCounts $qualityRows
    }
}

$res1fByFrame = @{}
foreach ($row in $rowsByBackend.res1f) { $res1fByFrame[$row.frameId] = $row }
$legacyByFrame = @{}
foreach ($row in $rowsByBackend.legacy) { $legacyByFrame[$row.frameId] = $row }
$paired = @($res1fByFrame.Keys | Sort-Object | ForEach-Object {
    $frameId = $_
    if ($legacyByFrame.ContainsKey($frameId)) {
        $newRow = $res1fByFrame[$frameId]
        $oldRow = $legacyByFrame[$frameId]
        [pscustomobject]@{
            frameId = $frameId
            comparable = $newRow.status -eq 0 -and $oldRow.status -eq 0
            qualityComparable = $newRow.qualityAvailable -and $oldRow.qualityAvailable
            res1fPoints = $newRow.validDepthPoints
            legacyPoints = $oldRow.validDepthPoints
            processDifferenceMs = $newRow.processMs - $oldRow.processMs
            res1fQuality = $newRow.qualityScore
            legacyQuality = $oldRow.qualityScore
            qualityAbsDifference = [Math]::Abs($newRow.qualityScore - $oldRow.qualityScore)
            sameReasonBits = $newRow.qualityReasonBits -eq $oldRow.qualityReasonBits
        }
    }
})
$comparable = @($paired | Where-Object comparable)
$qualityComparable = @($comparable | Where-Object qualityComparable)
$res1fTotal = [long](($comparable.res1fPoints | Measure-Object -Sum).Sum)
$legacyTotal = [long](($comparable.legacyPoints | Measure-Object -Sum).Sum)
$comparison = [ordered]@{
    comparableFrames = $comparable.Count
    res1fFasterFrames = @($comparable | Where-Object processDifferenceMs -lt 0).Count
    legacyFasterFrames = @($comparable | Where-Object processDifferenceMs -gt 0).Count
    res1fToLegacyPointRatio = if ($legacyTotal -eq 0) { $null } else {
        [Math]::Round($res1fTotal / [double]$legacyTotal, 9)
    }
    qualityComparableFrames = $qualityComparable.Count
    qualityPearsonCorrelation = Get-PearsonCorrelation `
        ([double[]]$qualityComparable.res1fQuality) ([double[]]$qualityComparable.legacyQuality)
    qualityAbsoluteDifference = Get-MetricStats ([double[]]$qualityComparable.qualityAbsDifference)
    sameReasonBitsFrames = @($qualityComparable | Where-Object sameReasonBits).Count
}

$summary = [ordered]@{
    runLabel = $RunLabel
    sourceRoot = $metadata.sourceRoot
    generatedAt = (Get-Date).ToString('o')
    res1fCommit = $metadata.res1fCommit
    legacyCommit = $metadata.legacyCommit
    legacyOriginMain = $metadata.legacyOriginMain
    warmupPolicy = 'Exclude the first frame id from successful-frame latency statistics.'
    providers = $providers
    comparison = $comparison
}
$summaryPath = Join-Path $OutputRoot ("summary_{0}.json" -f $RunLabel)
[System.IO.File]::WriteAllText(
    $summaryPath,
    ($summary | ConvertTo-Json -Depth 16) + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false))
Write-Host ($summary | ConvertTo-Json -Depth 16)
Write-Host "summary=$summaryPath"
