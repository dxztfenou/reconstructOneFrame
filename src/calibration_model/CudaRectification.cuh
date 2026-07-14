#pragma once

#include "calibration_model/CalibrationModel.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace reconstruct_one_frame::cuda_rectification {

// Kernel-friendly copy of OpenCV's initUndistortRectifyMap inputs. Keeping this
// POD shared makes phase, color, and masks use one rectified-to-sensor transform.
struct RemapCalibration {
    float k[9] = {};
    float dist[8] = {};
    float rInv[9] = {};
    float p[12] = {};
    int distCount = 0;
};

inline bool hasRectificationCalibration(const CalibrationModel& calibration)
{
    return calibration.leftIntrinsics.size() == 9 &&
           calibration.rightIntrinsics.size() == 9 &&
           !calibration.leftDistortion.empty() &&
           !calibration.rightDistortion.empty() &&
           calibration.rectificationLeft.size() == 9 &&
           calibration.rectificationRight.size() == 9 &&
           calibration.projectionLeft.size() == 12 &&
           calibration.projectionRight.size() == 12;
}

inline std::array<double, 9> invert3x3(const std::vector<double>& matrix)
{
    const double a = matrix[0];
    const double b = matrix[1];
    const double c = matrix[2];
    const double d = matrix[3];
    const double e = matrix[4];
    const double f = matrix[5];
    const double g = matrix[6];
    const double h = matrix[7];
    const double i = matrix[8];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) <= 1.0e-12) {
        return {1.0, 0.0, 0.0,
                0.0, 1.0, 0.0,
                0.0, 0.0, 1.0};
    }
    const double inv = 1.0 / det;
    return {
        (e * i - f * h) * inv,
        (c * h - b * i) * inv,
        (b * f - c * e) * inv,
        (f * g - d * i) * inv,
        (a * i - c * g) * inv,
        (c * d - a * f) * inv,
        (d * h - e * g) * inv,
        (b * g - a * h) * inv,
        (a * e - b * d) * inv
    };
}

inline RemapCalibration makeRemapCalibration(const std::vector<double>& intrinsics,
                                             const std::vector<double>& distortion,
                                             const std::vector<double>& rectification,
                                             const std::vector<double>& projection)
{
    RemapCalibration remap;
    for (std::size_t i = 0; i < intrinsics.size() && i < 9; ++i) {
        remap.k[i] = static_cast<float>(intrinsics[i]);
    }
    for (std::size_t i = 0; i < distortion.size() && i < 8; ++i) {
        remap.dist[i] = static_cast<float>(distortion[i]);
    }
    remap.distCount = static_cast<int>(std::min<std::size_t>(distortion.size(), 8));
    const std::array<double, 9> inverse = invert3x3(rectification);
    for (std::size_t i = 0; i < inverse.size(); ++i) {
        remap.rInv[i] = static_cast<float>(inverse[i]);
    }
    for (std::size_t i = 0; i < projection.size() && i < 12; ++i) {
        remap.p[i] = static_cast<float>(projection[i]);
    }
    return remap;
}

// Reverse-map one rectified output pixel into the distorted sensor image. CUDA
// callers then choose interpolation appropriate for phase steps, color, or masks.
__device__ inline bool rectifiedToRawPixel(int u,
                                           int v,
                                           RemapCalibration calibration,
                                           float& srcX,
                                           float& srcY)
{
    const float fxNew = calibration.p[0];
    const float fyNew = calibration.p[5];
    const float cxNew = calibration.p[2];
    const float cyNew = calibration.p[6];
    if (fabsf(fxNew) <= 1.0e-6F || fabsf(fyNew) <= 1.0e-6F) {
        return false;
    }

    const float xr = (static_cast<float>(u) - cxNew) / fxNew;
    const float yr = (static_cast<float>(v) - cyNew) / fyNew;
    const float rz = calibration.rInv[6] * xr + calibration.rInv[7] * yr + calibration.rInv[8];
    if (fabsf(rz) <= 1.0e-6F) {
        return false;
    }
    const float x = (calibration.rInv[0] * xr + calibration.rInv[1] * yr + calibration.rInv[2]) / rz;
    const float y = (calibration.rInv[3] * xr + calibration.rInv[4] * yr + calibration.rInv[5]) / rz;

    const float k1 = calibration.distCount > 0 ? calibration.dist[0] : 0.0F;
    const float k2 = calibration.distCount > 1 ? calibration.dist[1] : 0.0F;
    const float p1 = calibration.distCount > 2 ? calibration.dist[2] : 0.0F;
    const float p2 = calibration.distCount > 3 ? calibration.dist[3] : 0.0F;
    const float k3 = calibration.distCount > 4 ? calibration.dist[4] : 0.0F;
    const float r2 = x * x + y * y;
    const float r4 = r2 * r2;
    const float r6 = r4 * r2;
    const float radial = 1.0F + k1 * r2 + k2 * r4 + k3 * r6;
    const float xDist = x * radial + 2.0F * p1 * x * y + p2 * (r2 + 2.0F * x * x);
    const float yDist = y * radial + p1 * (r2 + 2.0F * y * y) + 2.0F * p2 * x * y;
    srcX = calibration.k[0] * xDist + calibration.k[2];
    srcY = calibration.k[4] * yDist + calibration.k[5];
    return isfinite(srcX) && isfinite(srcY);
}

} // namespace reconstruct_one_frame::cuda_rectification
