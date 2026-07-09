#include "io/PlyIO.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>

namespace reconstruct_one_frame {

namespace {

struct CellKey {
    int x = 0;
    int y = 0;
    int z = 0;

    bool operator==(const CellKey& other) const noexcept
    {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct CellKeyHash {
    std::size_t operator()(const CellKey& key) const noexcept
    {
        std::size_t seed = 1469598103934665603ULL;
        auto mix = [&seed](int value) {
            seed ^= static_cast<std::size_t>(value + 0x9e3779b9);
            seed *= 1099511628211ULL;
        };
        mix(key.x);
        mix(key.y);
        mix(key.z);
        return seed;
    }
};

PlyPoint toPlyPoint(const PointCloudVertex& vertex)
{
    return {vertex.x, vertex.y, vertex.z};
}

double squaredDistance(const PlyPoint& a, const PlyPoint& b)
{
    const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
    const double dy = static_cast<double>(a.y) - static_cast<double>(b.y);
    const double dz = static_cast<double>(a.z) - static_cast<double>(b.z);
    return dx * dx + dy * dy + dz * dz;
}

CellKey cellFor(const PlyPoint& point, double cellSize)
{
    return {static_cast<int>(std::floor(static_cast<double>(point.x) / cellSize)),
            static_cast<int>(std::floor(static_cast<double>(point.y) / cellSize)),
            static_cast<int>(std::floor(static_cast<double>(point.z) / cellSize))};
}

void summarizeCloud(const std::vector<PlyPoint>& points, double* centroid, double* bboxMin, double* bboxMax)
{
    if (points.empty()) {
        for (int i = 0; i < 3; ++i) {
            centroid[i] = 0.0;
            bboxMin[i] = 0.0;
            bboxMax[i] = 0.0;
        }
        return;
    }
    for (int i = 0; i < 3; ++i) {
        bboxMin[i] = std::numeric_limits<double>::infinity();
        bboxMax[i] = -std::numeric_limits<double>::infinity();
        centroid[i] = 0.0;
    }
    for (const PlyPoint& point : points) {
        const double values[3] = {point.x, point.y, point.z};
        for (int i = 0; i < 3; ++i) {
            centroid[i] += values[i];
            bboxMin[i] = std::min(bboxMin[i], values[i]);
            bboxMax[i] = std::max(bboxMax[i], values[i]);
        }
    }
    for (int i = 0; i < 3; ++i) {
        centroid[i] /= static_cast<double>(points.size());
    }
}

} // namespace

Status writeAsciiPly(const std::string& path, const std::vector<PointCloudVertex>& vertices)
{
    try {
        const std::filesystem::path outputPath(path);
        if (outputPath.has_parent_path()) {
            std::filesystem::create_directories(outputPath.parent_path());
        }
        std::ofstream out(outputPath);
        if (!out) {
            return {StatusCode::OutputWriteFailed, "PlyIO", "failed to open PLY for write: " + path};
        }
        out << "ply\n";
        out << "format ascii 1.0\n";
        out << "element vertex " << vertices.size() << "\n";
        out << "property float x\n";
        out << "property float y\n";
        out << "property float z\n";
        out << "property float nx\n";
        out << "property float ny\n";
        out << "property float nz\n";
        out << "property uchar red\n";
        out << "property uchar green\n";
        out << "property uchar blue\n";
        out << "end_header\n";
        out << std::setprecision(7);
        for (const PointCloudVertex& vertex : vertices) {
            out << vertex.x << ' ' << vertex.y << ' ' << vertex.z << ' '
                << vertex.nx << ' ' << vertex.ny << ' ' << vertex.nz << ' '
                << static_cast<int>(vertex.r) << ' '
                << static_cast<int>(vertex.g) << ' '
                << static_cast<int>(vertex.b) << '\n';
        }
        return {};
    } catch (const std::exception& ex) {
        return {StatusCode::OutputWriteFailed, "PlyIO", ex.what()};
    }
}

Status readAsciiPlyPoints(const std::string& path, std::vector<PlyPoint>& points)
{
    std::ifstream input(path);
    if (!input) {
        return {StatusCode::InputManifestMissing, "PlyIO", "PLY file not found: " + path};
    }
    std::string line;
    std::size_t vertexCount = 0;
    bool headerEnded = false;
    while (std::getline(input, line)) {
        if (line.rfind("element vertex ", 0) == 0) {
            vertexCount = static_cast<std::size_t>(std::stoull(line.substr(15)));
        }
        if (line == "end_header") {
            headerEnded = true;
            break;
        }
    }
    if (!headerEnded) {
        return {StatusCode::ConfigParseFailed, "PlyIO", "invalid PLY header: " + path};
    }
    points.clear();
    points.reserve(vertexCount);
    for (std::size_t i = 0; i < vertexCount && std::getline(input, line); ++i) {
        std::istringstream row(line);
        PlyPoint point;
        if (!(row >> point.x >> point.y >> point.z)) {
            return {StatusCode::ConfigParseFailed, "PlyIO", "invalid PLY vertex row: " + path};
        }
        points.push_back(point);
    }
    return {};
}

PlyComparisonResult comparePointCloudToLegacy(const std::vector<PointCloudVertex>& generated,
                                              const std::string& legacyPlyPath)
{
    PlyComparisonResult result;
    std::vector<PlyPoint> generatedPoints;
    generatedPoints.reserve(generated.size());
    for (const PointCloudVertex& vertex : generated) {
        generatedPoints.push_back(toPlyPoint(vertex));
    }
    std::vector<PlyPoint> legacyPoints;
    result.status = readAsciiPlyPoints(legacyPlyPath, legacyPoints);
    if (!result.status.ok()) {
        return result;
    }

    result.generatedVertexCount = generatedPoints.size();
    result.legacyVertexCount = legacyPoints.size();
    summarizeCloud(generatedPoints, result.generatedCentroid, result.generatedBboxMin, result.generatedBboxMax);
    summarizeCloud(legacyPoints, result.legacyCentroid, result.legacyBboxMin, result.legacyBboxMax);

    const std::size_t rowCount = std::min(generatedPoints.size(), legacyPoints.size());
    if (rowCount > 0) {
        double sumAbsZ = 0.0;
        double sumDist = 0.0;
        for (std::size_t i = 0; i < rowCount; ++i) {
            sumAbsZ += std::fabs(static_cast<double>(generatedPoints[i].z) - static_cast<double>(legacyPoints[i].z));
            sumDist += std::sqrt(squaredDistance(generatedPoints[i], legacyPoints[i]));
        }
        result.rowOrderMeanAbsZMm = sumAbsZ / static_cast<double>(rowCount);
        result.rowOrderMeanDistanceMm = sumDist / static_cast<double>(rowCount);
    }

    constexpr double cellSize = 1.0;
    std::unordered_map<CellKey, std::vector<std::size_t>, CellKeyHash> grid;
    grid.reserve(generatedPoints.size());
    for (std::size_t i = 0; i < generatedPoints.size(); ++i) {
        grid[cellFor(generatedPoints[i], cellSize)].push_back(i);
    }

    std::vector<double> nearestDistances;
    nearestDistances.reserve(legacyPoints.size());
    constexpr int searchRadius = 3;
    for (const PlyPoint& legacy : legacyPoints) {
        const CellKey center = cellFor(legacy, cellSize);
        double best = std::numeric_limits<double>::infinity();
        for (int dz = -searchRadius; dz <= searchRadius; ++dz) {
            for (int dy = -searchRadius; dy <= searchRadius; ++dy) {
                for (int dx = -searchRadius; dx <= searchRadius; ++dx) {
                    const CellKey key{center.x + dx, center.y + dy, center.z + dz};
                    auto found = grid.find(key);
                    if (found == grid.end()) {
                        continue;
                    }
                    for (std::size_t index : found->second) {
                        best = std::min(best, squaredDistance(legacy, generatedPoints[index]));
                    }
                }
            }
        }
        if (std::isfinite(best)) {
            nearestDistances.push_back(std::sqrt(best));
        }
    }
    result.matchedLegacyCount = nearestDistances.size();
    if (!nearestDistances.empty()) {
        double sum = 0.0;
        double sumSq = 0.0;
        for (double distance : nearestDistances) {
            sum += distance;
            sumSq += distance * distance;
        }
        std::sort(nearestDistances.begin(), nearestDistances.end());
        result.nearestMeanDistanceMm = sum / static_cast<double>(nearestDistances.size());
        result.nearestRmsDistanceMm = std::sqrt(sumSq / static_cast<double>(nearestDistances.size()));
        const std::size_t p95Index = std::min(nearestDistances.size() - 1,
                                              static_cast<std::size_t>(std::floor(0.95 * static_cast<double>(nearestDistances.size()))));
        result.nearestP95DistanceMm = nearestDistances[p95Index];
    }
    result.status = {};
    return result;
}

std::string formatPlyComparison(const PlyComparisonResult& comparison)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(4);
    out << "generatedVertices=" << comparison.generatedVertexCount
        << ",legacyVertices=" << comparison.legacyVertexCount
        << ",vertexDelta=" << static_cast<long long>(comparison.generatedVertexCount) - static_cast<long long>(comparison.legacyVertexCount)
        << ",matchedLegacy=" << comparison.matchedLegacyCount
        << ",rowMeanAbsZMm=" << comparison.rowOrderMeanAbsZMm
        << ",rowMeanDistanceMm=" << comparison.rowOrderMeanDistanceMm
        << ",nearestMeanMm=" << comparison.nearestMeanDistanceMm
        << ",nearestRmsMm=" << comparison.nearestRmsDistanceMm
        << ",nearestP95Mm=" << comparison.nearestP95DistanceMm
        << ",generatedCentroid=(" << comparison.generatedCentroid[0] << "," << comparison.generatedCentroid[1] << "," << comparison.generatedCentroid[2] << ")"
        << ",legacyCentroid=(" << comparison.legacyCentroid[0] << "," << comparison.legacyCentroid[1] << "," << comparison.legacyCentroid[2] << ")";
    return out.str();
}

} // namespace reconstruct_one_frame
