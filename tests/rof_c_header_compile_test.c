#include "reconstruct_one_frame/rof_c_api.h"

int main(void)
{
    RofApi api = {0};
    api.struct_size = (uint32_t)sizeof(api);
    RofCalcInput input = {0};
    input.struct_size = (uint32_t)sizeof(input);
    input.flags = ROF_FRAME_FLAG_METAL_SCAN;
    return api.struct_size == 0U || input.flags != ROF_FRAME_FLAG_METAL_SCAN;
}
