#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugTransientResourceHeap : public DebugObject<ITransientResourceHeap>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ADD_REF;
    SLANG_COM_OBJECT_IUNKNOWN_RELEASE;

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL synchronizeAndReset() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL finish() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandBuffer(ICommandBuffer** outCommandBuffer) override;
};

class DebugTransientResourceHeapD3D12 : public DebugObject<ITransientResourceHeapD3D12>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ADD_REF;
    SLANG_COM_OBJECT_IUNKNOWN_RELEASE;

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL allocateTransientDescriptorTable(
        DescriptorType type,
        GfxCount count,
        Offset& outDescriptorOffset,
        void** outD3DDescriptorHeapHandle
    ) override;
};

} // namespace rhi::debug
