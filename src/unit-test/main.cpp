#include "slang-unit-test.h"
#include "core/slang-basic.h"

#include "slang.h"

extern "C" IUnitTestModule *slangUnitTestGetModule();

namespace Slang
{

class TestReporter : public ITestReporter
{
public:
    // ITestReporter
    virtual SLANG_NO_THROW void SLANG_MCALL startTest(const char* testName) SLANG_OVERRIDE;
    virtual SLANG_NO_THROW void SLANG_MCALL addResult(TestResult result)SLANG_OVERRIDE;
    virtual SLANG_NO_THROW void SLANG_MCALL addResultWithLocation(TestResult result, const char* testText, const char* file, int line) SLANG_OVERRIDE;
    virtual SLANG_NO_THROW void SLANG_MCALL addResultWithLocation(bool testSucceeded, const char* testText, const char* file, int line) SLANG_OVERRIDE;
    virtual SLANG_NO_THROW void SLANG_MCALL addExecutionTime(double time) SLANG_OVERRIDE { }
    virtual SLANG_NO_THROW void SLANG_MCALL message(TestMessageType type, const char* message) SLANG_OVERRIDE;
    virtual SLANG_NO_THROW void SLANG_MCALL endTest() SLANG_OVERRIDE { }

    StringBuilder m_buf;
    Index m_failCount = 0;
    Index m_testCount = 0;
};

void TestReporter::startTest(const char* testName)
{
    printf("Running test: %s\n", testName);
}

void TestReporter::message(TestMessageType type, const char* message)
{
    if (type == TestMessageType::RunError ||
        type == TestMessageType::TestFailure)
    {
        m_failCount++;
    }

    m_buf << message << "\n";
    printf("%s\n", message);
}

void TestReporter::addResultWithLocation(TestResult result, const char* testText, const char* file, int line)
{
    if (result == TestResult::Fail)
    {
        addResultWithLocation(false, testText, file, line);
    }
    else
    {
        m_testCount++;
    }
}

void TestReporter::addResultWithLocation(bool testSucceeded, const char* testText, const char* file, int line)
{
    m_testCount++;

    if (testSucceeded)
    {
        printf("[Success]: %s\n", testText);
        return;
    }

    m_buf << "[Failed]: " << testText << "\n";
    m_buf << file << ":" << line << "\n";
    printf("[Failed]: %s\n", testText);
    printf("%s:%d\n", file, line);

    m_failCount++;
}

void TestReporter::addResult(TestResult result)
{
    if (result == TestResult::Fail)
    {
        m_failCount++;
    }
}

}

int main(int argc, char **argv)
{
    slang::IGlobalSession* slangGlobalSession;
    slang::createGlobalSession(&slangGlobalSession);
    Slang::RenderApiFlags enabledApis = Slang::RenderApiFlag::Vulkan | Slang::RenderApiFlag::D3D12;

    UnitTestContext context;
    context.slangGlobalSession = slangGlobalSession;
    context.enabledApis = enabledApis;
    context.workDirectory = ".";
    context.executableDirectory = ".";

    Slang::TestReporter reporter;

    auto module = slangUnitTestGetModule();
    module->setTestReporter(&reporter);
    for (int i = 0; i < module->getTestCount(); ++i)
    {
        auto name = module->getTestName(i);
        auto func = module->getTestFunc(i);
        for (auto api : {Slang::RenderApiFlag::Vulkan, Slang::RenderApiFlag::D3D12})
        {
            context.enabledApis = api;
            func(&context);
        }
    }
    if (reporter.m_failCount > 0)
    {
        printf("Failed %lld tests\n", reporter.m_failCount);
        return 1;
    }
    else
    {
        printf("SUCCESS!\n");
        return 0;
    }
}
