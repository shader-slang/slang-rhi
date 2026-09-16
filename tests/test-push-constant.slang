struct PushConstants
{
    uint value;
    RWStructuredBuffer<uint> result;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    pushConstants.result[0] = pushConstants.value;
}
