#include "diagnostics/Status.h"

namespace reconstruct_one_frame {

Status makeStatus(StatusCode code, const char* module, const std::string& message)
{
    return {code, module == nullptr ? "" : module, message};
}

const char* statusCodeName(StatusCode code) noexcept
{
    switch (code) {
    case StatusCode::Ok:
        return "Ok";
    case StatusCode::InputMissing:
        return "InputMissing";
    case StatusCode::InputEmptyImage:
        return "InputEmptyImage";
    case StatusCode::InputBlackImage:
        return "InputBlackImage";
    case StatusCode::InputSizeMismatch:
        return "InputSizeMismatch";
    case StatusCode::InputTypeUnsupported:
        return "InputTypeUnsupported";
    case StatusCode::InputStrideInvalid:
        return "InputStrideInvalid";
    case StatusCode::InputInvalidValue:
        return "InputInvalidValue";
    case StatusCode::ConfigMissing:
        return "ConfigMissing";
    case StatusCode::ConfigParseFailed:
        return "ConfigParseFailed";
    case StatusCode::ConfigInvalidValue:
        return "ConfigInvalidValue";
    case StatusCode::CalibrationMissing:
        return "CalibrationMissing";
    case StatusCode::CalibrationParseFailed:
        return "CalibrationParseFailed";
    case StatusCode::CalibrationInvalid:
        return "CalibrationInvalid";
    case StatusCode::CudaInitFailed:
        return "CudaInitFailed";
    case StatusCode::CudaKernelFailed:
        return "CudaKernelFailed";
    case StatusCode::DataQualityInsufficient:
        return "DataQualityInsufficient";
    case StatusCode::PhaseFailed:
        return "PhaseFailed";
    case StatusCode::UnwrapFailed:
        return "UnwrapFailed";
    case StatusCode::MatchingFailed:
        return "MatchingFailed";
    case StatusCode::ReconstructionInsufficient:
        return "ReconstructionInsufficient";
    case StatusCode::OutputWriteFailed:
        return "OutputWriteFailed";
    case StatusCode::NotComputed:
        return "NotComputed";
    case StatusCode::InternalError:
        return "InternalError";
    }
    return "Unknown";
}

} // namespace reconstruct_one_frame
