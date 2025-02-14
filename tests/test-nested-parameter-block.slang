// parameter-block.slang

struct CB
{
    uint4 value;
}

struct MaterialSystem
{
    CB cb;
    StructuredBuffer<uint4> data;
}

struct Scene
{
    CB sceneCb;
    StructuredBuffer<uint4> data;
    ParameterBlock<MaterialSystem> material;
}

struct PerView
{
    uint4 value;
}

ConstantBuffer<PerView> perView;

ParameterBlock<Scene> scene;

RWStructuredBuffer<uint4> resultBuffer;

[shader("compute")]
[numthreads(1,1,1)]
void computeMain(uint3 sv_dispatchThreadID : SV_DispatchThreadID)
{
    resultBuffer[sv_dispatchThreadID.x] = perView.value.x + scene.sceneCb.value.x + scene.data[0].x + scene.material.cb.value.x + scene.material.data[0].x;
}
