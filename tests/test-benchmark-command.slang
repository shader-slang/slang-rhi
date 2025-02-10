float add(float a, float b)
{
    return a + b;
}

struct AddKernelData
{
    StructuredBuffer<float> a;
    StructuredBuffer<float> b;
    RWStructuredBuffer<float> res;
    int count;
}
ParameterBlock<AddKernelData> addKernelData;
// uniform AddKernelData addKernelData;

[shader("compute")]
[numthreads(32, 1, 1)]
void addkernel(uint3 thread: SV_DispatchThreadID)
{
    if (thread.x < addKernelData.count)
    {
        addKernelData.res[thread.x] = add(addKernelData.a[thread.x], addKernelData.b[thread.x]);
    }
}
