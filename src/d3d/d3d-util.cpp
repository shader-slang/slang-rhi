#include "d3d-util.h"

#include "core/common.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxgidebug.h>
#if SLANG_ENABLE_DXBC_SUPPORT
#include <d3dcompiler.h>
#endif

// We will use the C standard library just for printing error messages.
#include <stdio.h>

#if SLANG_RHI_ENABLE_AFTERMATH
#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_Defines.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"
#endif

namespace rhi {

D3D_PRIMITIVE_TOPOLOGY D3DUtil::getPrimitiveTopology(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::PointList:
        return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case PrimitiveTopology::LineList:
        return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case PrimitiveTopology::LineStrip:
        return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case PrimitiveTopology::TriangleList:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case PrimitiveTopology::TriangleStrip:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    default:
        break;
    }
    return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE D3DUtil::getPrimitiveTopologyType(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::PointList:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case PrimitiveTopology::LineList:
    case PrimitiveTopology::LineStrip:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case PrimitiveTopology::TriangleList:
    case PrimitiveTopology::TriangleStrip:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    case PrimitiveTopology::PatchList:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    default:
        break;
    }
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
}

D3D12_COMPARISON_FUNC D3DUtil::getComparisonFunc(ComparisonFunc func)
{
    switch (func)
    {
    case ComparisonFunc::Never:
        return D3D12_COMPARISON_FUNC_NEVER;
    case ComparisonFunc::Less:
        return D3D12_COMPARISON_FUNC_LESS;
    case ComparisonFunc::Equal:
        return D3D12_COMPARISON_FUNC_EQUAL;
    case ComparisonFunc::LessEqual:
        return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case ComparisonFunc::Greater:
        return D3D12_COMPARISON_FUNC_GREATER;
    case ComparisonFunc::NotEqual:
        return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case ComparisonFunc::GreaterEqual:
        return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case ComparisonFunc::Always:
        return D3D12_COMPARISON_FUNC_ALWAYS;
    default:
        return D3D12_COMPARISON_FUNC_NEVER;
    }
}

static D3D12_STENCIL_OP translateStencilOp(StencilOp op)
{
    switch (op)
    {
    case StencilOp::Keep:
        return D3D12_STENCIL_OP_KEEP;
    case StencilOp::Zero:
        return D3D12_STENCIL_OP_ZERO;
    case StencilOp::Replace:
        return D3D12_STENCIL_OP_REPLACE;
    case StencilOp::IncrementSaturate:
        return D3D12_STENCIL_OP_INCR_SAT;
    case StencilOp::DecrementSaturate:
        return D3D12_STENCIL_OP_DECR_SAT;
    case StencilOp::Invert:
        return D3D12_STENCIL_OP_INVERT;
    case StencilOp::IncrementWrap:
        return D3D12_STENCIL_OP_INCR;
    case StencilOp::DecrementWrap:
        return D3D12_STENCIL_OP_DECR;
    default:
        return D3D12_STENCIL_OP_KEEP;
    }
}

D3D12_DEPTH_STENCILOP_DESC D3DUtil::translateStencilOpDesc(DepthStencilOpDesc desc)
{
    D3D12_DEPTH_STENCILOP_DESC rs;
    rs.StencilDepthFailOp = translateStencilOp(desc.stencilDepthFailOp);
    rs.StencilFailOp = translateStencilOp(desc.stencilFailOp);
    rs.StencilFunc = getComparisonFunc(desc.stencilFunc);
    rs.StencilPassOp = translateStencilOp(desc.stencilPassOp);
    return rs;
}

