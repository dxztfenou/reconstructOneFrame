#include "diagnostics/DiagnosticSummary.h"

#include <iomanip>
#include <sstream>

namespace reconstruct_one_frame {

std::string formatPreprocessPlanSummary(const PreprocessPlan& plan)
{
    std::ostringstream out;
    out << "preprocessPlan="
        << plan.width << "x" << plan.height
        << ", channels=" << plan.channels
        << ", stripeCount=" << plan.stripeCount
        << ", normalizedFloatRequired=" << (plan.normalizedFloatRequired ? "true" : "false")
        << ", rectificationRequired=" << (plan.rectificationRequired ? "true" : "false")
        << ", gpuUploadRequired=" << (plan.gpuUploadRequired ? "true" : "false");
    return out.str();
}

std::string formatFrameResultSummary(std::uint64_t frameId, const FrameResult& result)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "status=" << statusCodeName(result.status.code) << "\n";
    out << "frameId=" << frameId << "\n";
    for (const StageStats& stat : result.stats) {
        out << "stage=" << stat.stageName
            << ",status=" << statusCodeName(stat.status.code)
            << ",inputImages=" << stat.inputImageCount
            << ",validImages=" << stat.validImageCount
            << ",rejectedImages=" << stat.rejectedImageCount
            << ",cudaPixels=" << stat.cudaComputedPixels
            << ",elapsedMs=" << stat.elapsedMs
            << ",blackRatio=" << stat.blackPixelRatio
            << ",saturatedRatio=" << stat.saturatedPixelRatio
            << ",notComputed=" << (stat.notComputed ? "true" : "false")
            << ",skipped=" << (stat.skipped ? "true" : "false")
            << "\n";
    }
    out << "depthComputed=" << (result.depthComputed ? "true" : "false") << "\n"
        << "normalComputed=" << (result.normalComputed ? "true" : "false") << "\n"
        << "qualityComputed=" << (result.qualityComputed ? "true" : "false") << "\n"
        << "wrappedPhaseComputed=" << (result.wrappedPhaseComputed ? "true" : "false") << "\n"
        << "unwrappedPhaseComputed=" << (result.unwrappedPhaseComputed ? "true" : "false") << "\n"
        << "pointCloudVertexCount=" << result.pointCloudVertexCount << "\n";
    if (!result.outputPointCloudPath.empty()) {
        out << "outputPointCloudPath=" << result.outputPointCloudPath << "\n";
    }
    if (!result.legacyComparisonSummary.empty()) {
        out << "legacyComparison=" << result.legacyComparisonSummary << "\n";
    }
    if (!result.qualitySummary.empty()) {
        out << "qualitySummary=" << result.qualitySummary << "\n";
    }
    return out.str();
}

} // namespace reconstruct_one_frame
