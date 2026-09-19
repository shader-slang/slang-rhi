struct Material
{
    StructuredBuffer<uint> a;
    StructuredBuffer<uint> b;
    StructuredBuffer<uint> c;
    StructuredBuffer<uint> d;
    uint bias;
    uint4 data[15];
};
ParameterBlock<Material> first;
ParameterBlock<Material> second;
uint iteration;
uint4 payload[16];

uint materialValue(Material m)
{
    return m.a[0] + m.b[0] + m.c[0] + m.d[0] + m.bias + m.data[0].x + m.data[14].w;
}
uint bindingValue()
{
    return iteration + payload[0].x + payload[15].w + materialValue(first) + materialValue(second);
}
