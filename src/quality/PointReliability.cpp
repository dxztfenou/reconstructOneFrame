#include "quality/PointReliability.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <sstream>

namespace reconstruct_one_frame {

namespace {

double clamp01(const double value)
{
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::max(0.0, std::min(1.0, value));
}

bool isFrameQualitySentinel(const int x, const int y)
{
    return x == 0 && y == 0;
}

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

Rect centerRoiForFrame(const int width, const int height)
{
    if (width <= 0 || height <= 0) {
        return {};
    }
    const int x0 = width / 5;
    const int y0 = height / 5;
    const int x1 = width - x0;
    const int y1 = height - y0;
    if (x1 <= x0 || y1 <= y0) {
        return {0, 0, width, height};
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

bool contains(const Rect& rect, const int x, const int y)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

double percentileFromSorted(const std::vector<double>& sorted, const double percentile)
{
    if (sorted.empty()) {
        return 0.0;
    }
    const double clamped = clamp01(percentile);
    const double position = clamped * static_cast<double>(sorted.size() - 1);
    const auto lowerIndex = static_cast<std::size_t>(std::floor(position));
    const auto upperIndex = static_cast<std::size_t>(std::ceil(position));
    if (lowerIndex == upperIndex) {
        return sorted[lowerIndex];
    }
    const double weight = position - static_cast<double>(lowerIndex);
    return sorted[lowerIndex] * (1.0 - weight) + sorted[upperIndex] * weight;
}

void addReasonBitsToHistogram(const unsigned int reason, SingleFrameQualityStats& stats)
{
    for (int bit = 0; bit < kPointReliabilityReasonBitCount; ++bit) {
        const unsigned int mask = 1U << static_cast<unsigned int>(bit);
        if ((reason & mask) != 0U) {
            ++stats.reasonHistogram[static_cast<std::size_t>(bit)];
        }
    }
}

bool validPoint(const PointCloudGridPoint& point, const ReconsConfig& config)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) &&
           !(point.x == 0.0F && point.y == 0.0F && point.z == 0.0F) &&
           point.z >= static_cast<float>(config.minZ) &&
           point.z <= static_cast<float>(config.maxZ);
}

bool hasInvalidDepthNeighbor(const std::vector<PointCloudGridPoint>& points,
                             const int width,
                             const int height,
                             const int x,
                             const int y,
                             const int radius,
                             const ReconsConfig& config)
{
    const int clampedRadius = std::max(1, std::min(radius, 3));
    for (int dy = -clampedRadius; dy <= clampedRadius; ++dy) {
        const int yy = y + dy;
        if (yy < 0 || yy >= height) {
            continue;
        }
        for (int dx = -clampedRadius; dx <= clampedRadius; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            const int xx = x + dx;
            if (xx < 0 || xx >= width) {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(yy) * static_cast<std::size_t>(width) +
                                      static_cast<std::size_t>(xx);
            if (!validPoint(points[index], config)) {
                return true;
            }
        }
    }
    return false;
}

void summarizeValidSupport(const std::vector<unsigned char>& validMask,
                           const int width,
                           const int height,
                           SingleFrameQualityStats& stats)
{
    if (width <= 0 || height <= 0 || validMask.empty()) {
        return;
    }

    std::vector<unsigned char> visited(validMask.size(), 0U);
    std::queue<std::pair<int, int>> queue;
    constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    constexpr int kDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t start = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                      static_cast<std::size_t>(x);
            if (validMask[start] == 0U || visited[start] != 0U) {
                continue;
            }

            int area = 0;
            visited[start] = 1U;
            queue.push({x, y});
            while (!queue.empty()) {
                const auto [cx, cy] = queue.front();
                queue.pop();
                ++area;
                for (int i = 0; i < 8; ++i) {
                    const int nx = cx + kDx[i];
                    const int ny = cy + kDy[i];
                    if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
                        continue;
                    }
                    const std::size_t ni = static_cast<std::size_t>(ny) * static_cast<std::size_t>(width) +
                                           static_cast<std::size_t>(nx);
                    if (validMask[ni] != 0U && visited[ni] == 0U) {
                        visited[ni] = 1U;
                        queue.push({nx, ny});
                    }
                }
            }
            stats.largestConnectedComponentPoints = std::max(stats.largestConnectedComponentPoints, area);
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                      static_cast<std::size_t>(x);
            if (validMask[index] == 0U) {
                continue;
            }

            bool touchesInvalid = false;
            if (x > 0) {
                touchesInvalid = touchesInvalid || validMask[index - 1] == 0U;
            }
            if (x + 1 < width) {
                touchesInvalid = touchesInvalid || validMask[index + 1] == 0U;
            }
            if (y > 0) {
                touchesInvalid = touchesInvalid || validMask[index - static_cast<std::size_t>(width)] == 0U;
            }
            if (y + 1 < height) {
                touchesInvalid = touchesInvalid || validMask[index + static_cast<std::size_t>(width)] == 0U;
            }
            if (touchesInvalid) {
                ++stats.boundaryValidPoints;
            }
        }
    }
}

