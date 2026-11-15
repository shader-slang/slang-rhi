#include "cuda-cubin.h"
#include "cuda-device.h"
#include "cuda-utils.h"
#include "core/deferred.h"
#include "core/sha1.h"
#include "core/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <fstream>
#include <filesystem>

#if SLANG_RHI_HAS_NVPTXCOMPILER
#include <nvPTXCompiler.h>
#endif
#if SLANG_RHI_HAS_NVJITLINK
#include <nvJitLink.h>
#if SLANG_WINDOWS_FAMILY
#include <windows.h>
#endif
#endif

namespace rhi::cuda {

namespace {
#if SLANG_RHI_HAS_NVJITLINK
Result getJitLinkFingerprint(std::string& fingerprint)
{
    // Hash the binary actually loaded, so replacing a compatible shared library
    // cannot accidentally reuse artifacts produced by the previous compiler.
    std::filesystem::path path;
#if SLANG_WINDOWS_FAMILY
    auto module = GetModuleHandleW(L"nvJitLink_120_0.dll");
    if (!module)
        return SLANG_FAIL;
    std::vector<wchar_t> name(32768);
    DWORD size = GetModuleFileNameW(module, name.data(), DWORD(name.size()));
    if (!size || size >= name.size())
        return SLANG_FAIL;
    path = std::wstring(name.data(), size);
#else
    const char* name = findSharedLibraryPath(reinterpret_cast<void*>(&nvJitLinkVersion));
    if (!name)
        return SLANG_FAIL;
    path = name;
#endif
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return SLANG_FAIL;
    SHA1 hash;
    std::array<char, 64 * 1024> buffer;
    while (stream)
    {
        stream.read(buffer.data(), buffer.size());
        hash.update(buffer.data(), size_t(stream.gcount()));
    }
    if (!stream.eof())
        return SLANG_FAIL;
    fingerprint = hash.getHexDigest();
    return SLANG_OK;
}
#endif

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
} // namespace

bool CubinCompiler::isRequested()
{
    return getEnvironment("SLANG_RHI_CUDA_CUBIN") == "1";
}

Result CubinCompiler::initialize(DeviceImpl* device)
{
    m_device = device;
    m_compilerName = getEnvironment("SLANG_RHI_CUDA_CUBIN_COMPILER");
    if (m_compilerName.empty())
        m_compilerName = "nvptxcompiler";
    if (m_compilerName == "nvjitlink")
    {
#if SLANG_RHI_HAS_NVJITLINK
        if (nvJitLinkVersion(&m_major, &m_minor) != NVJITLINK_SUCCESS)
            return SLANG_FAIL;
        if (SLANG_FAILED(getJitLinkFingerprint(m_fingerprint)))
        {
            device->printError("Could not fingerprint the loaded nvJitLink library.");
            return SLANG_FAIL;
        }
#else
        device->printError("nvJitLink requested, but this build has no SLANG_RHI_NVJITLINK_ROOT.");
        return SLANG_E_NOT_AVAILABLE;
#endif
    }
    else if (m_compilerName == "nvptxcompiler")
    {
#if SLANG_RHI_HAS_NVPTXCOMPILER
        if (nvPTXCompilerGetVersion(&m_major, &m_minor) != NVPTXCOMPILE_SUCCESS)
            return SLANG_FAIL;
        m_fingerprint = SLANG_RHI_NVPTXCOMPILER_HASH;
#else
        device->printError("nvPTXCompiler requested, but this build has no SLANG_RHI_NVPTXCOMPILER_ROOT.");
        return SLANG_E_NOT_AVAILABLE;
#endif
    }
    else
    {
        device->printError("SLANG_RHI_CUDA_CUBIN_COMPILER must be nvptxcompiler or nvjitlink.");
        return SLANG_E_INVALID_ARG;
    }
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
    if (m_compilerName == "nvjitlink")
    {
        m_archOption = "-arch=sm_" + std::to_string(major) + std::to_string(minor);
        m_optimizationOption = "-O" + (level.empty() ? std::string("3") : level);
        m_options = {m_archOption, m_optimizationOption, "-no-cache", "-split-compile=1"};
    }
    else
        m_options = {m_archOption, m_optimizationOption};
    m_trace = getEnvironment("SLANG_RHI_CUDA_CUBIN_TRACE") == "1";
    return SLANG_OK;
}

Result CubinCompiler::compile(ISlangBlob* ptx, ComPtr<ISlangBlob>& cubin, std::string& diagnostics)
{
    if (m_compilerName == "nvjitlink")
        return compileWithJitLink(ptx, cubin, diagnostics);
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

Result CubinCompiler::compileWithJitLink(ISlangBlob* ptx, ComPtr<ISlangBlob>& cubin, std::string& diagnostics)
{
#if SLANG_RHI_HAS_NVJITLINK
    std::vector<const char*> options;
    for (const auto& option : m_options)
        options.push_back(option.c_str());
    nvJitLinkHandle linker = nullptr;
    auto result = nvJitLinkCreate(&linker, uint32_t(options.size()), options.data());
    SLANG_RHI_DEFERRED({
        if (linker)
            nvJitLinkDestroy(&linker);
    });
    const char* operation = "nvJitLinkCreate";
    if (result == NVJITLINK_SUCCESS)
    {
        // Supply a terminator even when Slang's PTX blob is length-delimited.
        std::string source(static_cast<const char*>(ptx->getBufferPointer()), ptx->getBufferSize());
        operation = "nvJitLinkAddData";
        result = nvJitLinkAddData(linker, NVJITLINK_INPUT_PTX, source.c_str(), source.size() + 1, "kernel.ptx");
        if (result == NVJITLINK_SUCCESS)
        {
            operation = "nvJitLinkComplete";
            result = nvJitLinkComplete(linker);
        }
    }
    if (linker)
    {
        size_t size = 0;
        if (nvJitLinkGetErrorLogSize(linker, &size) == NVJITLINK_SUCCESS && size)
        {
            std::vector<char> log(size + 1, 0);
            if (nvJitLinkGetErrorLog(linker, log.data()) == NVJITLINK_SUCCESS)
                diagnostics += log.data();
        }
        size = 0;
        if (nvJitLinkGetInfoLogSize(linker, &size) == NVJITLINK_SUCCESS && size)
        {
            std::vector<char> log(size + 1, 0);
            if (nvJitLinkGetInfoLog(linker, log.data()) == NVJITLINK_SUCCESS)
                diagnostics += log.data();
        }
    }
    if (result != NVJITLINK_SUCCESS)
    {
        diagnostics += std::string("\n") + operation + " failed: " + std::to_string(result);
        return SLANG_FAIL;
    }
    size_t size = 0;
    if (nvJitLinkGetLinkedCubinSize(linker, &size) != NVJITLINK_SUCCESS || !size)
    {
        diagnostics += "\nnvJitLinkGetLinkedCubinSize failed.";
        return SLANG_FAIL;
    }
    std::vector<uint8_t> bytes(size);
    if (nvJitLinkGetLinkedCubin(linker, bytes.data()) != NVJITLINK_SUCCESS)
    {
        diagnostics += "\nnvJitLinkGetLinkedCubin failed.";
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
    if (!m_device->m_persistentPipelineCache)
        return;
    const auto digest = SHA1(cubin->getBufferPointer(), cubin->getBufferSize()).getDigest();
    std::vector<uint8_t> value(kHeaderSize + cubin->getBufferSize());
    std::memcpy(value.data(), kCacheMagic, 8);
    std::memcpy(value.data() + 8, digest.data(), digest.size());
    std::memcpy(value.data() + kHeaderSize, cubin->getBufferPointer(), cubin->getBufferSize());
    auto blob = OwnedBlob::create(value.data(), value.size());
    m_device->m_persistentPipelineCache->writeCache(key, blob);
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
    const TimePoint start = Timer::now();
    ComPtr<ISlangBlob> artifactOwner(new CubinArtifact());
    auto artifact = static_cast<CubinArtifact*>(artifactOwner.get());
    artifact->ptx = entryPoint.code;
    SHA1 hash;
    hashString(hash, "rhi-cuda-cubin-v2");
    hashString(hash, m_compilerName);
    hashString(hash, m_fingerprint);
    for (const auto& option : m_options)
        hashString(hash, option);
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
            "\"compiler\":\"%u.%u\",\"backend\":\"%s\",\"intermediate_cache_disabled\":true,"
            "\"fingerprint\":\"%s\",\"arch\":\"%s\",\"optimization\":\"%s\"}\n",
            (unsigned long long)program->m_id,
            (unsigned long long)start,
            (unsigned long long)end,
            artifact->cacheHit ? "true" : "false",
            SLANG_SUCCEEDED(result) ? "true" : "false",
            m_major,
            m_minor,
            m_compilerName.c_str(),
            m_fingerprint.c_str(),
            m_archOption.c_str(),
            m_optimizationOption.c_str()
        );
    }
    if (SLANG_SUCCEEDED(result))
        entryPoint.code = artifactOwner;
    return result;
}

} // namespace rhi::cuda
