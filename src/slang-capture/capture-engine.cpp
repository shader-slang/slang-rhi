#include "capture-engine.h"

#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <sys/syscall.h>
#endif

namespace slang_capture {

namespace {

// Cross-platform safe environment variable getter
std::string getEnvVar(const char* name)
{
#if defined(_WIN32)
    char* buffer = nullptr;
    size_t size = 0;
    errno_t err = _dupenv_s(&buffer, &size, name);
    if (err == 0 && buffer != nullptr)
    {
        std::string result(buffer);
        free(buffer);
        return result;
    }
    return "";
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : "";
#endif
}

} // anonymous namespace

CaptureEngine& CaptureEngine::instance()
{
    static CaptureEngine s_instance;
    return s_instance;
}

CaptureEngine::CaptureEngine()
    : m_startTime(std::chrono::steady_clock::now())
{
}

CaptureEngine::~CaptureEngine()
{
    close();
}

void CaptureEngine::setMode(CaptureMode mode)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mode = mode;
}

void CaptureEngine::setOutputPath(const std::string& path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_outputPath = path;

    // Close existing output if any
    if (m_output.is_open())
    {
        m_output.close();
    }

    // Open new output file
    if (!path.empty() && m_mode == CaptureMode::Capture)
    {
        m_output.open(path, std::ios::out | std::ios::trunc);
    }
}

void CaptureEngine::setReferencePath(const std::string& path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_referencePath = path;

    if (m_reference.is_open())
    {
        m_reference.close();
    }

    if (!path.empty() && m_mode == CaptureMode::SyncTest)
    {
        m_reference.open(path, std::ios::in);
    }
}

void CaptureEngine::initFromEnvironment()
{
    // Read SLANG_CAPTURE_MODE
    std::string modeStr = getEnvVar("SLANG_CAPTURE_MODE");
    if (!modeStr.empty())
    {
        if (modeStr == "capture")
        {
            setMode(CaptureMode::Capture);
        }
        else if (modeStr == "replay")
        {
            setMode(CaptureMode::Replay);
        }
        else if (modeStr == "sync_test")
        {
            setMode(CaptureMode::SyncTest);
        }
        else if (modeStr == "disabled")
        {
            setMode(CaptureMode::Disabled);
        }
    }

    // Read SLANG_CAPTURE_FILE
    std::string filePath = getEnvVar("SLANG_CAPTURE_FILE");
    if (!filePath.empty() && m_mode == CaptureMode::Capture)
    {
        setOutputPath(filePath);
    }

    // Read SLANG_CAPTURE_REF
    std::string refPath = getEnvVar("SLANG_CAPTURE_REF");
    if (!refPath.empty() && m_mode == CaptureMode::SyncTest)
    {
        setReferencePath(refPath);
    }
}

uint64_t CaptureEngine::beginCall(const char* iface, const char* method, uint64_t objectId)
{
    if (m_mode == CaptureMode::Disabled)
    {
        return 0;
    }

    uint64_t callId = m_nextSeq.fetch_add(1);

    CallState call;
    call.seq = callId;
    call.objectId = objectId;
    call.iface = iface ? iface : "";
    call.method = method ? method : "";
    call.args = "";
    call.startTime = std::chrono::steady_clock::now();
    call.threadId = getCurrentThreadId();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeCalls[callId] = std::move(call);

    return callId;
}

void CaptureEngine::addArg(uint64_t callId, const char* name, const std::string& jsonValue)
{
    if (m_mode == CaptureMode::Disabled || callId == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_activeCalls.find(callId);
    if (it == m_activeCalls.end())
    {
        return;
    }

    // Build JSON object incrementally
    if (!it->second.args.empty())
    {
        it->second.args += ",";
    }
    it->second.args += "\"";
    it->second.args += name;
    it->second.args += "\":";
    it->second.args += jsonValue;
}

void CaptureEngine::endCall(uint64_t callId, const char* result, const std::string& outParamsJson)
{
    if (m_mode == CaptureMode::Disabled || callId == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_activeCalls.find(callId);
    if (it == m_activeCalls.end())
    {
        return;
    }

    writeCall(it->second, result, outParamsJson);
    m_activeCalls.erase(it);
}

uint64_t CaptureEngine::registerObject(void* ptr, const char* type)
{
    if (!ptr)
    {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if already registered
    auto it = m_ptrToId.find(ptr);
    if (it != m_ptrToId.end())
    {
        return it->second;
    }

    // Assign new ID
    uint64_t id = m_nextObjectId.fetch_add(1);
    m_ptrToId[ptr] = id;
    m_idToPtr[id] = ptr;
    if (type)
    {
        m_idToType[id] = type;
    }

    return id;
}

void* CaptureEngine::getObject(uint64_t id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_idToPtr.find(id);
    if (it != m_idToPtr.end())
    {
        return it->second;
    }
    return nullptr;
}

void CaptureEngine::releaseObject(uint64_t id)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_idToPtr.find(id);
    if (it != m_idToPtr.end())
    {
        m_ptrToId.erase(it->second);
        m_idToPtr.erase(it);
        m_idToType.erase(id);
    }
}

void CaptureEngine::writeCall(const CallState& call, const char* result, const std::string& outParamsJson)
{
    if (!m_output.is_open())
    {
        return;
    }

    // Calculate timestamp
    auto elapsed = std::chrono::duration<double>(call.startTime - m_startTime).count();

    // Build JSON line
    std::ostringstream json;
    json << std::fixed << std::setprecision(6);
    json << "{";
    json << "\"seq\":" << call.seq;
    json << ",\"ts\":" << elapsed;
    json << ",\"tid\":" << call.threadId;
    json << ",\"iface\":\"" << call.iface << "\"";
    json << ",\"method\":\"" << call.method << "\"";
    json << ",\"id\":" << call.objectId;
    json << ",\"args\":{" << call.args << "}";
    json << ",\"result\":\"" << (result ? result : "") << "\"";
    json << ",\"out\":" << outParamsJson;
    json << "}\n";

    m_output << json.str();
}

uint32_t CaptureEngine::getCurrentThreadId() const
{
#if defined(_WIN32)
    return static_cast<uint32_t>(GetCurrentThreadId());
#elif defined(__APPLE__)
    uint64_t tid;
    pthread_threadid_np(nullptr, &tid);
    return static_cast<uint32_t>(tid);
#else
    return static_cast<uint32_t>(syscall(SYS_gettid));
#endif
}

void CaptureEngine::flush()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_output.is_open())
    {
        m_output.flush();
    }
}

void CaptureEngine::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_output.is_open())
    {
        m_output.close();
    }
    if (m_reference.is_open())
    {
        m_reference.close();
    }
}

} // namespace slang_capture
