#include "d3d11-input-layout.h"
#include "d3d11-device.h"

namespace rhi::d3d11 {

Result DeviceImpl::createInputLayout(InputLayoutDesc const& desc, IInputLayout** outLayout)
{
    D3D11_INPUT_ELEMENT_DESC inputElements[16] = {};

    char hlslBuffer[1024];
    char* hlslCursor = &hlslBuffer[0];

    hlslCursor += sprintf(hlslCursor, "float4 main(\n");

    auto inputElementCount = desc.inputElementCount;
    auto inputElementsIn = desc.inputElements;
    for (Int ii = 0; ii < inputElementCount; ++ii)
    {
        auto vertexStreamIndex = inputElementsIn[ii].bufferSlotIndex;
        auto& vertexStream = desc.vertexStreams[vertexStreamIndex];

        inputElements[ii].SemanticName = inputElementsIn[ii].semanticName;
        inputElements[ii].SemanticIndex = (UINT)inputElementsIn[ii].semanticIndex;
        inputElements[ii].Format = D3DUtil::getMapFormat(inputElementsIn[ii].format);
        inputElements[ii].InputSlot = (UINT)vertexStreamIndex;
        inputElements[ii].AlignedByteOffset = (UINT)inputElementsIn[ii].offset;
        inputElements[ii].InputSlotClass = (vertexStream.slotClass == InputSlotClass::PerInstance)
                                               ? D3D11_INPUT_PER_INSTANCE_DATA
                                               : D3D11_INPUT_PER_VERTEX_DATA;
        inputElements[ii].InstanceDataStepRate = (UINT)vertexStream.instanceDataStepRate;

        if (ii != 0)
        {
            hlslCursor += sprintf(hlslCursor, ",\n");
        }

        char const* typeName = "Unknown";
        switch (inputElementsIn[ii].format)
        {
        case Format::R32G32B32A32_FLOAT:
        case Format::R8G8B8A8_UNORM:
            typeName = "float4";
            break;
        case Format::R32G32B32_FLOAT:
            typeName = "float3";
            break;
        case Format::R32G32_FLOAT:
            typeName = "float2";
            break;
        case Format::R32_FLOAT:
            typeName = "float";
            break;
        default:
            return SLANG_FAIL;
        }

        hlslCursor += sprintf(
            hlslCursor,
            "%s a%d : %s%d",
            typeName,
            (int)ii,
            inputElementsIn[ii].semanticName,
            (int)inputElementsIn[ii].semanticIndex
        );
    }

    hlslCursor += sprintf(hlslCursor, "\n) : SV_Position { return 0; }");

    ComPtr<ID3DBlob> vertexShaderBlob;
    SLANG_RETURN_ON_FAIL(D3DUtil::compileHLSLShader("inputLayout", hlslBuffer, "main", "vs_5_0", vertexShaderBlob));

    ComPtr<ID3D11InputLayout> inputLayout;
    SLANG_RETURN_ON_FAIL(m_device->CreateInputLayout(
        &inputElements[0],
        (UINT)inputElementCount,
        vertexShaderBlob->GetBufferPointer(),
        vertexShaderBlob->GetBufferSize(),
        inputLayout.writeRef()
    ));

    RefPtr<InputLayoutImpl> impl = new InputLayoutImpl;
    impl->m_layout.swap(inputLayout);

    auto vertexStreamCount = desc.vertexStreamCount;
    impl->m_vertexStreamStrides.resize(vertexStreamCount);
    for (Int i = 0; i < vertexStreamCount; ++i)
    {
        impl->m_vertexStreamStrides[i] = (UINT)desc.vertexStreams[i].stride;
    }

    returnComPtr(outLayout, impl);
    return SLANG_OK;
}

} // namespace rhi::d3d11
