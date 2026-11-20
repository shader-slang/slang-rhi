[anyValueSize(16)]
interface IValue { int read(); }

export struct First : IValue
{
    int value;
    int read() { return value; }
}

export struct Second : IValue
{
    int value;
    int read() { return 2 * value; }
}

struct Child { int value; }
struct Params
{
    IValue field;
    ConstantBuffer<Child> child;
}

ConstantBuffer<Params> params;
RWStructuredBuffer<int> output;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain() { output[0] = params.field.read() + params.child.value; }