void finalizeFrameQualityScores(SingleFrameQualityStats& stats)
{
    if (stats.totalPixels > 0) {
        stats.validCoverageRatio = static_cast<double>(stats.validPoints) / static_cast<double>(stats.totalPixels);
        stats.coverageScore = clamp01(stats.validCoverageRatio / kDefaultFrameCoverageTargetRatio);
    }

    if (stats.centerPixels > 0) {
        stats.centerCoverageRatio = static_cast<double>(stats.centerValidPoints) / static_cast<double>(stats.centerPixels);
        stats.centerCoverageScore = clamp01(stats.centerCoverageRatio / kDefaultFrameCenterCoverageTargetRatio);
    }

    if (stats.validPoints > 0) {
        stats.largestConnectedComponentRatio =
            static_cast<double>(stats.largestConnectedComponentPoints) / static_cast<double>(stats.validPoints);
        stats.boundaryRatio = static_cast<double>(stats.boundaryValidPoints) / static_cast<double>(stats.validPoints);
        stats.connectedSurfaceScore = clamp01(stats.largestConnectedComponentRatio * (1.0 - stats.boundaryRatio));
        stats.pointQualityScore = clamp01(0.50 * (stats.meanScore / 0.65) + 0.50 * (stats.p90Score / 0.90));
    }

    const double effectiveCenterSupportScore = clamp01(0.35 + 0.65 * stats.centerCoverageScore);
    const double hardSupportScore = std::min(stats.coverageScore, effectiveCenterSupportScore);
    double spatialPenalty = 1.0;
    if (stats.validPoints > 0) {
        const double fragmentationPenalty = clamp01((0.70 - stats.largestConnectedComponentRatio) / 0.70);
        const double boundaryPenalty = clamp01((stats.boundaryRatio - 0.15) / 0.45);
        spatialPenalty = clamp01(1.0 - 0.25 * fragmentationPenalty - 0.15 * boundaryPenalty);
    }

    stats.frameQualityScore = std::min(hardSupportScore, stats.pointQualityScore * spatialPenalty);

    if (stats.coverageScore < 0.80 || stats.centerCoverageScore < 0.80) {
        stats.lowQualityReasonBits |= ReasonDepthRange;
    }
    if (stats.connectedSurfaceScore < 0.80) {
        stats.lowQualityReasonBits |= ReasonLocalDiscontinuity;
        stats.lowQualityReasonBits |= ReasonHoleEdge;
    }
}

