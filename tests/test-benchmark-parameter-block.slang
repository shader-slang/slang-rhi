struct BenchmarkScene
{
    float4 constants[1024];
    StructuredBuffer<float> input0;
    StructuredBuffer<float> input1;
    StructuredBuffer<float> input2;
    StructuredBuffer<float> input3;
}

ParameterBlock<BenchmarkScene> scene;
RWStructuredBuffer<float> output;
uint iteration;

float evaluateScene()
{
    return scene.constants[iteration & 1023].x + scene.input0[0] + scene.input1[0] +
           scene.input2[0] + scene.input3[0];
}

[shader("compute")]
[numthreads(1, 1, 1)]
void computeA() { output[0] = evaluateScene(); }

[shader("compute")]
[numthreads(1, 1, 1)]
void computeB() { output[0] = evaluateScene(); }
