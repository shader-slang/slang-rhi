#include <doctest.h>

#include <mutex>
#include <cstdarg>

namespace doctest {

#define LOCK() std::lock_guard<std::mutex> lock(mutex);

struct CustomReporter : public IReporter
{
    // caching pointers/references to objects of these types - safe to do
    std::ostream& stream;
    const ContextOptions& opt;
    ConsoleReporter consoleReporter;
    const TestCaseData* tc;
    const SubcaseSignature* sc;
    std::mutex mutex;
    Timer timer;
    int cursorPos = 0;

    const int resultPos = 64;
    const char* indent = "    ";

    // constructor has to accept the ContextOptions by ref as a single argument
    CustomReporter(const ContextOptions& in)
        : stream(*in.cout)
        , opt(in)
        , consoleReporter(in)
    {
    }

    void report_query(const QueryData& in) override { consoleReporter.report_query(in); }

    void test_run_start() override
    {
        LOCK();
        stream << Color::None;
        consoleReporter.test_run_start();
    }

    void test_run_end(const TestRunStats& in) override
    {
        LOCK();
        stream << Color::None;
        consoleReporter.test_run_end(in);
    }

    void test_case_start(const TestCaseData& in) override
    {
        LOCK();
        ensure_newline();
        consoleReporter.test_case_start(in);
        tc = &in;
        color(Color::Grey);
        fill(79, '-');
        printf("\n");
        color(Color::None);
        printf("%s\n", tc->m_name);
    }

    // called when a test case is reentered because of unfinished subcases
    void test_case_reenter(const TestCaseData& in) override
    {
        LOCK();
        ensure_newline();
        consoleReporter.test_case_reenter(in);
    }

    void test_case_end(const CurrentTestCaseStats& in) override
    {
        LOCK();
        ensure_newline();
        consoleReporter.test_case_end(in);
        color(Color::None);
        printf("%s", tc->m_name);
        fill(resultPos);
        if (in.failure_flags)
        {
            color(Color::Red);
            print("FAILED");
        }
        else
        {
            color(Color::Green);
            printf("PASSED");
        }
        color(Color::LightGrey);
        printf(" (%.2fs)\n", in.seconds);
    }

    void test_case_exception(const TestCaseException& in) override
    {
        LOCK();
        ensure_newline();
        consoleReporter.test_case_exception(in);
    }

    void subcase_start(const SubcaseSignature& in) override
    {
        LOCK();
        sc = &in;
        timer.start();
        stream << Color::LightGrey;
        printf("%s (%s)", tc->m_name, sc->m_name.c_str());
    }

    void subcase_end() override
    {
        LOCK();
        double seconds = timer.getElapsedSeconds();
        stream << Color::LightGrey;
        if (cursorPos == 0)
            printf("%s (%s)", tc->m_name, sc->m_name.c_str());
        fill(resultPos);
        printf("  DONE (%.2fs)\n", seconds);
    }

    void log_assert(const AssertData& in) override
    {
        // don't include successful asserts by default - this is done here
        // instead of in the framework itself because doctest doesn't know
        // if/when a reporter/listener cares about successful results
        if (!in.m_failed && !opt.success)
            return;

        LOCK();
        ensure_newline();
        consoleReporter.log_assert(in);
    }

    void log_message(const MessageData& in) override
    {
        LOCK();
        ensure_newline();
        consoleReporter.log_message(in);
    }

    void test_case_skipped(const TestCaseData& /*in*/) override {}

private:
    void color(Color::Enum c)
    {
        if (!opt.no_colors)
            stream << c;
    }

    void fill(int x, char c = ' ')
    {
        while (cursorPos < x)
        {
            stream << c;
            cursorPos++;
        }
    }

    void print(std::string_view str)
    {
        stream << str;
        stream.flush();
        for (char c : str)
            cursorPos = c == '\n' ? 0 : cursorPos + 1;
    }

    void printf(const char* fmt, ...)
    {
        std::va_list args;
        va_start(args, fmt);
        char buffer[1024];
        std::vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        print(buffer);
    }

    void println(std::string_view str)
    {
        print(str);
        print("\n");
    }

    void ensure_newline()
    {
        if (cursorPos != 0)
            print("\n");
    }
};

// "1" is the priority - used for ordering when multiple reporters are used
REGISTER_REPORTER("custom", 1, CustomReporter);

} // namespace doctest
