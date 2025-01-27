#include "d3d11-input-layout.h"
#include "d3d11-device.h"

namespace rhi::d3d11 {

Result DeviceImpl::createInputLayout(const InputLayoutDesc& desc, IInputLayout** outLayout)
{
    D3D11_INPUT_ELEMENT_DESC inputElements[16] = {};

    char hlslBuffer[1024];
    char* hlslEnd = &hlslBuffer[0] + sizeof(hlslBuffer);
    char* hlslPos = &hlslBuffer[0];

    hlslPos += snprintf(hlslPos, hlslEnd - hlslPos, "float4 main(\n");

    for (uint32_t i = 0; i < desc.inputElementCount; ++i)
    {
        const InputElementDesc& inputElement = desc.inputElements[i];
        uint32_t vertexStreamIndex = inputElement.bufferSlotIndex;
        const VertexStreamDesc& vertexStream = desc.vertexStreams[vertexStreamIndex];

        inputElements[i].SemanticName = inputElement.semanticName;
        inputElements[i].SemanticIndex = (UINT)inputElement.semanticIndex;
        inputElements[i].Format = D3DUtil::getMapFormat(inputElement.format);
        inputElements[i].InputSlot = (UINT)vertexStreamIndex;
        inputElements[i].AlignedByteOffset = (UINT)inputElement.offset;
        inputElements[i].InputSlotClass = (vertexStream.slotClass == InputSlotClass::PerInstance)
                                              ? D3D11_INPUT_PER_INSTANCE_DATA
                                              : D3D11_INPUT_PER_VERTEX_DATA;
        inputElements[i].InstanceDataStepRate = (UINT)vertexStream.instanceDataStepRate;

        if (i != 0)
        {
            hlslPos += snprintf(hlslPos, hlslEnd - hlslPos, ",\n");
        }

        const char* typeName = "Unknown";
        switch (inputElement.format)
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

        hlslPos += snprintf(
            hlslPos,
            hlslEnd - hlslPos,
            "%s a%d : %s%d",
            typeName,
            (int)i,
            inputElement.semanticName,
            (int)inputElement.semanticIndex
        );
    }

    hlslPos += snprintf(hlslPos, hlslEnd - hlslPos, "\n) : SV_Position { return 0; }");

    ComPtr<ID3DBlob> vertexShaderBlob;
    SLANG_RETURN_ON_FAIL(D3DUtil::compileHLSLShader("inputLayout", hlslBuffer, "main", "vs_5_0", vertexShaderBlob));

    ComPtr<ID3D11InputLayout> inputLayout;
    SLANG_RETURN_ON_FAIL(m_device->CreateInputLayout(
        &inputElements[0],
        (UINT)desc.inputElementCount,
        vertexShaderBlob->GetBufferPointer(),
        vertexShaderBlob->GetBufferSize(),
        inputLayout.writeRef()
    ));

    RefPtr<InputLayoutImpl> layout = new InputLayoutImpl();
    layout->m_layout.swap(inputLayout);

    layout->m_vertexStreamStrides.resize(desc.vertexStreamCount);
    for (uint32_t i = 0; i < desc.vertexStreamCount; ++i)
    {
        layout->m_vertexStreamStrides[i] = (UINT)desc.vertexStreams[i].stride;
    }

    returnComPtr(outLayout, layout);
    return SLANG_OK;
}

} // namespace rhi::d3d11
