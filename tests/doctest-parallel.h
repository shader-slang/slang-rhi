// A process-based parallel runner for doctest. Requires C++17 and native threads.
// Copy this file alongside doctest.h; no other project headers are required.
//
// In exactly one translation unit, define DOCTEST_PARALLEL_IMPLEMENT before
// including this header. Define DOCTEST_CONFIG_IMPLEMENT there too if that TU
// also provides doctest's implementation. Use a custom main, for example:
//
//   #define DOCTEST_CONFIG_IMPLEMENT
//   #define DOCTEST_PARALLEL_IMPLEMENT
//   #include "doctest-parallel.h"
//   int main(int argc, char** argv) {
//       if (auto result = doctest::parallel::run(argc, argv)) return *result;
//       return doctest::Context(argc, argv).run();
//   }
//
// --jobs=N enables persistent workers; --test-timeout=SECONDS limits assignment,
// startup and shutdown time. Omit --jobs for the caller's sequential runner.
// Test discovery uses doctest's public query/reporter APIs. doctest.h is unmodified.

#ifndef DOCTEST_PARALLEL_INCLUDED
#define DOCTEST_PARALLEL_INCLUDED

#include <doctest.h>
#include <functional>
#include <optional>
#include <string>

#ifdef DOCTEST_PARALLEL_CONFIG_IMPLEMENTATION_IN_DLL
#ifdef DOCTEST_PARALLEL_IMPLEMENT
#define DOCTEST_PARALLEL_INTERFACE DOCTEST_SYMBOL_EXPORT
#else
#define DOCTEST_PARALLEL_INTERFACE DOCTEST_SYMBOL_IMPORT
#endif
#else
#define DOCTEST_PARALLEL_INTERFACE
#endif

namespace doctest {
namespace parallel {

struct Result
{
    std::string name;
    int exitCode = 1;
    unsigned executed = 0;
    unsigned skipped = 0;
    bool workerFailed = false;
    double seconds = 0;
    std::string data;
};

struct Options
{
    std::string reporters = "console";
    // Called in each worker, after output redirection. Return nonzero to fail.
    // workerStop runs before returning to main; final process exit also matters.
    std::function<int(const std::string& logPath)> workerStart;
    std::function<int()> workerStop;
    // Applied to fresh contexts before the runner sets scheduling/reporting options.
    std::function<void(doctest::Context&)> configureContext;
    // Optional custom runtime skip detection; doctest skip decorators work normally.
    std::function<bool(const doctest::TestCaseData&)> isSkipped;
    // Collect per-assignment metadata (up to 64 KiB, including binary data).
    std::function<std::string()> collectData;
    // Coordinator callbacks. onResult calls are serialized, in completion order.
    std::function<void(const Result&)> onResult;
    std::function<void()> coordinatorStart;
    std::function<int()> coordinatorStop;
    // Optional caller-owned log directory. Otherwise a temporary directory is
    // created and removed automatically. Supply an isolated directory per run.
    std::function<std::string()> logDirectory;
};

// No value means a query or sequential invocation: run the existing main path.
// Initialize process-wide project options before calling this function. Worker
// hooks should initialize test resources; the coordinator never executes tests.
// Calls must be sequential within a process. Callback exceptions are not caught.
DOCTEST_PARALLEL_INTERFACE std::optional<int> run(int argc, const char* const* argv, const Options& options = {});

} // namespace parallel
} // namespace doctest

#endif // DOCTEST_PARALLEL_INCLUDED

#if defined(DOCTEST_PARALLEL_IMPLEMENT) && !defined(DOCTEST_PARALLEL_IMPLEMENTED)
#define DOCTEST_PARALLEL_IMPLEMENTED

#ifndef DOCTEST_CONFIG_OPTIONS_PREFIX
#define DOCTEST_CONFIG_OPTIONS_PREFIX "dt-"
#endif

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include <string_view>

