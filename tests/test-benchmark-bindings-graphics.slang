#include "test-benchmark-bindings-common.slang"
[shader("vertex")]
float4 vertexMain(uint id : SV_VertexID) : SV_Position
{
    return float4(id == 2 ? 3.0 : -1.0, id == 1 ? 3.0 : -1.0, 0.0, 1.0);
}
[shader("fragment")]
float4 fragmentMain() : SV_Target
{
    return float4(bindingValue(), 1, 1, 1);
}
