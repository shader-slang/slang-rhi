struct Arguments
{
    uint before;
    StructuredBuffer<float>.Handle inputs[2];
    uint between;
    RWStructuredBuffer<float>.Handle output;
    StructuredBuffer<float>.Handle empty;
}

uniform Arguments arguments;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uniform uint extra)
{
    uint count;
    uint stride;
    arguments.output[0] = arguments.inputs[0][1] + arguments.inputs[1][0] +
                          arguments.before + arguments.between + extra;
    arguments.inputs[0].GetDimensions(count, stride);
    arguments.output[1] = count;
    arguments.inputs[1].GetDimensions(count, stride);
    arguments.output[2] = count;
    arguments.output.GetDimensions(count, stride);
    arguments.output[3] = count;
    arguments.empty.GetDimensions(count, stride);
    arguments.output[4] = count;
    arguments.output[5] = stride;
}