#ifdef DOCTEST_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define DOCTEST_PARALLEL_UNDEF_NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef DOCTEST_PARALLEL_UNDEF_NOMINMAX
#undef NOMINMAX
#undef DOCTEST_PARALLEL_UNDEF_NOMINMAX
#endif
#include <io.h>
#include <fcntl.h>
#include <share.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace doctest {
namespace parallel {
namespace detail {

// Query and execution results come exclusively from doctest's public reporter API.
struct Assignment
{
    unsigned ordinal;
    std::string name;
    uint64_t identity;
};
static std::vector<Assignment> sAssignments;
static unsigned sExecuted = 0;
static unsigned sSkipped = 0;
static uint64_t sIdentity = 0;
static bool sNoRun = false;
static const Options* sOptions = nullptr;

// Fixed hashing keeps assignment validation independent of process address layout
// and distinguishes same-named tests in different suites or source locations.
static uint64_t identity(const doctest::TestCaseData& test)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    std::string key = std::string(test.m_name) + '\n' + test.m_test_suite + '\n' + test.m_file.c_str() + '\n' +
                      std::to_string(test.m_line);
    for (unsigned char c : key)
        hash = (hash ^ c) * UINT64_C(1099511628211);
    return hash;
}

static std::string encode(const std::string& data)
{
    static const char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(data.size() * 2);
    for (unsigned char c : data)
    {
        result += digits[c >> 4];
        result += digits[c & 15];
    }
    return result;
}

static bool decode(const std::string& encoded, std::string& result)
{
    if (encoded.size() % 2)
        return false;
    auto digit = [](char c)
    {
        return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    };
    for (size_t i = 0; i < encoded.size(); i += 2)
    {
        int high = digit(encoded[i]), low = digit(encoded[i + 1]);
        if (high < 0 || low < 0)
            return false;
        result += char((high << 4) | low);
    }
    return true;
}

struct QueueReporter : doctest::IReporter
{
    const doctest::TestCaseData* current = nullptr;
    unsigned first = 1;
    explicit QueueReporter(const doctest::ContextOptions& options)
        : first(options.first ? options.first : 1)
    {
        sNoRun = options.no_run;
    }
    void report_query(const doctest::QueryData& query) override
    {
        for (unsigned i = 0; i < query.num_data; ++i)
            sAssignments.push_back({i + first, query.data[i]->m_name, identity(*query.data[i])});
    }
    void test_case_end(const doctest::CurrentTestCaseStats&) override
    {
        ++sExecuted;
        sIdentity = identity(*current);
        if (sOptions && sOptions->isSkipped && sOptions->isSkipped(*current))
            ++sSkipped;
    }
    void test_run_start() override {}
    void test_run_end(const doctest::TestRunStats&) override {}
    void test_case_start(const doctest::TestCaseData& test) override { current = &test; }
    void test_case_reenter(const doctest::TestCaseData&) override {}
    void test_case_exception(const doctest::TestCaseException&) override {}
    void subcase_start(const doctest::SubcaseSignature&) override {}
    void subcase_end() override {}
    void log_assert(const doctest::AssertData&) override {}
    void log_message(const doctest::MessageData&) override {}
    void test_case_skipped(const doctest::TestCaseData&) override {}
};
DOCTEST_REGISTER_REPORTER("doctest-parallel-queue", 0, QueueReporter);

// One parent thread owns each process and its blocking pipes. Only the work queue
// and console output are shared between these threads.
struct Worker
{
    FILE* input = nullptr;
    FILE* output = nullptr;
#ifdef DOCTEST_PLATFORM_WINDOWS
    HANDLE process = nullptr;

    static std::string quote(const std::string& value)
    {
        std::string result = "\"";
        size_t slashes = 0;
        for (char c : value)
        {
            if (c == '\\')
            {
                ++slashes;
                continue;
            }
            result.append(slashes * (c == '"' ? 2 : 1), '\\');
            slashes = 0;
            if (c == '"')
                result += '\\';
            result += c;
        }
        result.append(slashes * 2, '\\');
        return result + '"';
    }
#else
    pid_t process = -1;
#endif

