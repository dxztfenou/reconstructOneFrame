#include "reconstruct_one_frame/rof_c_api.h"

int main(void)
{
    RofApiV1 api = {0};
    api.struct_size = (uint32_t)sizeof(api);
    RofFrameInputV1 input = {0};
    input.struct_size = (uint32_t)sizeof(input);
    input.flags = ROF_FRAME_FLAG_METAL_SCAN_V1;
    return api.struct_size == 0U || input.flags != ROF_FRAME_FLAG_METAL_SCAN_V1;
}
