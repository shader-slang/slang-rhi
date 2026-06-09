#include "stress/stress-shaders.h"

#include <sstream>

namespace rhi::testing::stress {

std::string makeLifetimeCanaryComputeShader()
{
    return R"(
        [shader("compute")]
        [numthreads(64, 1, 1)]
        void computeMain(
            uint3 tid : SV_DispatchThreadID,
            uniform RWStructuredBuffer<uint> accum,
            uniform RWStructuredBuffer<uint> temp,
            uniform uint value,
            uniform uint count)
        {
            if (tid.x >= count)
                return;
            uint v = value + tid.x;
            temp[tid.x] = v;
            accum[tid.x] = accum[tid.x] + v;
        }
    )";
}

std::string makeVariantComputeShader(uint32_t variant)
{
    std::ostringstream stream;
    stream << R"(
        [shader("compute")]
        [numthreads(4, 1, 1)]
        void computeMain(
            uint3 tid : SV_DispatchThreadID,
            uniform RWStructuredBuffer<uint> buffer,
            uniform uint baseValue)
        {
            uint x = baseValue + tid.x + )"
           << variant << R"(u;
            x = (x ^ )"
           << (0x9e3779b9u + variant * 17u) << R"(u) + (x << 6) + (x >> 2);
            buffer[tid.x] = x;
        }
    )";
    return stream.str();
}

std::string makeSimpleRenderShader(uint32_t variant)
{
    std::ostringstream stream;
    stream << R"(
        [shader("vertex")]
        float4 vertexMain(uint vid : SV_VertexID) : SV_Position
        {
            float2 uv = float2((vid << 1) & 2, vid & 2);
            return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
        }

        [shader("fragment")]
        float4 fragmentMain() : SV_Target
        {
            return float4()"
           << ((variant & 1) ? "0.25, 0.5, 1.0, 1.0" : "1.0, 0.5, 0.25, 1.0") << R"();
        }
    )";
    return stream.str();
}

} // namespace rhi::testing::stress