const D3DUtil::FormatMapping& D3DUtil::getFormatMapping(Format format)
{
    static const FormatMapping mappings[] = {
        // clang-format off
        { Format::Undefined,        DXGI_FORMAT_UNKNOWN,                DXGI_FORMAT_UNKNOWN,                    DXGI_FORMAT_UNKNOWN                 },

        { Format::R8Uint,           DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_UINT,                    DXGI_FORMAT_R8_UINT                 },
        { Format::R8Sint,           DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_SINT,                    DXGI_FORMAT_R8_SINT                 },
        { Format::R8Unorm,          DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_UNORM,                   DXGI_FORMAT_R8_UNORM                },
        { Format::R8Snorm,          DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_SNORM,                   DXGI_FORMAT_R8_SNORM                },

        { Format::RG8Uint,          DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_UINT,                  DXGI_FORMAT_R8G8_UINT               },
        { Format::RG8Sint,          DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_SINT,                  DXGI_FORMAT_R8G8_SINT               },
        { Format::RG8Unorm,         DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_UNORM,                 DXGI_FORMAT_R8G8_UNORM              },
        { Format::RG8Snorm,         DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_SNORM,                 DXGI_FORMAT_R8G8_SNORM              },

        { Format::RGBA8Uint,        DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_UINT,              DXGI_FORMAT_R8G8B8A8_UINT           },
        { Format::RGBA8Sint,        DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_SINT,              DXGI_FORMAT_R8G8B8A8_SINT           },
        { Format::RGBA8Unorm,       DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_UNORM,             DXGI_FORMAT_R8G8B8A8_UNORM          },
        { Format::RGBA8UnormSrgb,   DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB     },
        { Format::RGBA8Snorm,       DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_SNORM,             DXGI_FORMAT_R8G8B8A8_SNORM          },

        { Format::BGRA8Unorm,       DXGI_FORMAT_B8G8R8A8_TYPELESS,      DXGI_FORMAT_B8G8R8A8_UNORM,             DXGI_FORMAT_B8G8R8A8_UNORM          },
        { Format::BGRA8UnormSrgb,   DXGI_FORMAT_B8G8R8A8_TYPELESS,      DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB     },
        { Format::BGRX8Unorm,       DXGI_FORMAT_B8G8R8A8_TYPELESS,      DXGI_FORMAT_B8G8R8X8_UNORM,             DXGI_FORMAT_B8G8R8X8_UNORM          },
        { Format::BGRX8UnormSrgb,   DXGI_FORMAT_B8G8R8A8_TYPELESS,      DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,        DXGI_FORMAT_B8G8R8X8_UNORM_SRGB     },

        { Format::R16Uint,          DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_UINT,                   DXGI_FORMAT_R16_UINT                },
        { Format::R16Sint,          DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_SINT,                   DXGI_FORMAT_R16_SINT                },
        { Format::R16Unorm,         DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_UNORM,                  DXGI_FORMAT_R16_UNORM               },
        { Format::R16Snorm,         DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_SNORM,                  DXGI_FORMAT_R16_SNORM               },
        { Format::R16Float,         DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_FLOAT,                  DXGI_FORMAT_R16_FLOAT               },

        { Format::RG16Uint,         DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_UINT,                DXGI_FORMAT_R16G16_UINT             },
        { Format::RG16Sint,         DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_SINT,                DXGI_FORMAT_R16G16_SINT             },
        { Format::RG16Unorm,        DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_UNORM,               DXGI_FORMAT_R16G16_UNORM            },
        { Format::RG16Snorm,        DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_SNORM,               DXGI_FORMAT_R16G16_SNORM            },
        { Format::RG16Float,        DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_FLOAT,               DXGI_FORMAT_R16G16_FLOAT            },

        { Format::RGBA16Uint,       DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_UINT,          DXGI_FORMAT_R16G16B16A16_UINT       },
        { Format::RGBA16Sint,       DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_SINT,          DXGI_FORMAT_R16G16B16A16_SINT       },
        { Format::RGBA16Unorm,      DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_UNORM,         DXGI_FORMAT_R16G16B16A16_UNORM      },
        { Format::RGBA16Snorm,      DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_SNORM,         DXGI_FORMAT_R16G16B16A16_SNORM      },
        { Format::RGBA16Float,      DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_FLOAT,         DXGI_FORMAT_R16G16B16A16_FLOAT      },

        { Format::R32Uint,          DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_UINT,                   DXGI_FORMAT_R32_UINT                },
        { Format::R32Sint,          DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_SINT,                   DXGI_FORMAT_R32_SINT                },
        { Format::R32Float,         DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_FLOAT,                  DXGI_FORMAT_R32_FLOAT               },

        { Format::RG32Uint,         DXGI_FORMAT_R32G32_TYPELESS,        DXGI_FORMAT_R32G32_UINT,                DXGI_FORMAT_R32G32_UINT             },
        { Format::RG32Sint,         DXGI_FORMAT_R32G32_TYPELESS,        DXGI_FORMAT_R32G32_SINT,                DXGI_FORMAT_R32G32_SINT             },
        { Format::RG32Float,        DXGI_FORMAT_R32G32_TYPELESS,        DXGI_FORMAT_R32G32_FLOAT,               DXGI_FORMAT_R32G32_FLOAT            },

        { Format::RGB32Uint,        DXGI_FORMAT_R32G32B32_TYPELESS,     DXGI_FORMAT_R32G32B32_UINT,             DXGI_FORMAT_R32G32B32_UINT          },
        { Format::RGB32Sint,        DXGI_FORMAT_R32G32B32_TYPELESS,     DXGI_FORMAT_R32G32B32_SINT,             DXGI_FORMAT_R32G32B32_SINT          },
        { Format::RGB32Float,       DXGI_FORMAT_R32G32B32_TYPELESS,     DXGI_FORMAT_R32G32B32_FLOAT,            DXGI_FORMAT_R32G32B32_FLOAT         },

        { Format::RGBA32Uint,       DXGI_FORMAT_R32G32B32A32_TYPELESS,  DXGI_FORMAT_R32G32B32A32_UINT,          DXGI_FORMAT_R32G32B32A32_UINT       },
        { Format::RGBA32Sint,       DXGI_FORMAT_R32G32B32A32_TYPELESS,  DXGI_FORMAT_R32G32B32A32_SINT,          DXGI_FORMAT_R32G32B32A32_SINT       },
        { Format::RGBA32Float,      DXGI_FORMAT_R32G32B32A32_TYPELESS,  DXGI_FORMAT_R32G32B32A32_FLOAT,         DXGI_FORMAT_R32G32B32A32_FLOAT      },

        { Format::R64Uint,          DXGI_FORMAT_UNKNOWN,                DXGI_FORMAT_UNKNOWN,                    DXGI_FORMAT_UNKNOWN                 },
        { Format::R64Sint,          DXGI_FORMAT_UNKNOWN,                DXGI_FORMAT_UNKNOWN,                    DXGI_FORMAT_UNKNOWN                 },

        { Format::BGRA4Unorm,       DXGI_FORMAT_B4G4R4A4_UNORM,         DXGI_FORMAT_B4G4R4A4_UNORM,             DXGI_FORMAT_B4G4R4A4_UNORM          },
        { Format::B5G6R5Unorm,      DXGI_FORMAT_B5G6R5_UNORM,           DXGI_FORMAT_B5G6R5_UNORM,               DXGI_FORMAT_B5G6R5_UNORM            },
        { Format::BGR5A1Unorm,      DXGI_FORMAT_B5G5R5A1_UNORM,         DXGI_FORMAT_B5G5R5A1_UNORM,             DXGI_FORMAT_B5G5R5A1_UNORM          },

        { Format::RGB9E5Ufloat,     DXGI_FORMAT_R9G9B9E5_SHAREDEXP,     DXGI_FORMAT_R9G9B9E5_SHAREDEXP,         DXGI_FORMAT_R9G9B9E5_SHAREDEXP      },
        { Format::RGB10A2Uint,      DXGI_FORMAT_R10G10B10A2_TYPELESS,   DXGI_FORMAT_R10G10B10A2_UINT,           DXGI_FORMAT_R10G10B10A2_UINT        },
        { Format::RGB10A2Unorm,     DXGI_FORMAT_R10G10B10A2_TYPELESS,   DXGI_FORMAT_R10G10B10A2_UNORM,          DXGI_FORMAT_R10G10B10A2_UNORM       },
        { Format::R11G11B10Float,   DXGI_FORMAT_R11G11B10_FLOAT,        DXGI_FORMAT_R11G11B10_FLOAT,            DXGI_FORMAT_R11G11B10_FLOAT         },

        { Format::D32Float,         DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_FLOAT,                  DXGI_FORMAT_D32_FLOAT               },
        { Format::D16Unorm,         DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_UNORM,                  DXGI_FORMAT_D16_UNORM               },
        { Format::D32FloatS8Uint,   DXGI_FORMAT_R32G8X24_TYPELESS,      DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,   DXGI_FORMAT_D32_FLOAT_S8X24_UINT    },

        { Format::BC1Unorm,         DXGI_FORMAT_BC1_TYPELESS,           DXGI_FORMAT_BC1_UNORM,                  DXGI_FORMAT_BC1_UNORM               },
        { Format::BC1UnormSrgb,     DXGI_FORMAT_BC1_TYPELESS,           DXGI_FORMAT_BC1_UNORM_SRGB,             DXGI_FORMAT_BC1_UNORM_SRGB          },
        { Format::BC2Unorm,         DXGI_FORMAT_BC2_TYPELESS,           DXGI_FORMAT_BC2_UNORM,                  DXGI_FORMAT_BC2_UNORM               },
        { Format::BC2UnormSrgb,     DXGI_FORMAT_BC2_TYPELESS,           DXGI_FORMAT_BC2_UNORM_SRGB,             DXGI_FORMAT_BC2_UNORM_SRGB          },
        { Format::BC3Unorm,         DXGI_FORMAT_BC3_TYPELESS,           DXGI_FORMAT_BC3_UNORM,                  DXGI_FORMAT_BC3_UNORM               },
        { Format::BC3UnormSrgb,     DXGI_FORMAT_BC3_TYPELESS,           DXGI_FORMAT_BC3_UNORM_SRGB,             DXGI_FORMAT_BC3_UNORM_SRGB          },
        { Format::BC4Unorm,         DXGI_FORMAT_BC4_TYPELESS,           DXGI_FORMAT_BC4_UNORM,                  DXGI_FORMAT_BC4_UNORM               },
        { Format::BC4Snorm,         DXGI_FORMAT_BC4_TYPELESS,           DXGI_FORMAT_BC4_SNORM,                  DXGI_FORMAT_BC4_SNORM               },
        { Format::BC5Unorm,         DXGI_FORMAT_BC5_TYPELESS,           DXGI_FORMAT_BC5_UNORM,                  DXGI_FORMAT_BC5_UNORM               },
        { Format::BC5Snorm,         DXGI_FORMAT_BC5_TYPELESS,           DXGI_FORMAT_BC5_SNORM,                  DXGI_FORMAT_BC5_SNORM               },
        { Format::BC6HUfloat,       DXGI_FORMAT_BC6H_TYPELESS,          DXGI_FORMAT_BC6H_UF16,                  DXGI_FORMAT_BC6H_UF16               },
        { Format::BC6HSfloat,       DXGI_FORMAT_BC6H_TYPELESS,          DXGI_FORMAT_BC6H_SF16,                  DXGI_FORMAT_BC6H_SF16               },
        { Format::BC7Unorm,         DXGI_FORMAT_BC7_TYPELESS,           DXGI_FORMAT_BC7_UNORM,                  DXGI_FORMAT_BC7_UNORM               },
        { Format::BC7UnormSrgb,     DXGI_FORMAT_BC7_TYPELESS,           DXGI_FORMAT_BC7_UNORM_SRGB,             DXGI_FORMAT_BC7_UNORM_SRGB          },
        // clang-format on
    };

    static_assert(SLANG_COUNT_OF(mappings) == size_t(Format::_Count), "Missing format mapping");
    SLANG_RHI_ASSERT(uint32_t(format) < uint32_t(Format::_Count));
    return mappings[int(format)];
}

