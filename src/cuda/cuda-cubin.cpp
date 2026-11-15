#include "cuda-cubin.h"
#include "cuda-device.h"
#include "cuda-utils.h"
#include "core/deferred.h"
#include "core/sha1.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#if SLANG_RHI_HAS_NVPTXCOMPILER
#include <nvPTXCompiler.h>
#endif

namespace rhi::cuda {

namespace {
std::string getEnvironment(const char* name)
{
#if SLANG_WINDOWS_FAMILY
    char* value = nullptr;
    size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0)
        return {};
    std::string result = value ? value : "";
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

#if SLANG_RHI_HAS_NVPTXCOMPILER
constexpr char kCacheMagic[] = "RHICUB01";
constexpr size_t kHeaderSize = 8 + 20;

ComPtr<ISlangBlob> decodeCache(ISlangBlob* value)
{
    if (!value || value->getBufferSize() < kHeaderSize + 64)
        return nullptr;
    const auto* data = static_cast<const uint8_t*>(value->getBufferPointer());
    if (std::memcmp(data, kCacheMagic, 8) != 0 || std::memcmp(
                                                      data + kHeaderSize,
                                                      "\x7f"
                                                      "ELF",
                                                      4
                                                  ) != 0)
        return nullptr;
    const auto digest = SHA1(data + kHeaderSize, value->getBufferSize() - kHeaderSize).getDigest();
    if (std::memcmp(data + 8, digest.data(), digest.size()) != 0)
        return nullptr;
    return OwnedBlob::create(data + kHeaderSize, value->getBufferSize() - kHeaderSize);
}

void hashString(SHA1& hash, std::string_view value)
{
    hash.update(uint64_t(value.size()));
    hash.update(value);
}
#endif
} // namespace

bool CubinCompiler::isRequested()
{
    return getEnvironment("SLANG_RHI_CUDA_CUBIN") == "1";
}

Result CubinCompiler::initialize(DeviceImpl* device)
{
    m_device = device;
#if SLANG_RHI_HAS_NVPTXCOMPILER
    if (nvPTXCompilerGetVersion(&m_major, &m_minor) != NVPTXCOMPILE_SUCCESS)
        return SLANG_FAIL;
    int major = 0, minor = 0;
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(
        cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device->m_ctx.device),
        device
    );
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(
        cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device->m_ctx.device),
        device
    );
    m_archOption = "--gpu-name=sm_" + std::to_string(major) + std::to_string(minor);
    const auto level = getEnvironment("SLANG_RHI_CUDA_CUBIN_OPT_LEVEL");
    if (!level.empty() && (level.size() != 1 || level[0] < '0' || level[0] > '3'))
    {
        device->printError("SLANG_RHI_CUDA_CUBIN_OPT_LEVEL must be 0, 1, 2, or 3.");
        return SLANG_E_INVALID_ARG;
    }
    m_optimizationOption = "--opt-level=" + (level.empty() ? std::string("3") : level);
    m_trace = getEnvironment("SLANG_RHI_CUDA_CUBIN_TRACE") == "1";
    return SLANG_OK;
#else
    device->printError("CUBIN compilation requested, but this build has no SLANG_RHI_NVPTXCOMPILER_ROOT.");
    return SLANG_E_NOT_AVAILABLE;
#endif
}

Result CubinCompiler::compile(ISlangBlob* ptx, ComPtr<ISlangBlob>& cubin, std::string& diagnostics)
{
#if SLANG_RHI_HAS_NVPTXCOMPILER
    nvPTXCompilerHandle compiler = nullptr;
    auto result =
        nvPTXCompilerCreate(&compiler, ptx->getBufferSize(), static_cast<const char*>(ptx->getBufferPointer()));
    if (result != NVPTXCOMPILE_SUCCESS)
    {
        diagnostics += "nvPTXCompilerCreate failed: " + std::to_string(result);
        return SLANG_FAIL;
    }
    SLANG_RHI_DEFERRED({ nvPTXCompilerDestroy(&compiler); });
    const char* options[] = {m_archOption.c_str(), m_optimizationOption.c_str()};
    result = nvPTXCompilerCompile(compiler, 2, options);
    size_t logSize = 0;
    if (nvPTXCompilerGetErrorLogSize(compiler, &logSize) == NVPTXCOMPILE_SUCCESS && logSize)
    {
        std::vector<char> log(logSize + 1, 0);
        if (nvPTXCompilerGetErrorLog(compiler, log.data()) == NVPTXCOMPILE_SUCCESS)
            diagnostics.append(log.data());
    }
    if (result != NVPTXCOMPILE_SUCCESS)
    {
        diagnostics += "\nnvPTXCompilerCompile failed: " + std::to_string(result);
        return SLANG_FAIL;
    }
    logSize = 0;
    if (nvPTXCompilerGetInfoLogSize(compiler, &logSize) == NVPTXCOMPILE_SUCCESS && logSize)
    {
        std::vector<char> log(logSize + 1, 0);
        if (nvPTXCompilerGetInfoLog(compiler, log.data()) == NVPTXCOMPILE_SUCCESS)
            diagnostics.append(log.data());
    }
    size_t size = 0;
    if (nvPTXCompilerGetCompiledProgramSize(compiler, &size) != NVPTXCOMPILE_SUCCESS || !size)
    {
        diagnostics += "\nnvPTXCompilerGetCompiledProgramSize failed.";
        return SLANG_FAIL;
    }
    std::vector<uint8_t> bytes(size);
    if (nvPTXCompilerGetCompiledProgram(compiler, bytes.data()) != NVPTXCOMPILE_SUCCESS)
    {
        diagnostics += "\nnvPTXCompilerGetCompiledProgram failed.";
        return SLANG_FAIL;
    }
    cubin = OwnedBlob::create(bytes.data(), bytes.size());
    return SLANG_OK;
#else
    return SLANG_E_NOT_AVAILABLE;
#endif
}

