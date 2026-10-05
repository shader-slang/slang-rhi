#include "d3d12-shader-program.h"
#include "d3d12-device.h"
#include "d3d12-shader-object-layout.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rhi::d3d12 {

namespace {

bool getDiagnosticTrailingBytes(size_t& outTrailingBytes)
{
    char value[32];
    DWORD length = GetEnvironmentVariableA("RHI_DIAGNOSTIC_SHADER_TRAILING_BYTES", value, DWORD(sizeof(value)));
    if (length == 0 || length >= sizeof(value))
        return false;

    char* end = nullptr;
    unsigned long long parsed = std::strtoull(value, &end, 10);
    if (!end || *end != '\0' || parsed > 4096)
        return false;

    outTrailingBytes = size_t(parsed);
    return true;
}

uint32_t readU32(const std::vector<uint8_t>& code, size_t offset)
{
    uint32_t value = 0;
    if (offset <= code.size() && code.size() - offset >= sizeof(value))
        std::memcpy(&value, code.data() + offset, sizeof(value));
    return value;
}

void printDxilRanges(const ShaderBinary& shader)
{
    constexpr uint32_t kDxbcMagic = 0x43425844; // DXBC
    constexpr uint32_t kDxilMagic = 0x4c495844; // DXIL
    if (shader.code.size() < 32 || readU32(shader.code, 0) != kDxbcMagic)
    {
        std::fprintf(stderr, "[diagnostic] shader is not a DXBC container bytes=%zu\n", shader.code.size());
        return;
    }

    const uint32_t containerSize = readU32(shader.code, 24);
    const uint32_t partCount = readU32(shader.code, 28);
    std::fprintf(
        stderr,
        "[diagnostic] dxil-container bytes=%zu declared=%u parts=%u\n",
        shader.code.size(),
        containerSize,
        partCount
    );
    if (partCount > (shader.code.size() - 32) / 4)
        return;

    for (uint32_t i = 0; i < partCount; ++i)
    {
        const size_t partOffset = readU32(shader.code, 32 + size_t(i) * 4);
        if (partOffset > shader.code.size() || shader.code.size() - partOffset < 32 ||
            readU32(shader.code, partOffset) != kDxilMagic)
            continue;

        const uint32_t partSize = readU32(shader.code, partOffset + 4);
        const size_t programOffset = partOffset + 8;
        const uint32_t programSize = readU32(shader.code, programOffset + 4) * 4;
        const uint32_t bitcodeOffset = readU32(shader.code, programOffset + 16);
        const uint32_t bitcodeSize = readU32(shader.code, programOffset + 20);
        const size_t bitcodeBegin = programOffset + 8 + bitcodeOffset;
        const size_t bitcodeEnd = bitcodeBegin + size_t(bitcodeSize);
        std::fprintf(
            stderr,
            "[diagnostic] dxil-part index=%u part-offset=%zu part-size=%u program-offset=%zu program-size=%u "
            "bitcode-offset=%u bitcode-size=%u bitcode-begin=%zu bitcode-end=%zu blob-end=%zu\n",
            i,
            partOffset,
            partSize,
            programOffset,
            programSize,
            bitcodeOffset,
            bitcodeSize,
            bitcodeBegin,
            bitcodeEnd,
            shader.code.size()
        );
    }
}

bool createDiagnosticGuardedCopy(ShaderBinary& shader, size_t trailingBytes)
{
    SYSTEM_INFO systemInfo = {};
    GetSystemInfo(&systemInfo);
    const size_t pageSize = systemInfo.dwPageSize;
    if (!pageSize || shader.code.size() > SIZE_MAX - trailingBytes)
        return false;

    const size_t readableBytes = shader.code.size() + trailingBytes;
    const size_t committedBytes = (readableBytes + pageSize - 1) / pageSize * pageSize;
    if (committedBytes > SIZE_MAX - pageSize)
        return false;

    void* allocation = VirtualAlloc(nullptr, committedBytes + pageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!allocation)
        return false;

    uint8_t* guard = static_cast<uint8_t*>(allocation) + committedBytes;
    DWORD oldProtection = 0;
    if (!VirtualProtect(guard, pageSize, PAGE_NOACCESS, &oldProtection))
    {
        VirtualFree(allocation, 0, MEM_RELEASE);
        return false;
    }

    uint8_t* destination = guard - trailingBytes - shader.code.size();
    std::memcpy(destination, shader.code.data(), shader.code.size());
    shader.diagnosticAllocation = std::shared_ptr<void>(
        allocation,
        [](void* pointer)
        {
            VirtualFree(pointer, 0, MEM_RELEASE);
        }
    );
    shader.diagnosticCode = destination;
    std::fprintf(
        stderr,
        "[diagnostic] guarded-shader stage=%d bytes=%zu trailing=%zu begin=%p end=%p guard=%p\n",
        int(shader.stage),
        shader.code.size(),
        trailingBytes,
        destination,
        destination + shader.code.size(),
        guard
    );
    printDxilRanges(shader);
    std::fflush(stderr);
    return true;
}

} // namespace

ShaderProgramImpl::ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc)
    : ShaderProgram(device, desc)
{
}

ShaderProgramImpl::~ShaderProgramImpl()
{
#if SLANG_RHI_ENABLE_AFTERMATH
    DeviceImpl* device = getDevice<DeviceImpl>();
    if (device->m_aftermathCrashDumper)
    {
        for (const ShaderBinary& shader : m_shaders)
        {
            device->m_aftermathCrashDumper->unregisterShader(reinterpret_cast<uint64_t>(shader.code.data()));
        }
    }
#endif
}

Result ShaderProgramImpl::createShaderModule(const ShaderModuleDesc& desc, ComPtr<ISlangBlob> kernelCode)
{
    ShaderBinary shaderBin;
    shaderBin.stage = desc.stage;
    shaderBin.entryPointName = desc.entryPointName;
    shaderBin.code.assign(
        reinterpret_cast<const uint8_t*>(kernelCode->getBufferPointer()),
        reinterpret_cast<const uint8_t*>(kernelCode->getBufferPointer()) + (size_t)kernelCode->getBufferSize()
    );

    size_t diagnosticTrailingBytes = 0;
    if (getDiagnosticTrailingBytes(diagnosticTrailingBytes) &&
        !createDiagnosticGuardedCopy(shaderBin, diagnosticTrailingBytes))
    {
        std::fprintf(
            stderr,
            "[diagnostic] failed to create guarded shader copy: error=%lu bytes=%zu trailing=%zu\n",
            GetLastError(),
            shaderBin.code.size(),
            diagnosticTrailingBytes
        );
        return SLANG_FAIL;
    }

#if SLANG_RHI_ENABLE_AFTERMATH
    DeviceImpl* device = getDevice<DeviceImpl>();
    if (device->m_aftermathCrashDumper)
    {
        device->m_aftermathCrashDumper->registerShader(
            reinterpret_cast<uint64_t>(shaderBin.code.data()),
            DeviceType::D3D12,
            shaderBin.code.data(),
            shaderBin.code.size()
        );
    }
#endif

    m_shaders.push_back(_Move(shaderBin));
    return SLANG_OK;
}

ShaderObjectLayout* ShaderProgramImpl::getRootShaderObjectLayout()
{
    return m_rootObjectLayout;
}

} // namespace rhi::d3d12
