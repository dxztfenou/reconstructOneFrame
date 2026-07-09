#pragma once

#include "reconstruct_one_frame/reconstructInterface.h"

namespace reconstruct_one_frame {

Status makeStatus(StatusCode code, const char* module, const std::string& message);

} // namespace reconstruct_one_frame