SingleFrameQualityStats summarizePointReliabilityMap(const PointReliabilityMap& map,
                                                     const double highConfidenceThreshold)
{
    SingleFrameQualityStats stats;
    if (map.width <= 0 || map.height <= 0 || map.pixels.size() != static_cast<std::size_t>(map.width) * map.height) {
        return stats;
    }

    std::vector<double> validScores;
    validScores.reserve(map.pixels.size());
    const double threshold = clamp01(highConfidenceThreshold);
    double scoreSum = 0.0;
    std::vector<unsigned char> validMask(map.pixels.size(), 0U);
    const Rect centerRoi = centerRoiForFrame(map.width, map.height);

    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            if (isFrameQualitySentinel(x, y)) {
                continue;
            }

            ++stats.totalPixels;
            const bool inCenterRoi = contains(centerRoi, x, y);
            if (inCenterRoi) {
                ++stats.centerPixels;
            }

            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(map.width) +
                                      static_cast<std::size_t>(x);
            const PointReliabilityPixel& pixel = map.pixels[index];
            const double score = clamp01(pixel.score);
            const unsigned int reason = pixel.reason;
            const bool semanticBackground = (reason & ReasonSemanticBackground) != 0U;

            if (score > 0.0) {
                ++stats.validPoints;
                scoreSum += score;
                validScores.push_back(score);
                validMask[index] = 1U;
                if (inCenterRoi) {
                    ++stats.centerValidPoints;
                }
                if (score >= threshold) {
                    ++stats.highConfidencePoints;
                }
            }

            if (!semanticBackground && score < threshold && reason != ReasonNone) {
                stats.lowQualityReasonBits |= reason;
                addReasonBitsToHistogram(reason, stats);
            }
        }
    }

    summarizeValidSupport(validMask, map.width, map.height, stats);

    if (stats.validPoints > 0) {
        stats.highConfidenceRatio =
            static_cast<double>(stats.highConfidencePoints) / static_cast<double>(stats.validPoints);
        stats.meanScore = scoreSum / static_cast<double>(stats.validPoints);
        std::sort(validScores.begin(), validScores.end());
        stats.p50Score = percentileFromSorted(validScores, 0.50);
        stats.p90Score = percentileFromSorted(validScores, 0.90);
        stats.p95Score = percentileFromSorted(validScores, 0.95);
    }

    finalizeFrameQualityScores(stats);
    return stats;
}

} // namespace

