#pragma once

#include "image/ImagePreprocessor.h"
#include "reconstruct_one_frame/reconstructInterface.h"

#include <string>

namespace reconstruct_one_frame {

std::string formatFrameResultSummary(std::uint64_t frameId, const FrameResult& result);
std::string formatPreprocessPlanSummary(const PreprocessPlan& plan);

} // namespace reconstruct_one_frame
