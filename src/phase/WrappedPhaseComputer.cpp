#include "phase/WrappedPhaseComputer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace reconstruct_one_frame {

namespace {

constexpr double kPi = 3.14159265358979323846;

double readPixelAsDouble(const ImageView& image, std::size_t pixelIndex)
{
    const int elementSize = image.elementType == ImageElementType::UInt8 ? 1 :
        image.elementType == ImageElementType::UInt16 ? 2 :
        image.elementType == ImageElementType::Float32 ? 4 : 0;
    const int row = static_cast<int>(pixelIndex / static_cast<std::size_t>(image.width * image.channels));
    const int col = static_cast<int>(pixelIndex % static_cast<std::size_t>(image.width * image.channels));
    const auto* base = static_cast<const unsigned char*>(image.data);
    const unsigned char* ptr = base + static_cast<std::size_t>(row) * image.strideBytes + static_cast<std::size_t>(col) * elementSize;
    if (image.elementType == ImageElementType::UInt8) {
        return static_cast<double>(*ptr);
    }
    if (image.elementType == ImageElementType::UInt16) {
        return static_cast<double>(*reinterpret_cast<const std::uint16_t*>(ptr));
    }
    if (image.elementType == ImageElementType::Float32) {
        return static_cast<double>(*reinterpret_cast<const float*>(ptr));
    }
    return 0.0;
}

std::vector<const StripeImage*> collectFrequencySteps(const std::vector<StripeImage>& images,
                                                      int frequencyIndex,
                                                      int requiredSteps)
{
    std::vector<const StripeImage*> steps(static_cast<std::size_t>(requiredSteps), nullptr);
    for (const StripeImage& image : images) {
        if (image.frequencyIndex == frequencyIndex &&
            image.phaseStepIndex >= 0 &&
            image.phaseStepIndex < requiredSteps) {
            steps[static_cast<std::size_t>(image.phaseStepIndex)] = &image;
        }
    }
    return steps;
}

Status computeOne(CameraSide camera,
                  const std::vector<StripeImage>& images,
                  const StripeRequirement& requirement,
                  WrappedPhaseFrequencyResult& output)
{
    const std::vector<const StripeImage*> steps =
        collectFrequencySteps(images, requirement.frequencyIndex, requirement.requiredPhaseSteps);
    for (const StripeImage* step : steps) {
        if (step == nullptr) {
            return {StatusCode::InputPhaseStepMissing, "WrappedPhaseComputer", "phase step missing before wrapped phase compute"};
        }
    }

    const ImageView& first = steps.front()->image;
    const std::size_t pixelCount = static_cast<std::size_t>(first.width) * static_cast<std::size_t>(first.height) * static_cast<std::size_t>(first.channels);
    output.camera = camera;
    output.frequencyIndex = requirement.frequencyIndex;
    output.frequencyValue = requirement.frequencyValue;
    output.width = first.width;
    output.height = first.height;
    output.phase.assign(pixelCount, 0.0F);
    output.modulation.assign(pixelCount, 0.0F);
    output.validPixelCount = pixelCount;

    double modulationSum = 0.0;
    double minModulation = std::numeric_limits<double>::infinity();
    double maxModulation = 0.0;

    const double direction = requirement.phaseStepDirection == 0 ? 1.0 : static_cast<double>(requirement.phaseStepDirection);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        double sinSum = 0.0;
        double cosSum = 0.0;
        for (int step = 0; step < requirement.requiredPhaseSteps; ++step) {
            const double intensity = readPixelAsDouble(steps[static_cast<std::size_t>(step)]->image, pixel);
            const double angle = direction * 2.0 * kPi * static_cast<double>(step) / static_cast<double>(requirement.requiredPhaseSteps);
            sinSum += intensity * std::sin(angle);
            cosSum += intensity * std::cos(angle);
        }

        const double phase = std::atan2(-sinSum, cosSum);
        const double modulation = 2.0 * std::sqrt(sinSum * sinSum + cosSum * cosSum) /
            static_cast<double>(requirement.requiredPhaseSteps);
        output.phase[pixel] = static_cast<float>(phase);
        output.modulation[pixel] = static_cast<float>(modulation);
        modulationSum += modulation;
        minModulation = std::min(minModulation, modulation);
        maxModulation = std::max(maxModulation, modulation);
    }

    output.meanModulation = pixelCount == 0 ? 0.0 : modulationSum / static_cast<double>(pixelCount);
    output.minModulation = pixelCount == 0 ? 0.0 : minModulation;
    output.maxModulation = maxModulation;
    return {};
}

} // namespace

WrappedPhaseResult computeWrappedPhaseCpuReference(const StripeFrameGroup& frame,
                                                   const ReconsConfig& config,
                                                   const WrappedPhaseOptions& options)
{
    WrappedPhaseResult result;
    result.stats.stageName = "wrapped_phase_compute";
    result.stats.inputImageCount = frame.leftStripes.size() + frame.rightStripes.size();

    for (const StripeRequirement& requirement : config.stripeRequirements) {
        WrappedPhaseFrequencyResult left;
        Status status = computeOne(CameraSide::Left, frame.leftStripes, requirement, left);
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        WrappedPhaseFrequencyResult right;
        status = computeOne(CameraSide::Right, frame.rightStripes, requirement, right);
        if (!status.ok()) {
            result.status = status;
            result.stats.status = status;
            return result;
        }
        result.stats.validImageCount += static_cast<std::size_t>(requirement.requiredPhaseSteps) * 2;
        result.stats.checkedPixels += left.phase.size() + right.phase.size();
        result.stats.meanPixelValue += left.meanModulation + right.meanModulation;
        result.frequencies.push_back(std::move(left));
        result.frequencies.push_back(std::move(right));
    }

    if (!result.frequencies.empty()) {
        result.stats.meanPixelValue /= static_cast<double>(result.frequencies.size());
    }
    if (result.stats.meanPixelValue < options.minMeanModulation) {
        result.status = {StatusCode::PhaseQualityInsufficient, "WrappedPhaseComputer", "wrapped phase mean modulation is below threshold"};
        result.stats.status = result.status;
        return result;
    }

    result.status = {};
    result.stats.status = {};
    return result;
}

} // namespace reconstruct_one_frame