DXGI_FORMAT D3DUtil::getMapFormat(Format format)
{
    return getFormatMapping(format).rtvFormat;
}

DXGI_FORMAT D3DUtil::getVertexFormat(Format format)
{
    return getFormatMapping(format).srvFormat;
}

DXGI_FORMAT D3DUtil::getIndexFormat(IndexFormat indexFormat)
{
    switch (indexFormat)
    {
    case IndexFormat::Uint16:
        return DXGI_FORMAT_R16_UINT;
    case IndexFormat::Uint32:
        return DXGI_FORMAT_R32_UINT;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

// Note: this subroutine is now only used by D3D11 for generating bytecode to go into input layouts.
//
// TODO: we can probably remove that code completely by switching to a PSO-like model across all APIs.
//
Result D3DUtil::compileHLSLShader(
    const char* sourcePath,
    const char* source,
    const char* entryPointName,
    const char* dxProfileName,
    ComPtr<ID3DBlob>& shaderBlobOut
)
{
#if !SLANG_ENABLE_DXBC_SUPPORT
    return SLANG_E_NOT_IMPLEMENTED;
#else
    // Rather than statically link against the `d3dcompile` library, we
    // dynamically load it.
    //
    // Note: A more realistic application would compile from HLSL text to D3D
    // shader bytecode as part of an offline process, rather than doing it
    // on-the-fly like this
    //
    static pD3DCompile compileFunc = nullptr;
    if (!compileFunc)
    {
        // On Linux, vkd3d-utils isn't suitable as a unix replacement for fxc
        // due to at least the following missing feature:
        // https://bugs.winehq.org/show_bug.cgi?id=54872

        // TODO(tfoley): maybe want to search for one of a few versions of the DLL
        const char* const libName = "d3dcompiler_47";
        SharedLibraryHandle compilerModule;
        if (SLANG_FAILED(loadSharedLibrary(libName, compilerModule)))
        {
            fprintf(stderr, "error: failed to load '%s'\n", libName);
            return SLANG_FAIL;
        }

        compileFunc = (pD3DCompile)findSymbolAddressByName(compilerModule, "D3DCompile");
        if (!compileFunc)
        {
            fprintf(stderr, "error: failed load symbol 'D3DCompile'\n");
            return SLANG_FAIL;
        }
    }

    // For this example, we turn on debug output, and turn off all
    // optimization. A real application would only use these flags
    // when shader debugging is needed.
    UINT flags = 0;
    flags |= D3DCOMPILE_DEBUG;
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL0 | D3DCOMPILE_SKIP_OPTIMIZATION;

    // We will always define `__HLSL__` when compiling here, so that
    // input code can react differently to being compiled as pure HLSL.
    D3D_SHADER_MACRO defines[] = {
        {"__HLSL__", "1"},
        {nullptr, nullptr},
    };

    // The `D3DCompile` entry point takes a bunch of parameters, but we
    // don't really need most of them for Slang-generated code.
    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = compileFunc(
        source,
        strlen(source),
        sourcePath,
        &defines[0],
        nullptr,
        entryPointName,
        dxProfileName,
        flags,
        0,
        shaderBlob.writeRef(),
        errorBlob.writeRef()
    );

    // If the HLSL-to-bytecode compilation produced any diagnostic messages
    // then we will print them out (whether or not the compilation failed).
    if (errorBlob)
    {
        ::fputs((const char*)errorBlob->GetBufferPointer(), stderr);
        ::fflush(stderr);
#if SLANG_WINDOWS_FAMILY
        ::OutputDebugStringA((const char*)errorBlob->GetBufferPointer());
#endif
    }

    SLANG_RETURN_ON_FAIL(hr);
    shaderBlobOut.swap(shaderBlob);
    return SLANG_OK;
#endif // SLANG_ENABLE_DXBC_SUPPORT
}

SharedLibraryHandle D3DUtil::getDxgiModule()
{
#if SLANG_WINDOWS_FAMILY
    const char* const libName = "dxgi";
#else
    const char* const libName = "libdxvk_dxgi.so";
#endif

    static SharedLibraryHandle s_dxgiModule = [&]()
    {
        SharedLibraryHandle h = nullptr;
        loadSharedLibrary(libName, h);
        if (!h)
        {
            fprintf(stderr, "error: failed to load dll '%s'\n", libName);
        }
        return h;
    }();
    return s_dxgiModule;
}

Result D3DUtil::createFactory(DeviceCheckFlags flags, ComPtr<IDXGIFactory>& outFactory)
{
    auto dxgiModule = getDxgiModule();
    if (!dxgiModule)
    {
        return SLANG_FAIL;
    }

    typedef HRESULT(WINAPI * PFN_DXGI_CREATE_FACTORY)(REFIID riid, void** ppFactory);
    typedef HRESULT(WINAPI * PFN_DXGI_CREATE_FACTORY_2)(UINT Flags, REFIID riid, void** ppFactory);

    {
        auto createFactory2 = (PFN_DXGI_CREATE_FACTORY_2)findSymbolAddressByName(dxgiModule, "CreateDXGIFactory2");
        if (createFactory2)
        {
            UINT dxgiFlags = 0;

            if (flags & DeviceCheckFlag::UseDebug)
            {
                dxgiFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }

            ComPtr<IDXGIFactory4> factory;
            SLANG_RETURN_ON_FAIL(createFactory2(dxgiFlags, IID_PPV_ARGS(factory.writeRef())));

            outFactory = factory;
            return SLANG_OK;
        }
    }

    {
        auto createFactory = (PFN_DXGI_CREATE_FACTORY)findSymbolAddressByName(dxgiModule, "CreateDXGIFactory");
        if (!createFactory)
        {
            fprintf(stderr, "error: failed load symbol '%s'\n", "CreateDXGIFactory");
            return SLANG_FAIL;
        }
        return createFactory(IID_PPV_ARGS(outFactory.writeRef()));
    }
}

Result D3DUtil::findAdapters(
    DeviceCheckFlags flags,
    const AdapterLUID* adapterLUID,
    std::vector<ComPtr<IDXGIAdapter>>& outDxgiAdapters
)
{
    ComPtr<IDXGIFactory> factory;
    SLANG_RETURN_ON_FAIL(createFactory(flags, factory));
    return findAdapters(flags, adapterLUID, factory, outDxgiAdapters);
}

AdapterLUID D3DUtil::getAdapterLUID(IDXGIAdapter* dxgiAdapter)
{
    DXGI_ADAPTER_DESC desc;
    dxgiAdapter->GetDesc(&desc);
    AdapterLUID luid = {};
    SLANG_RHI_ASSERT(sizeof(AdapterLUID) >= sizeof(LUID));
    memcpy(&luid, &desc.AdapterLuid, sizeof(LUID));
    return luid;
}

bool D3DUtil::isWarp(IDXGIFactory* dxgiFactory, IDXGIAdapter* adapterIn)
{
    ComPtr<IDXGIFactory4> dxgiFactory4;
    if (SLANG_SUCCEEDED(dxgiFactory->QueryInterface(IID_PPV_ARGS(dxgiFactory4.writeRef()))))
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        dxgiFactory4->EnumWarpAdapter(IID_PPV_ARGS(warpAdapter.writeRef()));

        return adapterIn == warpAdapter;
    }

    return false;
}

uint32_t D3DUtil::getPlaneSliceCount(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return 2;
    default:
        return 1;
    }
}

