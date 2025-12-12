#pragma once

#include <slang-rhi.h>

#if SLANG_RHI_ENABLE_AFTERMATH

#include "core/smart-pointer.h"

#include <GFSDK_Aftermath.h>

#include <array>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rhi {

/// Helper class for keeping track of debug markers.
/// Retains a fixed number of markers for later lookup by hash.
/// Marker names are stored in a stack-like manner to allow for nested markers.
class AftermathMarkerTracker
{
public:
    /// Push a new debug group. Returns the hash for recording with Aftermath.
    uint64_t pushGroup(const char* name);
    /// Pop the last debug group.
    void popGroup();
    /// Find a marker by its hash.
    const std::string* findMarker(uint64_t hash);

private:
    struct MarkerName
    {
        std::string fullName;
        std::vector<size_t> sizeStack;
        void push(const char* name)
        {
            sizeStack.push_back(fullName.size());
            if (!fullName.empty())
                fullName += '/';
            fullName += name;
        }
        void pop()
        {
            if (sizeStack.empty())
                return;
            fullName.resize(sizeStack.back());
            sizeStack.pop_back();
        }
    };

    struct MarkerEntry
    {
        std::string name;
        uint64_t hash;
    };

    MarkerName m_markerName;
    std::array<MarkerEntry, 16> m_entries;
    size_t m_nextEntryIndex = 0;
};

/// Helper class for managing Aftermath crash dumps.
/// Allows registering shader blobs and marker trackers for resolving information in crash dumps.
class AftermathCrashDumper : public RefObject
{
public:
    struct Shader
    {
        DeviceType deviceType;
        const void* code;
        size_t codeSize;
        uint64_t hash;
    };

    AftermathCrashDumper();
    ~AftermathCrashDumper();

    /// Get the directory where dumps are stored.
    const std::string& getDumpDir() const { return m_dumpDir; }

    /// Register a shader for potential inclusion in crash dumps.
    void registerShader(uint64_t id, DeviceType deviceType, const void* code, size_t codeSize);
    /// Unregister a previously registered shader.
    void unregisterShader(uint64_t id);
    /// Find a shader by hash.
    Shader* findShader(uint64_t hash);

    /// Register a marker tracker for resolving markers in crash dumps.
    void registerMarkerTracker(AftermathMarkerTracker* tracker);
    /// Unregister a previously registered marker tracker.
    void unregisterMarkerTracker(AftermathMarkerTracker* tracker);
    /// Find a marker by its hash across all registered trackers.
    const std::string* findMarker(uint64_t hash);

    /// Get or create the global AftermathCrashDumper instance.
    static AftermathCrashDumper* getOrCreate();
    /// Wait for a crash dump to be written. Should be called after a crash is triggered.
    static void waitForDump(int timeoutSeconds = 10);

private:
    std::string m_dumpDir;

    std::mutex m_shadersMutex;
    std::unordered_map<uint64_t, Shader> m_shaders;

    std::mutex m_markerTrackersMutex;
    std::unordered_set<AftermathMarkerTracker*> m_markerTrackers;
};

} // namespace rhi

#endif // SLANG_RHI_ENABLE_AFTERMATH
