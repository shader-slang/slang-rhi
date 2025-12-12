#include "aftermath.h"

#if SLANG_RHI_ENABLE_AFTERMATH

#if SLANG_RHI_ENABLE_D3D12
#include "d3d12/d3d12-api.h"
#endif
#if SLANG_RHI_ENABLE_VULKAN
#include "vulkan/vk-api.h"
#endif

#include <GFSDK_Aftermath_GpuCrashDump.h>
#include <GFSDK_Aftermath_GpuCrashDumpDecoding.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace rhi {

uint64_t AftermathMarkerTracker::pushGroup(const char* name)
{
    m_markerName.push(name);
    uint64_t hash = std::hash<std::string>()(m_markerName.fullName);
    MarkerEntry& entry = m_entries[m_nextEntryIndex];
    entry.name = m_markerName.fullName;
    entry.hash = hash;
    m_nextEntryIndex = (m_nextEntryIndex + 1) % m_entries.size();
    return hash;
}

void AftermathMarkerTracker::popGroup()
{
    m_markerName.pop();
}

const std::string* AftermathMarkerTracker::findMarker(uint64_t hash)
{
    for (const auto& entry : m_entries)
    {
        if (entry.hash == hash)
        {
            return &entry.name;
        }
    }
    return nullptr;
}


static void writeFile(const std::filesystem::path& path, const void* data, size_t size)
{
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data), size);
    file.close();
}

static void gpuCrashDumpCallback(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize, void* pUserData)
{
    AftermathCrashDumper* dumper = reinterpret_cast<AftermathCrashDumper*>(pUserData);
    std::filesystem::path dumpDir = dumper->getDumpDir();
    std::filesystem::create_directory(dumpDir);
    std::filesystem::path path = dumpDir / "crash.nv-gpudmp";
    writeFile(path, pGpuCrashDump, gpuCrashDumpSize);
    fprintf(stdout, "Aftermath crash dump written to %s\n", path.string().c_str());

    GFSDK_Aftermath_GpuCrashDump_Decoder decoder = {};
    GFSDK_Aftermath_Result result = GFSDK_Aftermath_GpuCrashDump_CreateDecoder(
        GFSDK_Aftermath_Version_API,
        pGpuCrashDump,
        gpuCrashDumpSize,
        &decoder
    );
    if (!GFSDK_Aftermath_SUCCEED(result))
    {
        fprintf(stderr, "Aftermath crash dump decoder failed create with error 0x%.8x\n", result);
        return;
    }
    uint32_t numActiveShaders = 0;
    GFSDK_Aftermath_GpuCrashDump_GetActiveShadersInfoCount(decoder, &numActiveShaders);
    if (numActiveShaders > 0)
    {
        std::vector<GFSDK_Aftermath_GpuCrashDump_ShaderInfo> shaderInfos{numActiveShaders};
        GFSDK_Aftermath_GpuCrashDump_GetActiveShadersInfo(decoder, numActiveShaders, shaderInfos.data());
        for (auto& shaderInfo : shaderInfos)
        {
            if (shaderInfo.isInternal)
                continue;

            GFSDK_Aftermath_ShaderBinaryHash shaderHash = {};
            GFSDK_Aftermath_GetShaderHashForShaderInfo(decoder, &shaderInfo, &shaderHash);
            AftermathCrashDumper::Shader* shader = dumper->findShader(shaderHash.hash);
            if (shader)
            {
                char name[128];
                snprintf(name, sizeof(name), "%08x.bin", (uint32_t)shaderHash.hash);
                std::filesystem::path shaderPath = dumpDir / name;
                writeFile(shaderPath, shader->code, shader->codeSize);
            }
        }
    }
    GFSDK_Aftermath_GpuCrashDump_DestroyDecoder(decoder);
}

static void shaderDebugInfoCallback(const void* pShaderDebugInfo, const uint32_t shaderDebugInfoSize, void* pUserData)
{
    AftermathCrashDumper* dumper = reinterpret_cast<AftermathCrashDumper*>(pUserData);
    std::filesystem::path dumpDir = dumper->getDumpDir();
    std::filesystem::create_directory(dumpDir);

    // Get shader debug information identifier.
    GFSDK_Aftermath_ShaderDebugInfoIdentifier identifier = {};
    if (!GFSDK_Aftermath_SUCCEED(GFSDK_Aftermath_GetShaderDebugInfoIdentifier(
            GFSDK_Aftermath_Version_API,
            pShaderDebugInfo,
            shaderDebugInfoSize,
            &identifier
        )))
    {
        return;
    }

    char name[128];
    snprintf(name, sizeof(name), "%08x-%08x.nvdbg", (uint32_t)identifier.id[0], (uint32_t)identifier.id[1]);
    std::filesystem::path path = dumpDir / name;
    writeFile(path, pShaderDebugInfo, shaderDebugInfoSize);
}

static void descriptionCallback(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription, void* pUserData)
{
    // AftermathCrashDumper* dumper = reinterpret_cast<AftermathCrashDumper*>(pUserData);
    // addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName, "slang-rhi");
}

// this callback should call into the nvrhi device which has the necessary information
static void resolveMarkerCallback(
    const void* pMarkerData,
    const uint32_t markerDataSize,
    void* pUserData,
    PFN_GFSDK_Aftermath_ResolveMarker resolveMarker
)
{
    AftermathCrashDumper* dumper = reinterpret_cast<AftermathCrashDumper*>(pUserData);
    const uint64_t hash = reinterpret_cast<const uint64_t>(pMarkerData);
    const std::string* marker = dumper->findMarker(hash);
    if (marker)
    {
        resolveMarker(marker->c_str(), marker->size());
    }
}