uint32_t D3DUtil::getPlaneSlice(DXGI_FORMAT format, TextureAspect aspect)
{
    switch (aspect)
    {
    case TextureAspect::All:
    case TextureAspect::DepthOnly:
        return 0;
    case TextureAspect::StencilOnly:
        switch (format)
        {
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return 1;
        default:
            return 0;
        }
    default:
        SLANG_RHI_ASSERT_FAILURE("Unknown texture aspect.");
        return 0;
    }
}

D3D12_INPUT_CLASSIFICATION D3DUtil::getInputSlotClass(InputSlotClass slotClass)
{
    switch (slotClass)
    {
    case InputSlotClass::PerVertex:
        return D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    case InputSlotClass::PerInstance:
        return D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unknown input slot class.");
        return D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    }
}

D3D12_FILL_MODE D3DUtil::getFillMode(FillMode mode)
{
    switch (mode)
    {
    case FillMode::Solid:
        return D3D12_FILL_MODE_SOLID;
    case FillMode::Wireframe:
        return D3D12_FILL_MODE_WIREFRAME;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unknown fill mode.");
        return D3D12_FILL_MODE_SOLID;
    }
}

D3D12_CULL_MODE D3DUtil::getCullMode(CullMode mode)
{
    switch (mode)
    {
    case CullMode::None:
        return D3D12_CULL_MODE_NONE;
    case CullMode::Front:
        return D3D12_CULL_MODE_FRONT;
    case CullMode::Back:
        return D3D12_CULL_MODE_BACK;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unknown cull mode.");
        return D3D12_CULL_MODE_NONE;
    }
}

