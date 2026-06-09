#pragma once

#include "device.h"
#include "core/common.h"
#include "core/diagnostics.h"

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang-rhi.h>

#include <d3dcommon.h>
#include <dxgi.h>
#include <dxgi1_4.h>

namespace rhi {

struct FormatMapping
{
    Format format;
    DXGI_FORMAT typelessFormat;
    DXGI_FORMAT srvFormat;
    DXGI_FORMAT rtvFormat;
};

const FormatMapping& getFormatMapping(Format format);

/// Given a slang pixel format returns the equivalent DXGI_ pixel format. If the format is not known, will return
/// DXGI_FORMAT_UNKNOWN
DXGI_FORMAT getMapFormat(Format format);

DXGI_FORMAT getVertexFormat(Format format);
DXGI_FORMAT getIndexFormat(IndexFormat indexFormat);

/// Get primitive topology as D3D primitive topology
D3D_PRIMITIVE_TOPOLOGY translatePrimitiveTopology(PrimitiveTopology prim);

/// Compile HLSL code to DXBC
Result compileHLSLShader(
    const char* sourcePath,
    const char* source,
    const char* entryPointName,
    const char* dxProfileName,
    ComPtr<ID3DBlob>& shaderBlobOut
);

SharedLibraryHandle getDXGIModule();
void clearDXGIModule();

Result createDXGIFactory(bool debug, ComPtr<IDXGIFactory>& outFactory);
ComPtr<IDXGIFactory> getDXGIFactory(DebugLayerOptions debugLayerOptions, Device* device);
void clearDXGIFactory();

Result enumAdapters(IDXGIFactory* dxgiFactory, std::vector<ComPtr<IDXGIAdapter>>& outAdapters);
Result enumAdapters(std::vector<ComPtr<IDXGIAdapter>>& outAdapters);

AdapterInfo getAdapterInfo(IDXGIAdapter* dxgiAdapter);

AdapterLUID getAdapterLUID(LUID luid);

const char* getHRESULTName(HRESULT result);

void reportD3DError(HRESULT result, const char* call, const SourceLocation location, Device* device = nullptr);

#define SLANG_D3D_RETURN_ON_FAIL(x)                                                                                    \
    {                                                                                                                  \
        HRESULT _res = x;                                                                                              \
        if (FAILED(_res))                                                                                              \
        {                                                                                                              \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
    }

/// Pass nullptr for device to write the diagnostic to stderr.
#define SLANG_D3D_RETURN_ON_FAIL_REPORT(x, device)                                                                     \
    {                                                                                                                  \
        HRESULT _res = x;                                                                                              \
        if (FAILED(_res))                                                                                              \
        {                                                                                                              \
            ::rhi::reportD3DError(_res, #x, SLANG_RHI_SOURCE_LOCATION(), device);                                      \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
    }

uint32_t getPlaneSlice(DXGI_FORMAT format, TextureAspect aspect);

uint32_t getPlaneSliceCount(DXGI_FORMAT format);

uint32_t getSubresourceIndex(
    uint32_t mipIndex,
    uint32_t arrayIndex,
    uint32_t planeIndex,
    uint32_t mipCount,
    uint32_t layoutCount
);

Result reportLiveObjects();

} // namespace rhi