void CubinCompiler::store(ISlangBlob* key, ISlangBlob* cubin)
{
#if SLANG_RHI_HAS_NVPTXCOMPILER
    if (!m_device->m_persistentPipelineCache)
        return;
    const auto digest = SHA1(cubin->getBufferPointer(), cubin->getBufferSize()).getDigest();
    std::vector<uint8_t> value(kHeaderSize + cubin->getBufferSize());
    std::memcpy(value.data(), kCacheMagic, 8);
    std::memcpy(value.data() + 8, digest.data(), digest.size());
    std::memcpy(value.data() + kHeaderSize, cubin->getBufferPointer(), cubin->getBufferSize());
    auto blob = OwnedBlob::create(value.data(), value.size());
    m_device->m_persistentPipelineCache->writeCache(key, blob);
#endif
}

Result CubinCompiler::rebuild(
    ISlangBlob* ptx,
    ISlangBlob* cacheKey,
    ComPtr<ISlangBlob>& cubin,
    std::string& diagnostics
)
{
    SLANG_RETURN_ON_FAIL(compile(ptx, cubin, diagnostics));
    store(cacheKey, cubin);
    return SLANG_OK;
}

Result CubinCompiler::convert(ShaderProgram* program, CompiledEntryPoint& entryPoint)
{
#if SLANG_RHI_HAS_NVPTXCOMPILER
    const TimePoint start = Timer::now();
    ComPtr<ISlangBlob> artifactOwner(new CubinArtifact());
    auto artifact = static_cast<CubinArtifact*>(artifactOwner.get());
    artifact->ptx = entryPoint.code;
    SHA1 hash;
    hashString(hash, "rhi-cuda-cubin-v1");
    hashString(hash, SLANG_RHI_NVPTXCOMPILER_HASH);
    hashString(hash, m_archOption);
    hashString(hash, m_optimizationOption);
    hash.update(artifact->ptx->getBufferPointer(), artifact->ptx->getBufferSize());
    const auto digest = hash.getDigest();
    artifact->cacheKey = OwnedBlob::create(digest.data(), digest.size());
    if (m_device->m_persistentPipelineCache)
    {
        ComPtr<ISlangBlob> cached;
        if (SLANG_SUCCEEDED(m_device->m_persistentPipelineCache->queryCache(artifact->cacheKey, cached.writeRef())))
            artifact->cubin = decodeCache(cached);
    }
    artifact->cacheHit = bool(artifact->cubin);
    Result result = SLANG_OK;
    if (!artifact->cacheHit)
    {
        std::string diagnostics;
        result = rebuild(artifact->ptx, artifact->cacheKey, artifact->cubin, diagnostics);
        if (!diagnostics.empty())
        {
            if (entryPoint.diagnostics)
                diagnostics.insert(0, static_cast<const char*>(entryPoint.diagnostics->getBufferPointer()));
            entryPoint.diagnostics = OwnedBlob::create(diagnostics.c_str(), diagnostics.size() + 1);
        }
    }
    const TimePoint end = Timer::now();
    if (m_trace)
    {
        static std::mutex traceMutex;
        std::lock_guard lock(traceMutex);
        std::fprintf(
            stderr,
            "CUBIN_TRACE={\"program_id\":%llu,\"start_ns\":%llu,\"end_ns\":%llu,\"cache_hit\":%s,\"success\":%s,"
            "\"compiler\":\"%u.%u\",\"fingerprint\":\"%s\",\"arch\":\"%s\",\"optimization\":\"%s\"}\n",
            (unsigned long long)program->m_id,
            (unsigned long long)start,
            (unsigned long long)end,
            artifact->cacheHit ? "true" : "false",
            SLANG_SUCCEEDED(result) ? "true" : "false",
            m_major,
            m_minor,
            SLANG_RHI_NVPTXCOMPILER_HASH,
            m_archOption.c_str(),
            m_optimizationOption.c_str()
        );
    }
    if (SLANG_SUCCEEDED(result))
        entryPoint.code = artifactOwner;
    return result;
#else
    return SLANG_E_NOT_AVAILABLE;
#endif
}

} // namespace rhi::cuda
