extern static const uint numthread = 0;
extern static const bool constBool = false;
extern static const int constInt = 0;
extern static const uint constUint = 0;
extern static const float constFloat = 0.0;

// Main entry-point. Write some value into buffer depending on link
// time constant.
[shader("compute")]
[numthreads(numthread, 1, 1)]
void computeMain(
    uint3 tid: SV_DispatchThreadID,
    uniform RWStructuredBuffer<float> buffer)
{
    if (tid.x == 0)
        buffer[0] = constBool ? 1.0 : 0.0;
    else if (tid.x == 1)
        buffer[1] = float(constInt);
    else if (tid.x == 2)
        buffer[2] = float(constUint);
    else if (tid.x == 3)
        buffer[3] = float(constFloat);
}
