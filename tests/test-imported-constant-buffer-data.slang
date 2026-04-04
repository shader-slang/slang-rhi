// Module with global ConstantBuffer — triggers varLayout assertion when imported.
module "test-imported-constant-buffer-data";

public struct MyData
{
    public float4 value;
};

public ConstantBuffer<MyData> gData;