D3D12_BLEND_OP D3DUtil::getBlendOp(BlendOp op)
{
    switch (op)
    {
    case BlendOp::Add:
        return D3D12_BLEND_OP_ADD;
    case BlendOp::Subtract:
        return D3D12_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract:
        return D3D12_BLEND_OP_REV_SUBTRACT;
    case BlendOp::Min:
        return D3D12_BLEND_OP_MIN;
    case BlendOp::Max:
        return D3D12_BLEND_OP_MAX;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unknown blend op.");
        return D3D12_BLEND_OP_ADD;
    }
}

D3D12_BLEND D3DUtil::getBlendFactor(BlendFactor factor)
{
    switch (factor)
    {
    case BlendFactor::Zero:
        return D3D12_BLEND_ZERO;
    case BlendFactor::One:
        return D3D12_BLEND_ONE;
    case BlendFactor::SrcColor:
        return D3D12_BLEND_SRC_COLOR;
    case BlendFactor::InvSrcColor:
        return D3D12_BLEND_INV_SRC_COLOR;
    case BlendFactor::SrcAlpha:
        return D3D12_BLEND_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha:
        return D3D12_BLEND_INV_SRC_ALPHA;
    case BlendFactor::DestAlpha:
        return D3D12_BLEND_DEST_ALPHA;
    case BlendFactor::InvDestAlpha:
        return D3D12_BLEND_INV_DEST_ALPHA;
    case BlendFactor::DestColor:
        return D3D12_BLEND_DEST_COLOR;
    case BlendFactor::InvDestColor:
        return D3D12_BLEND_INV_DEST_COLOR;
    case BlendFactor::SrcAlphaSaturate:
        return D3D12_BLEND_SRC_ALPHA_SAT;
    case BlendFactor::BlendColor:
        return D3D12_BLEND_BLEND_FACTOR;
    case BlendFactor::InvBlendColor:
        return D3D12_BLEND_INV_BLEND_FACTOR;
    case BlendFactor::SecondarySrcColor:
        return D3D12_BLEND_SRC1_COLOR;
    case BlendFactor::InvSecondarySrcColor:
        return D3D12_BLEND_INV_SRC1_COLOR;
    case BlendFactor::SecondarySrcAlpha:
        return D3D12_BLEND_SRC1_ALPHA;
    case BlendFactor::InvSecondarySrcAlpha:
        return D3D12_BLEND_INV_SRC1_ALPHA;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unknown blend factor.");
        return D3D12_BLEND_ZERO;
    }
}