PointReliabilityResult evaluatePointReliability(const PointCloudReconstructionResult& pointCloud,
                                                const ReconsConfig& config)
{
    PointReliabilityResult result;
    result.stats.stageName = "quality_evaluate";
    if (!pointCloud.status.ok()) {
        result.status = pointCloud.status;
        result.stats.status = result.status;
        return result;
    }
    if (pointCloud.width <= 0 || pointCloud.height <= 0 ||
        pointCloud.gridPoints.size() != static_cast<std::size_t>(pointCloud.width) * pointCloud.height) {
        result.status = {StatusCode::DataQualityInsufficient, "PointReliability", "point cloud grid is required for quality evaluation"};
        result.stats.status = result.status;
        return result;
    }

    result.map.width = pointCloud.width;
    result.map.height = pointCloud.height;
    result.map.pixels.assign(pointCloud.gridPoints.size(), {});

    for (int y = 0; y < pointCloud.height; ++y) {
        for (int x = 0; x < pointCloud.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(pointCloud.width) +
                                      static_cast<std::size_t>(x);
            PointReliabilityPixel pixel;
            unsigned int reason = ReasonNone;
            double quality = 1.0;
            const PointCloudGridPoint& point = pointCloud.gridPoints[index];

            if (!validPoint(point, config)) {
                reason |= ReasonDepthRange;
                quality = 0.0;
            }

            if (config.qualityInfoUseMatchCost && std::isfinite(point.matchCost)) {
                if (point.matchCost > static_cast<float>(config.qualityInfoPhaseCostThreshold)) {
                    reason |= ReasonHighMatchCost;
                    quality = 0.0;
                } else if (config.qualityInfoPhaseCostThreshold > 0.0) {
                    quality = std::min(quality, clamp01(1.0 - static_cast<double>(point.matchCost) / config.qualityInfoPhaseCostThreshold));
                }
            }

            if (config.qualityInfoUseCandidateCount && point.candidateCount > std::max(1, config.qualityInfoMaxCandidateCount)) {
                reason |= ReasonCandidateAmbiguous;
                quality = std::min(quality, 0.6);
            }

            if (config.qualityInfoUseModulation && std::isfinite(point.modulation)) {
                if (point.modulation < static_cast<float>(config.qualityInfoMinModulation)) {
                    reason |= ReasonLowModulation;
                    const double modulationQuality = config.qualityInfoMinModulation > 0.0
                        ? clamp01(static_cast<double>(point.modulation) / config.qualityInfoMinModulation)
                        : 0.0;
                    quality = std::min(quality, std::max(0.05, modulationQuality));
                }
            }

            if (quality > 0.0 &&
                hasInvalidDepthNeighbor(pointCloud.gridPoints,
                                        pointCloud.width,
                                        pointCloud.height,
                                        x,
                                        y,
                                        config.qualityInfoHoleEdgeRadius,
                                        config)) {
                reason |= ReasonHoleEdge;
                quality = std::min(quality, 0.7);
            }

            pixel.score = clamp01(quality);
            pixel.reason = reason;
            result.map.pixels[index] = pixel;
        }
    }

    result.frameStats = summarizePointReliabilityMap(result.map, config.qualityInfoHighConfidenceThreshold);
    if (!result.map.pixels.empty()) {
        PointReliabilityPixel& sentinel = result.map.pixels[0];
        sentinel.semanticClass = 0.0;
        sentinel.score = clamp01(result.frameStats.frameQualityScore);
        sentinel.reason = result.frameStats.lowQualityReasonBits;
    }

    result.summary = formatFrameQualitySummary(result.frameStats);
    result.stats.status = {};
    result.stats.inputImageCount = 1;
    result.stats.validImageCount = static_cast<std::size_t>(result.frameStats.validPoints);
    result.stats.rejectedImageCount = result.frameStats.totalPixels > result.frameStats.validPoints
        ? static_cast<std::size_t>(result.frameStats.totalPixels - result.frameStats.validPoints)
        : 0U;
    result.stats.checkedPixels = static_cast<std::size_t>(result.frameStats.totalPixels);
    result.stats.cudaComputedPixels = 0;
    result.stats.blackPixels = static_cast<std::size_t>(result.frameStats.highConfidencePoints);
    result.stats.saturatedPixels = static_cast<std::size_t>(result.frameStats.largestConnectedComponentPoints);
    result.stats.blackPixelRatio = result.frameStats.validCoverageRatio;
    result.stats.saturatedPixelRatio = result.frameStats.highConfidenceRatio;
    result.stats.minPixelValue = result.frameStats.frameQualityScore;
    result.stats.maxPixelValue = result.frameStats.p95Score;
    result.stats.meanPixelValue = result.frameStats.meanScore;
    result.status = {};
    return result;
}

std::string formatFrameQualitySummary(const SingleFrameQualityStats& stats)
{
    std::ostringstream out;
    out << "frameQualityScore=" << stats.frameQualityScore
        << ",pointQualityScore=" << stats.pointQualityScore
        << ",coverage=" << stats.validCoverageRatio
        << ",centerCoverage=" << stats.centerCoverageRatio
        << ",connectedSurfaceScore=" << stats.connectedSurfaceScore
        << ",validPoints=" << stats.validPoints
        << ",centerValidPoints=" << stats.centerValidPoints
        << ",highConfidenceRatio=" << stats.highConfidenceRatio
        << ",meanScore=" << stats.meanScore
        << ",p50=" << stats.p50Score
        << ",p90=" << stats.p90Score
        << ",p95=" << stats.p95Score
        << ",largestConnectedComponent=" << stats.largestConnectedComponentPoints
        << ",boundaryRatio=" << stats.boundaryRatio
        << ",lowQualityReasonBits=" << stats.lowQualityReasonBits;
    return out.str();
}

} // namespace reconstruct_one_frame
