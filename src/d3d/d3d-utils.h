#pragma once

#include "core/common.h"

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
Result createDXGIFactory(bool debug, ComPtr<IDXGIFactory>& outFactory);
ComPtr<IDXGIFactory> getDXGIFactory();

Result enumAdapters(IDXGIFactory* dxgiFactory, std::vector<ComPtr<IDXGIAdapter>>& outAdapters);
Result enumAdapters(std::vector<ComPtr<IDXGIAdapter>>& outAdapters);

AdapterInfo getAdapterInfo(IDXGIAdapter* dxgiAdapter);

AdapterLUID getAdapterLUID(LUID luid);

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

/// Call after a DXGI_ERROR_DEVICE_REMOVED/DXGI_ERROR_DEVICE_RESET on present, to wait for
/// dumping to complete. Will return SLANG_OK if wait happened successfully
Result waitForCrashDumpCompletion(HRESULT res);

} // namespace rhi