uint32_t D3DUtil::getSubresourceIndex(
    uint32_t mipIndex,
    uint32_t arrayIndex,
    uint32_t planeIndex,
    uint32_t mipCount,
    uint32_t layerCount
)
{
    return mipIndex + arrayIndex * mipCount + planeIndex * mipCount * layerCount;
}

uint32_t D3DUtil::getSubresourceMip(uint32_t subresourceIndex, uint32_t mipCount)
{
    return subresourceIndex % mipCount;
}

D3D12_RESOURCE_STATES D3DUtil::getResourceState(ResourceState state)
{
    switch (state)
    {
    case ResourceState::Undefined:
        return D3D12_RESOURCE_STATE_COMMON;
    case ResourceState::General:
        return D3D12_RESOURCE_STATE_COMMON;
    case ResourceState::VertexBuffer:
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case ResourceState::IndexBuffer:
        return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    case ResourceState::ConstantBuffer:
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case ResourceState::StreamOutput:
        return D3D12_RESOURCE_STATE_STREAM_OUT;
    case ResourceState::ShaderResource:
    case ResourceState::AccelerationStructureBuildInput:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case ResourceState::UnorderedAccess:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case ResourceState::RenderTarget:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case ResourceState::DepthRead:
        return D3D12_RESOURCE_STATE_DEPTH_READ;
    case ResourceState::DepthWrite:;
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case ResourceState::Present:
        return D3D12_RESOURCE_STATE_PRESENT;
    case ResourceState::IndirectArgument:
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case ResourceState::CopySource:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case ResourceState::CopyDestination:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case ResourceState::ResolveSource:
        return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    case ResourceState::ResolveDestination:
        return D3D12_RESOURCE_STATE_RESOLVE_DEST;
    case ResourceState::AccelerationStructure:
        return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    default:
        return D3D12_RESOURCE_STATE_COMMON;
    }
}

