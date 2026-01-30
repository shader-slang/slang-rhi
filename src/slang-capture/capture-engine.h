#pragma once

#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <chrono>

namespace slang_capture {

/// Operating mode for the capture system.
enum class CaptureMode
{
    Disabled, ///< No capture/replay active
    Capture,  ///< Record API calls to file
    Replay,   ///< Playback from file (not yet implemented)
    SyncTest  ///< Compare against previous capture (not yet implemented)
};

/// Main engine for capturing Slang API calls.
/// Singleton that manages recording, object tracking, and file output.
class CaptureEngine
{
public:
    /// Get the singleton instance.
    static CaptureEngine& instance();

    // Non-copyable
    CaptureEngine(const CaptureEngine&) = delete;
    CaptureEngine& operator=(const CaptureEngine&) = delete;

    /// Set the operating mode.
    void setMode(CaptureMode mode);

    /// Get the current operating mode.
    CaptureMode getMode() const { return m_mode; }

    /// Set the output file path for capture mode.
    void setOutputPath(const std::string& path);

    /// Set the reference file path for sync test mode.
    void setReferencePath(const std::string& path);

    /// Initialize from environment variables.
    /// Reads SLANG_CAPTURE_MODE, SLANG_CAPTURE_FILE, SLANG_CAPTURE_REF.
    void initFromEnvironment();

    /// Begin recording a new API call.
    /// @param iface Interface name (e.g., "IGlobalSession")
    /// @param method Method name (e.g., "createSession")
    /// @param objectId Object ID being called on (0 for free functions)
    /// @return Call ID to use with addArg/endCall
    uint64_t beginCall(const char* iface, const char* method, uint64_t objectId);

    /// Add an argument to a call being recorded.
    /// @param callId Call ID from beginCall
    /// @param name Argument name
    /// @param jsonValue JSON-formatted value
    void addArg(uint64_t callId, const char* name, const std::string& jsonValue);

    /// End a call and write it to the output.
    /// @param callId Call ID from beginCall
    /// @param result Result string (e.g., "SLANG_OK", "SLANG_FAIL", or a value)
    /// @param outParamsJson JSON object for output parameters (optional)
    void endCall(uint64_t callId, const char* result, const std::string& outParamsJson = "{}");

    /// Register an object and get its ID.
    /// @param ptr Object pointer
    /// @param type Type name for logging
    /// @return Unique object ID
    uint64_t registerObject(void* ptr, const char* type);

    /// Look up an object by ID.
    /// @param id Object ID
    /// @return Object pointer, or nullptr if not found
    void* getObject(uint64_t id) const;

    /// Release an object ID.
    /// @param id Object ID to release
    void releaseObject(uint64_t id);

    /// Check if capture is active (mode is Capture).
    bool isCapturing() const { return m_mode == CaptureMode::Capture; }

    /// Verify sync match (for SyncTest mode).
    /// @return true if all calls matched, false if mismatch detected
    bool verifySyncMatch() const { return !m_syncMismatch; }

    /// Get total number of calls recorded.
    uint64_t getCallCount() const { return m_nextSeq.load() - 1; }

    /// Flush output file.
    void flush();

    /// Close and finalize capture.
    void close();

private:
    CaptureEngine();
    ~CaptureEngine();

    /// Internal call state tracking
    struct CallState
    {
        uint64_t seq;
        uint64_t objectId;
        std::string iface;
        std::string method;
        std::string args;
        std::chrono::steady_clock::time_point startTime;
        uint32_t threadId;
    };

    void writeCall(const CallState& call, const char* result, const std::string& outParamsJson);
    uint32_t getCurrentThreadId() const;

    CaptureMode m_mode = CaptureMode::Disabled;
    std::string m_outputPath;
    std::string m_referencePath;
    std::ofstream m_output;
    std::chrono::steady_clock::time_point m_startTime;

    mutable std::mutex m_mutex;
    std::atomic<uint64_t> m_nextSeq{1};
    std::atomic<uint64_t> m_nextObjectId{1};

    std::unordered_map<void*, uint64_t> m_ptrToId;
    std::unordered_map<uint64_t, void*> m_idToPtr;
    std::unordered_map<uint64_t, std::string> m_idToType;

    // Active calls (keyed by call ID)
    std::unordered_map<uint64_t, CallState> m_activeCalls;

    // Sync test state
    std::ifstream m_reference;
    bool m_syncMismatch = false;
};

} // namespace slang_capture
