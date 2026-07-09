#pragma once

#include "reconstruction/PointCloudReconstructor.h"

#include <string>
#include <vector>

namespace reconstruct_one_frame {

struct PlyPoint {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct PlyComparisonResult {
    Status status;
    std::size_t generatedVertexCount = 0;
    std::size_t legacyVertexCount = 0;
    std::size_t matchedLegacyCount = 0;
    double generatedCentroid[3] = {0.0, 0.0, 0.0};
    double legacyCentroid[3] = {0.0, 0.0, 0.0};
    double generatedBboxMin[3] = {0.0, 0.0, 0.0};
    double generatedBboxMax[3] = {0.0, 0.0, 0.0};
    double legacyBboxMin[3] = {0.0, 0.0, 0.0};
    double legacyBboxMax[3] = {0.0, 0.0, 0.0};
    double rowOrderMeanAbsZMm = 0.0;
    double rowOrderMeanDistanceMm = 0.0;
    double nearestMeanDistanceMm = 0.0;
    double nearestRmsDistanceMm = 0.0;
    double nearestP95DistanceMm = 0.0;
};

Status writeAsciiPly(const std::string& path, const std::vector<PointCloudVertex>& vertices);
Status readAsciiPlyPoints(const std::string& path, std::vector<PlyPoint>& points);
PlyComparisonResult comparePointCloudToLegacy(const std::vector<PointCloudVertex>& generated,
                                              const std::string& legacyPlyPath);
std::string formatPlyComparison(const PlyComparisonResult& comparison);

} // namespace reconstruct_one_frame