Result D3DUtil::reportLiveObjects()
{
    static IDXGIDebug* dxgiDebug = nullptr;

#if SLANG_ENABLE_DXGI_DEBUG
    if (!dxgiDebug)
    {
        HMODULE debugModule = LoadLibraryA("dxgidebug.dll");
        if (debugModule != INVALID_HANDLE_VALUE)
        {
            auto fun =
                reinterpret_cast<decltype(&DXGIGetDebugInterface)>(GetProcAddress(debugModule, "DXGIGetDebugInterface")
                );
            if (fun)
            {
                fun(__uuidof(IDXGIDebug), (void**)&dxgiDebug);
            }
        }
    }
#endif

    if (dxgiDebug)
    {
        const GUID DXGI_DEBUG_ALL_ = {0xe48ae283, 0xda80, 0x490b, {0x87, 0xe6, 0x43, 0xe9, 0xa9, 0xcf, 0xda, 0x8}};
        dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL_, DXGI_DEBUG_RLO_ALL);
        return SLANG_OK;
    }

    return SLANG_E_NOT_AVAILABLE;
}

Result SLANG_MCALL reportD3DLiveObjects()
{
    return D3DUtil::reportLiveObjects();
}

Result D3DUtil::waitForCrashDumpCompletion(HRESULT res)
{
    // If it's not a device remove/reset then theres nothing to wait for
    if (!(res == DXGI_ERROR_DEVICE_REMOVED || res == DXGI_ERROR_DEVICE_RESET))
    {
        return SLANG_OK;
    }

#if SLANG_RHI_NV_AFTERMATH
    {
        GFSDK_Aftermath_CrashDump_Status status = GFSDK_Aftermath_CrashDump_Status_Unknown;
        if (GFSDK_Aftermath_GetCrashDumpStatus(&status) != GFSDK_Aftermath_Result_Success)
        {
            return SLANG_FAIL;
        }

        const auto startTick = Process::getClockTick();
        const auto frequency = Process::getClockFrequency();

        float timeOutInSecs = 1.0f;

        uint64_t timeOutTicks = uint64_t(frequency * timeOutInSecs) + 1;

        // Loop while Aftermath crash dump data collection has not finished or
        // the application is still processing the crash dump data.
        while (status != GFSDK_Aftermath_CrashDump_Status_CollectingDataFailed &&
               status != GFSDK_Aftermath_CrashDump_Status_Finished &&
               Process::getClockTick() - startTick < timeOutTicks)
        {
            // Sleep a couple of milliseconds and poll the status again.
            Process::sleepCurrentThread(50);
            if (GFSDK_Aftermath_GetCrashDumpStatus(&status) != GFSDK_Aftermath_Result_Success)
            {
                return SLANG_FAIL;
            }
        }

        if (status == GFSDK_Aftermath_CrashDump_Status_Finished)
        {
            return SLANG_OK;
        }
        else
        {
            return SLANG_E_TIME_OUT;
        }
    }
#endif

    return SLANG_OK;
}

