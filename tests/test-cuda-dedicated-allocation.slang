uniform RWStructuredBuffer<uint>.Handle storage;
uniform RWStructuredBuffer<uint> result;
uniform uint index;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    storage[0] = 17;
    storage[index] = 42;
    result[0] = storage[0];
    result[1] = storage[index];
    uint count, stride;
    storage.GetDimensions(count, stride);
    result[2] = count;
    result[3] = stride;
}
