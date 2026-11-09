uniform RWStructuredBuffer<float>.Handle output;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    output[0] = 42.0;
}