AftermathCrashDumper::AftermathCrashDumper()
{
    uint32_t watchedApis = GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_None;
#if SLANG_RHI_ENABLE_D3D11 || SLANG_RHI_ENABLE_D3D12
    watchedApis |= GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_DX;
#endif
#if SLANG_RHI_ENABLE_VULKAN
    watchedApis |= GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_Vulkan;
#endif
    uint32_t featureFlags = GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks;
    GFSDK_Aftermath_Result result = GFSDK_Aftermath_EnableGpuCrashDumps(
        GFSDK_Aftermath_Version_API,
        watchedApis,
        featureFlags,
        gpuCrashDumpCallback,
        shaderDebugInfoCallback,
        descriptionCallback,
        resolveMarkerCallback,
        this
    );
    if (!GFSDK_Aftermath_SUCCEED(result))
    {
        fprintf(stderr, "Aftermath crash dump enable failed with error 0x%.8x\n", result);
    }

    time_t now;
    time(&now);
    struct tm tm;
#if SLANG_WINDOWS_FAMILY
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char name[128];
    std::strftime(name, sizeof(name), "crash-%Y-%m-%d-%H-%M-%S", &tm);
    m_dumpDir = (std::filesystem::current_path() / name).string();
}

AftermathCrashDumper::~AftermathCrashDumper()
{
    GFSDK_Aftermath_DisableGpuCrashDumps();
}

void AftermathCrashDumper::registerShader(uint64_t id, DeviceType deviceType, const void* code, size_t codeSize)
{
    std::lock_guard lock(m_shadersMutex);
    m_shaders[id] = Shader{deviceType, code, codeSize, 0};
}

void AftermathCrashDumper::unregisterShader(uint64_t id)
{
    std::lock_guard lock(m_shadersMutex);
    m_shaders.erase(id);
}

AftermathCrashDumper::Shader* AftermathCrashDumper::findShader(uint64_t hash)
{
    std::lock_guard lock(m_shadersMutex);
    for (auto& [id, shader] : m_shaders)
    {
        if (shader.hash == 0)
        {
#if SLANG_RHI_ENABLE_D3D12
            if (shader.deviceType == DeviceType::D3D11 || shader.deviceType == DeviceType::D3D12)
            {
                D3D12_SHADER_BYTECODE dxil = {};
                dxil.pShaderBytecode = shader.code;
                dxil.BytecodeLength = shader.codeSize;
                GFSDK_Aftermath_ShaderBinaryHash shaderHash = {};
                GFSDK_Aftermath_GetShaderHash(GFSDK_Aftermath_Version_API, &dxil, &shaderHash);
                shader.hash = shaderHash.hash;
            }
#endif
#if SLANG_RHI_ENABLE_VULKAN
            if (shader.deviceType == DeviceType::Vulkan)
            {
                GFSDK_Aftermath_SpirvCode spirv = {};
                spirv.pData = shader.code;
                spirv.size = shader.codeSize;
                GFSDK_Aftermath_ShaderBinaryHash shaderHash = {};
                GFSDK_Aftermath_GetShaderHashSpirv(GFSDK_Aftermath_Version_API, &spirv, &shaderHash);
                shader.hash = shaderHash.hash;
            }
        }
#endif
        if (shader.hash == hash)
        {
            return &shader;
        }
    }
    return nullptr;
}

void AftermathCrashDumper::registerMarkerTracker(AftermathMarkerTracker* tracker)
{
    std::lock_guard lock(m_markerTrackersMutex);
    m_markerTrackers.insert(tracker);
}

void AftermathCrashDumper::unregisterMarkerTracker(AftermathMarkerTracker* tracker)
{
    std::lock_guard lock(m_markerTrackersMutex);
    m_markerTrackers.erase(tracker);
}

const std::string* AftermathCrashDumper::findMarker(uint64_t hash)
{
    std::lock_guard lock(m_markerTrackersMutex);
    for (auto* tracker : m_markerTrackers)
    {
        if (const std::string* marker = tracker->findMarker(hash))
        {
            return marker;
        }
    }
    return nullptr;
}

AftermathCrashDumper* AftermathCrashDumper::getOrCreate()
{
    static RefPtr<AftermathCrashDumper> instance = new AftermathCrashDumper();
    return instance;
}

void AftermathCrashDumper::waitForDump(int timeoutSeconds)
{
    std::chrono::time_point startTime = std::chrono::system_clock::now();
    bool timedOut = false;
    while (!timedOut)
    {
        GFSDK_Aftermath_CrashDump_Status status = GFSDK_Aftermath_CrashDump_Status_Unknown;
        GFSDK_Aftermath_GetCrashDumpStatus(&status);
        if (status == GFSDK_Aftermath_CrashDump_Status_NotStarted)
        {
            break;
        }
        if (status == GFSDK_Aftermath_CrashDump_Status_Finished)
        {
            break;
        }
        auto elapsedSeconds =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - startTime).count();
        timedOut = elapsedSeconds > timeoutSeconds;
    }
}

} // namespace rhi

#endif // SLANG_RHI_ENABLE_AFTERMATH
