#include "d3d-utils.h"

#include "core/common.h"

#include <dxgi1_6.h>
#include <dxgidebug.h>
#if SLANG_ENABLE_DXBC_SUPPORT
#include <d3dcompiler.h>
#endif

// We will use the C standard library just for printing error messages.
#include <cstdio>

#if SLANG_RHI_ENABLE_AFTERMATH
#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_Defines.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"
#endif

namespace rhi {

const FormatMapping& getFormatMapping(Format format)
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

DXGI_FORMAT getMapFormat(Format format)
{
    return getFormatMapping(format).rtvFormat;
}

DXGI_FORMAT getVertexFormat(Format format)
{
    return getFormatMapping(format).srvFormat;
}

DXGI_FORMAT getIndexFormat(IndexFormat indexFormat)
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

D3D_PRIMITIVE_TOPOLOGY translatePrimitiveTopology(PrimitiveTopology topology)
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


// Note: this subroutine is now only used by D3D11 for generating bytecode to go into input layouts.
//
// TODO: we can probably remove that code completely by switching to a PSO-like model across all APIs.
//
Result compileHLSLShader(
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

SharedLibraryHandle getDXGIModule()
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

Result createDXGIFactory(bool debug, ComPtr<IDXGIFactory>& outFactory)
{
    auto dxgiModule = getDXGIModule();
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
            ComPtr<IDXGIFactory4> factory;
            Result result = SLANG_FAIL;
            if (debug)
            {
                result = (createFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(factory.writeRef())));
            }
            if (SLANG_FAILED(result))
            {
                result = createFactory2(0, IID_PPV_ARGS(factory.writeRef()));
            }
            if (SLANG_SUCCEEDED(result))
            {
                outFactory = factory;
            }
            return result;
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

bool isDebugLayersEnabled();

ComPtr<IDXGIFactory> getDXGIFactory()
{
    static ComPtr<IDXGIFactory> factory = []()
    {
        ComPtr<IDXGIFactory> f;
        if (SLANG_FAILED(createDXGIFactory(isDebugLayersEnabled(), f)))
        {
            return ComPtr<IDXGIFactory>();
        }
        return f;
    }();
    return factory;
}

Result enumAdapters(IDXGIFactory* dxgiFactory, std::vector<ComPtr<IDXGIAdapter>>& outAdapters)
{
    ComPtr<IDXGIFactory6> dxgiFactory6;
    dxgiFactory->QueryInterface(IID_PPV_ARGS(dxgiFactory6.writeRef()));
    if (dxgiFactory6)
    {
        UINT i = 0;
        ComPtr<IDXGIAdapter4> adapter;
        while (dxgiFactory6->EnumAdapterByGpuPreference(
                   i,
                   DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                   IID_PPV_ARGS(adapter.writeRef())
               ) != DXGI_ERROR_NOT_FOUND)
        {
            outAdapters.push_back(ComPtr<IDXGIAdapter>(static_cast<IDXGIAdapter*>(adapter.get())));
            ++i;
        }
        return SLANG_OK;
    }
    ComPtr<IDXGIFactory1> dxgiFactory1;
    dxgiFactory->QueryInterface(IID_PPV_ARGS(dxgiFactory1.writeRef()));
    if (dxgiFactory1)
    {
        UINT i = 0;
        ComPtr<IDXGIAdapter1> adapter;
        while (dxgiFactory1->EnumAdapters1(i, adapter.writeRef()) != DXGI_ERROR_NOT_FOUND)
        {
            outAdapters.push_back(ComPtr<IDXGIAdapter>(static_cast<IDXGIAdapter*>(adapter.get())));
            ++i;
        }
    }
    else
    {
        UINT i = 0;
        ComPtr<IDXGIAdapter> adapter;
        while (dxgiFactory->EnumAdapters(i, adapter.writeRef()) != DXGI_ERROR_NOT_FOUND)
        {
            outAdapters.push_back(adapter);
            ++i;
        }
    }
    return SLANG_OK;
}

Result enumAdapters(std::vector<ComPtr<IDXGIAdapter>>& outAdapters)
{
    ComPtr<IDXGIFactory> factory = getDXGIFactory();
    if (!factory)
    {
        return SLANG_FAIL;
    }
    return enumAdapters(factory, outAdapters);
}

AdapterInfo getAdapterInfo(IDXGIAdapter* dxgiAdapter)
{
    DXGI_ADAPTER_DESC desc;
    dxgiAdapter->GetDesc(&desc);
    AdapterInfo info = {};
    info.adapterType = desc.DedicatedVideoMemory > 0 ? AdapterType::Discrete : AdapterType::Integrated;

    // Check for software adapter.
    ComPtr<IDXGIAdapter1> dxgiAdapter1;
    if (SUCCEEDED(dxgiAdapter->QueryInterface(dxgiAdapter1.writeRef())))
    {
        DXGI_ADAPTER_DESC1 desc1;
        dxgiAdapter1->GetDesc1(&desc1);
        if (desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            info.adapterType = AdapterType::Software;
        }
    }
    else
    {
        // Fallback: Check for WARP adapter.
        if (desc.VendorId == 0x1414 && desc.DeviceId == 0x8c)
        {
            info.adapterType = AdapterType::Software;
        }
    }

    auto name = string::from_wstring(desc.Description);
    string::copy_safe(info.name, sizeof(info.name), name.c_str());
    info.vendorID = desc.VendorId;
    info.deviceID = desc.DeviceId;
    info.luid = getAdapterLUID(desc.AdapterLuid);

    return info;
}

AdapterLUID getAdapterLUID(LUID luid)
{
    AdapterLUID adapterLUID = {};
    SLANG_RHI_ASSERT(sizeof(AdapterLUID) >= sizeof(LUID));
    memcpy(&adapterLUID, &luid, sizeof(LUID));
    return adapterLUID;
}

uint32_t getPlaneSliceCount(DXGI_FORMAT format)
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

uint32_t getPlaneSlice(DXGI_FORMAT format, TextureAspect aspect)
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

uint32_t getSubresourceIndex(
    uint32_t mipIndex,
    uint32_t arrayIndex,
    uint32_t planeIndex,
    uint32_t mipCount,
    uint32_t layerCount
)
{
    return mipIndex + arrayIndex * mipCount + planeIndex * mipCount * layerCount;
}

Result reportLiveObjects()
{
    static IDXGIDebug* dxgiDebug = nullptr;

#if SLANG_ENABLE_DXGI_DEBUG
    if (!dxgiDebug)
    {
        HMODULE debugModule = LoadLibraryA("dxgidebug.dll");
        if (debugModule != INVALID_HANDLE_VALUE)
        {
            auto fun = reinterpret_cast<decltype(&DXGIGetDebugInterface)>(
                GetProcAddress(debugModule, "DXGIGetDebugInterface")
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

Result reportD3DLiveObjects()
{
    return reportLiveObjects();
}

Result waitForCrashDumpCompletion(HRESULT res)
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

} // namespace rhi