    bool start(const std::vector<std::string>& arguments)
    {
        // Serialize pipe creation and spawning so no child inherits another
        // spawn's transient, inheritable pipe ends.
        static std::mutex spawnMutex;
        std::lock_guard<std::mutex> lock(spawnMutex);
#ifdef DOCTEST_PLATFORM_WINDOWS
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE childInput = nullptr, parentInput = nullptr, parentOutput = nullptr, childOutput = nullptr;
        if (!CreatePipe(&childInput, &parentInput, &security, 0))
            return false;
        if (!CreatePipe(&parentOutput, &childOutput, &security, 0))
        {
            CloseHandle(childInput);
            CloseHandle(parentInput);
            return false;
        }
        SetHandleInformation(parentInput, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(parentOutput, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = childInput;
        startup.hStdOutput = childOutput;
        HANDLE childError = nullptr;
        DuplicateHandle(
            GetCurrentProcess(),
            childOutput,
            GetCurrentProcess(),
            &childError,
            0,
            TRUE,
            DUPLICATE_SAME_ACCESS
        );
        startup.hStdError = childError;
        PROCESS_INFORMATION info{};
        std::string command;
        for (const auto& argument : arguments)
            command += quote(argument) + " ";
        bool success = CreateProcessA(
                           nullptr,
                           command.data(),
                           nullptr,
                           nullptr,
                           TRUE,
                           CREATE_NO_WINDOW,
                           nullptr,
                           nullptr,
                           &startup,
                           &info
                       ) != FALSE;
        CloseHandle(childInput);
        CloseHandle(childOutput);
        if (childError)
            CloseHandle(childError);
        if (!success)
        {
            CloseHandle(parentInput);
            CloseHandle(parentOutput);
            return false;
        }
        process = info.hProcess;
        CloseHandle(info.hThread);
        input = _fdopen(_open_osfhandle(reinterpret_cast<intptr_t>(parentInput), _O_WRONLY | _O_BINARY), "wb");
        output = _fdopen(_open_osfhandle(reinterpret_cast<intptr_t>(parentOutput), _O_RDONLY | _O_BINARY), "rb");
#else
        int toChild[2], fromChild[2];
        if (pipe(toChild) != 0)
            return false;
        if (pipe(fromChild) != 0)
        {
            close(toChild[0]);
            close(toChild[1]);
            return false;
        }
        // Prevent concurrently spawned workers from inheriting other workers' pipes.
        for (int fd : {toChild[0], toChild[1], fromChild[0], fromChild[1]})
            fcntl(fd, F_SETFD, FD_CLOEXEC);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, toChild[0], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, fromChild[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, fromChild[1], STDERR_FILENO);
        std::vector<char*> args;
        for (const auto& argument : arguments)
            args.push_back(const_cast<char*>(argument.c_str()));
        args.push_back(nullptr);
        int error = posix_spawnp(&process, args[0], &actions, nullptr, args.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        close(toChild[0]);
        close(fromChild[1]);
        if (error != 0)
        {
            process = -1;
            close(toChild[1]);
            close(fromChild[0]);
            return false;
        }
        input = fdopen(toChild[1], "w");
        output = fdopen(fromChild[0], "r");
#endif
        if (output)
            setvbuf(output, nullptr, _IONBF, 0);
        return input && output;
    }

    // Messages are short newline-terminated records. Read bytewise so partial
    // records and EOF are handled without stdio read-ahead hiding pipe data.
    bool receive(std::string& message, int timeoutSeconds)
    {
        message.clear();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
        while (message.size() < 132096)
        {
            if (timeoutSeconds && std::chrono::steady_clock::now() >= deadline)
                return false;
#ifdef DOCTEST_PLATFORM_WINDOWS
            DWORD available = 0;
            HANDLE pipe = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(output)));
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
                return false;
            if (!available)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
#else
            pollfd descriptor{fileno(output), POLLIN, 0};
            int ready = poll(&descriptor, 1, 100);
            if (ready < 0 && errno == EINTR)
                continue;
            if (ready < 0)
                return false;
            if (!ready)
                continue;
#endif
            char c = 0;
#ifdef DOCTEST_PLATFORM_WINDOWS
            DWORD received = 0;
            if (!ReadFile(pipe, &c, 1, &received, nullptr) || received != 1)
#else
            if (read(fileno(output), &c, 1) != 1)
#endif
                return false;
            if (c == '\n')
                return true;
            if (c == '\r')
                continue;
            message += char(c);
        }
        return false;
    }

    void terminate()
    {
#ifdef DOCTEST_PLATFORM_WINDOWS
        if (process)
            TerminateProcess(process, 1);
#else
        if (process > 0)
            kill(process, SIGKILL);
#endif
    }

    int finish(int timeoutSeconds = 0)
    {
        if (input)
            fclose(input);
        if (output)
            fclose(output);
        input = output = nullptr;
#ifdef DOCTEST_PLATFORM_WINDOWS
        if (!process)
            return 1;
        DWORD timeout =
            timeoutSeconds ? DWORD((std::min<int64_t>)(int64_t(timeoutSeconds) * 1000, INFINITE - 1)) : INFINITE;
        if (WaitForSingleObject(process, timeout) == WAIT_TIMEOUT)
        {
            terminate();
            WaitForSingleObject(process, INFINITE);
        }
        DWORD code = 1;
        GetExitCodeProcess(process, &code);
        CloseHandle(process);
        process = nullptr;
        return int(code);
#else
        if (process < 0)
            return 1;
        int status = 0;
        pid_t waited;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
        while (true)
        {
            waited = waitpid(process, &status, timeoutSeconds ? WNOHANG : 0);
            if (waited > 0 || (waited < 0 && errno != EINTR))
                break;
            if (timeoutSeconds && std::chrono::steady_clock::now() >= deadline)
            {
                terminate();
                timeoutSeconds = 0;
            }
            else if (waited == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        process = -1;
        return waited >= 0 && WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
    }
    ~Worker()
    {
        if (input || output)
            finish();
    }
};

int runWorker(int argc, const char* const* argv, const std::string& logPath, const Options& options)
{
    // Preserve the protocol stream, then redirect all test/driver output to a file.
    // Files cannot fill a pipe and deadlock a worker producing verbose diagnostics.
#ifdef DOCTEST_PLATFORM_WINDOWS
    FILE* protocol = _fdopen(_dup(_fileno(stdout)), "w");
    if (protocol)
        _setmode(_fileno(protocol), _O_BINARY);
#else
    FILE* protocol = fdopen(dup(fileno(stdout)), "w");
#endif
    auto redirect = [&](FILE* stream)
    {
#ifdef DOCTEST_PLATFORM_WINDOWS
        FILE* log = _fsopen(logPath.c_str(), "ab", _SH_DENYNO);
        if (!log)
            return false;
        int result = _dup2(_fileno(log), _fileno(stream));
        fclose(log);
        return result == 0;
#else
        return freopen(logPath.c_str(), "a", stream) != nullptr;
#endif
    };
    if (!protocol || !redirect(stdout))
    {
        if (protocol)
        {
            fprintf(protocol, "redirect-failed %d\n", errno);
            fflush(protocol);
        }
        return 1;
    }
    // Both streams append to the same file, including when stdout is redirected.
#ifdef DOCTEST_PLATFORM_WINDOWS
    _dup2(_fileno(stdout), _fileno(stderr));
#else
    dup2(fileno(stdout), fileno(stderr));
#endif
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    sOptions = &options;
    if (options.workerStart && options.workerStart(logPath) != 0)
    {
        fclose(protocol);
        return 1;
    }
    fprintf(protocol, "ready\n");
    fflush(protocol);
    unsigned ordinal;
    while (std::cin >> ordinal)
    {
        sExecuted = sSkipped = 0;
        sIdentity = 0;
        doctest::Context context(argc, argv);
        if (options.configureContext)
            options.configureContext(context);
        context.setOption(
            DOCTEST_CONFIG_OPTIONS_PREFIX "reporters",
            (options.reporters + ",doctest-parallel-queue").c_str()
        );
        context.setOption(DOCTEST_CONFIG_OPTIONS_PREFIX "order-by", "name");
        context.setOption(DOCTEST_CONFIG_OPTIONS_PREFIX "first", int(ordinal));
        context.setOption(DOCTEST_CONFIG_OPTIONS_PREFIX "last", int(ordinal));
        int result = context.run();
        std::cout.flush();
        std::cerr.flush();
        fflush(nullptr);
        std::string data = options.collectData ? options.collectData() : std::string();
        if (data.size() > 65536)
        {
            fprintf(stderr, "doctest-parallel: result metadata exceeds 64 KiB\n");
            data.clear();
            result = 1;
        }
        // Hex encoding permits arbitrary binary metadata without corrupting framing.
        std::string response = "done " + std::to_string(result) + " " + std::to_string(sExecuted) + " " +
                               std::to_string(sSkipped) + " " + std::to_string(sIdentity) + " \"" + encode(data) +
                               "\"\n";
        fputs(response.c_str(), protocol);
        fflush(protocol);
    }
    int result = options.workerStop ? options.workerStop() : 0;
    fclose(protocol);
    sOptions = nullptr;
    return result;
}

// Only remove directories created by the extension itself. A supplied directory
// belongs to the caller and may contain other artifacts.
struct LogDirectory
{
    std::filesystem::path path;
    bool owned = false;
    ~LogDirectory()
    {
        if (owned)
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    }
};

} // namespace detail

std::optional<int> run(int argc, const char* const* argv, const Options& options)
{
    using namespace detail;
    int jobs = 0;
    int timeoutSeconds = 0;
    bool requested = false;
    std::string workerLog;
    for (int i = 1; i < argc; ++i)
    {
        std::string_view argument = argv[i];
        if (argument.substr(0, 18) == "--parallel-worker=")
            workerLog = argument.substr(18);
        if (argument.substr(0, 15) == "--test-timeout=")
        {
            auto value = argument.substr(15);
            auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), timeoutSeconds);
            if (error != std::errc() || end != value.data() + value.size() || timeoutSeconds <= 0)
            {
                fprintf(stderr, "--test-timeout requires a positive number of seconds\n");
                return 1;
            }
        }
        if (argument.substr(0, 7) == "--jobs=")
        {
            requested = true;
            auto value = argument.substr(7);
            auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), jobs);
            if (error != std::errc() || end != value.data() + value.size() || jobs <= 0)
            {
                fprintf(stderr, "--jobs requires a positive integer\n");
                return 1;
            }
        }
    }
    if (!workerLog.empty())
        return runWorker(argc, argv, workerLog, options);
    if (!requested)
        return std::nullopt;

    doctest::Context discovery(argc, argv);
    if (options.configureContext)
        options.configureContext(discovery);
    // Explicit query flags retain the caller's sequential reporting path. The
    // query reporter reads first/last options without changing the active context.
    if (discovery.shouldExit())
        return std::nullopt;
    discovery.setOption(DOCTEST_CONFIG_OPTIONS_PREFIX "order-by", "name");
    discovery.setOption(DOCTEST_CONFIG_OPTIONS_PREFIX "reporters", "doctest-parallel-queue");
    const char* queryFlag = "--" DOCTEST_CONFIG_OPTIONS_PREFIX "list-test-cases";
    discovery.applyCommandLine(1, &queryFlag);
    sAssignments.clear();
    if (discovery.run() != 0)
        return 1;
    if (sNoRun)
        return std::nullopt;
    if (sAssignments.empty())
    {
        printf("No tests matched.\n");
        return 0;
    }

    if (options.coordinatorStart)
        options.coordinatorStart();
    LogDirectory directory;
    if (options.logDirectory)
        directory.path = options.logDirectory();
    else
    {
#ifdef DOCTEST_PLATFORM_WINDOWS
        auto processId = GetCurrentProcessId();
#else
        auto processId = getpid();
#endif
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory.path = std::filesystem::temp_directory_path() /
                         ("doctest-parallel-" + std::to_string(processId) + "-" + std::to_string(stamp));
        directory.owned = true;
    }
    std::filesystem::create_directories(directory.path);
    std::atomic<size_t> next{0};
    std::atomic<unsigned> failures{0}, skipped{0};
    std::mutex consoleMutex;
    std::vector<std::thread> threads;
    auto start = std::chrono::steady_clock::now();
    jobs = (std::min)(jobs, int(sAssignments.size()));
    printf("Running %zu tests with %d workers\n", sAssignments.size(), jobs);
    for (int workerIndex = 0; workerIndex < jobs; ++workerIndex)
    {
        threads.emplace_back(
            [&, workerIndex]()
            {
                auto logPath = directory.path / ("worker-" + std::to_string(workerIndex) + ".log");
                uintmax_t logOffset = 0;
                auto printLog = [&]()
                {
                    std::ifstream log(logPath, std::ios::binary);
                    if (log)
                    {
                        log.seekg(std::streamoff(logOffset));
                        std::string contents((std::istreambuf_iterator<char>(log)), {});
                        logOffset += contents.size();
                        std::cout << contents;
                    }
                };
                std::vector<std::string> arguments;
                for (int i = 0; i < argc; ++i)
                    arguments.emplace_back(argv[i]);
                arguments.push_back("--parallel-worker=" + logPath.string());
                Worker worker;
#ifndef DOCTEST_PLATFORM_WINDOWS
                // A crashed worker must yield EPIPE instead of terminating the parent.
                sigset_t signals;
                sigemptyset(&signals);
                sigaddset(&signals, SIGPIPE);
                pthread_sigmask(SIG_BLOCK, &signals, nullptr);
#endif
                bool running = false;
                std::string message;
                while (true)
                {
                    size_t index = next.fetch_add(1);
                    if (index >= sAssignments.size())
                        break;
                    const auto& assignment = sAssignments[index];
                    if (!running)
                    {
                        running =
                            worker.start(arguments) && worker.receive(message, timeoutSeconds) && message == "ready";
                        if (!running)
                        {
                            worker.terminate();
                            worker.finish();
                            ++failures;
                            std::lock_guard<std::mutex> lock(consoleMutex);
                            fprintf(
                                stderr,
                                "Worker %d failed to start for %s (response: %s)\n",
                                workerIndex,
                                assignment.name.c_str(),
                                message.c_str()
                            );
                            printLog();
                            // Do not repeatedly spawn workers when initialization is broken.
                            break;
                        }
                    }
                    Result result;
                    result.name = assignment.name;
                    auto assignmentStart = std::chrono::steady_clock::now();
                    bool sent = fprintf(worker.input, "%u\n", assignment.ordinal) > 0 && fflush(worker.input) == 0;
                    bool completed = sent && worker.receive(message, timeoutSeconds);
                    std::istringstream response(message);
                    std::string tag, encoded;
                    uint64_t actualIdentity = 0;
                    completed = completed &&
                                bool(
                                    response >> tag >> result.exitCode >> result.executed >> result.skipped >>
                                    actualIdentity >> std::quoted(encoded)
                                ) &&
                                tag == "done" && actualIdentity == assignment.identity && decode(encoded, result.data);
                    result.workerFailed = !completed;
                    result.seconds =
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - assignmentStart).count();
                    if (!completed)
                    {
                        worker.terminate();
                        worker.finish();
                        running = false;
                    }
                    {
                        std::lock_guard<std::mutex> lock(consoleMutex);
                        printLog();
                        if (options.onResult)
                            options.onResult(result);
                        if (!completed || result.executed != 1)
                            fprintf(
                                stderr,
                                "FAILED: %s (worker exited, timed out, or returned an invalid result)\n",
                                assignment.name.c_str()
                            );
                    }
                    if (!completed || result.exitCode || result.executed != 1)
                        ++failures;
                    if (completed)
                        skipped += result.skipped;
                    // A failed assignment is not retried. A fresh worker takes the next test.
                }
                int exitCode = 0;
                if (running)
                {
                    // EOF requests normal cleanup; keep the output pipe until exit.
                    fclose(worker.input);
                    worker.input = nullptr;
                    // The worker closes the protocol before main performs leak checks.
                    // finish() waits for the final process exit code as well.
                    exitCode = worker.finish(timeoutSeconds);
                }
                std::lock_guard<std::mutex> lock(consoleMutex);
                printLog();
                if (exitCode)
                {
                    ++failures;
                    fprintf(stderr, "Worker %d failed during cleanup\n", workerIndex);
                }
            }
        );
    }
    for (auto& thread : threads)
        thread.join();
    size_t assigned = (std::min)(next.load(), sAssignments.size());
    if (assigned != sAssignments.size())
    {
        fprintf(stderr, "%zu tests were not assigned because workers exited\n", sAssignments.size() - assigned);
        ++failures;
    }
    if (options.coordinatorStop && options.coordinatorStop() != 0)
        ++failures;
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    printf(
        "Parallel run: %zu assigned, %u skipped, %u failures (including worker failures), %.2fs\n",
        assigned,
        skipped.load(),
        failures.load(),
        seconds
    );
    return failures ? 1 : 0;
}

} // namespace parallel
} // namespace doctest

#endif // DOCTEST_PARALLEL_IMPLEMENT