Result D3DUtil::findAdapters(
    DeviceCheckFlags flags,
    const AdapterLUID* adapterLUID,
    IDXGIFactory* dxgiFactory,
    std::vector<ComPtr<IDXGIAdapter>>& outDxgiAdapters
)
{
    outDxgiAdapters.clear();

    ComPtr<IDXGIAdapter> warpAdapter;
    if ((flags & DeviceCheckFlag::UseHardwareDevice) == 0)
    {
        ComPtr<IDXGIFactory4> dxgiFactory4;
        if (SLANG_SUCCEEDED(dxgiFactory->QueryInterface(IID_PPV_ARGS(dxgiFactory4.writeRef()))))
        {
            dxgiFactory4->EnumWarpAdapter(IID_PPV_ARGS(warpAdapter.writeRef()));
            if (!adapterLUID || D3DUtil::getAdapterLUID(warpAdapter) == *adapterLUID)
            {
                outDxgiAdapters.push_back(warpAdapter);
            }
        }
    }

    for (UINT adapterIndex = 0; true; adapterIndex++)
    {
        ComPtr<IDXGIAdapter> dxgiAdapter;
        if (dxgiFactory->EnumAdapters(adapterIndex, dxgiAdapter.writeRef()) == DXGI_ERROR_NOT_FOUND)
            break;

        // Skip if warp (as we will have already added it)
        if (dxgiAdapter == warpAdapter)
        {
            continue;
        }
        if (adapterLUID && D3DUtil::getAdapterLUID(dxgiAdapter) != *adapterLUID)
        {
            continue;
        }

        // Get if it's software
        UINT deviceFlags = 0;
        ComPtr<IDXGIAdapter1> dxgiAdapter1;
        if (SLANG_SUCCEEDED(dxgiAdapter->QueryInterface(IID_PPV_ARGS(dxgiAdapter1.writeRef()))))
        {
            DXGI_ADAPTER_DESC1 desc;
            dxgiAdapter1->GetDesc1(&desc);
            deviceFlags = desc.Flags;
        }

        // If the right type then add it
        if ((deviceFlags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 && (flags & DeviceCheckFlag::UseHardwareDevice) != 0)
        {
            outDxgiAdapters.push_back(dxgiAdapter);
        }
    }

    return SLANG_OK;
}

} // namespace rhi
