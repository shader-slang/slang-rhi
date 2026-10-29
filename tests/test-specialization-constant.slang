// A specialization constant of each scalar type, at its default. On Metal each one is a function
// constant, which the device has to specialize before a pipeline can be built from the function.

[SpecializationConstant]
const bool constBool = true;
[SpecializationConstant]
const int constInt = -7;
[SpecializationConstant]
const uint constUint = 9;
[vk::constant_id(5)]
const float constFloat = 2.5;

[shader("compute")]
[numthreads(4, 1, 1)]
void computeMain(uint3 tid: SV_DispatchThreadID, uniform RWStructuredBuffer<float> buffer)
{
    if (tid.x == 0)
        buffer[0] = constBool ? 1.0 : 0.0;
    else if (tid.x == 1)
        buffer[1] = float(constInt);
    else if (tid.x == 2)
        buffer[2] = float(constUint);
    else if (tid.x == 3)
        buffer[3] = constFloat;
}

// A triangle covering the whole target, from the vertex index alone.
[shader("vertex")]
float4 vertexMain(uint vertexID: SV_VertexID) : SV_Position
{
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    return float4(uv * 2.0 - 1.0, 0.0, 1.0);
}

[shader("fragment")]
float4 fragmentMain() : SV_Target
{
    return float4(constBool ? 1.0 : 0.0, float(constInt), float(constUint), constFloat);
}
