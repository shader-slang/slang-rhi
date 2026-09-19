#include "test-benchmark-bindings-common.slang"
RWStructuredBuffer<uint> output;
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    output[0] += bindingValue();
}
