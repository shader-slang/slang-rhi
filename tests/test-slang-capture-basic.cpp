#include "testing.h"
#include "../src/slang-capture/capture-engine.h"
#include "../src/slang-capture/json-serialization.h"

#include <fstream>
#include <sstream>
#include <cstdio>

using namespace slang_capture;

TEST_CASE("slang-capture-engine-basic")
{
    auto& engine = CaptureEngine::instance();

    SUBCASE("initial-state")
    {
        // Engine should start disabled
        // Note: This test may fail if run after other tests that modify state
        // For now, we explicitly set disabled mode
        engine.setMode(CaptureMode::Disabled);
        CHECK(engine.getMode() == CaptureMode::Disabled);
        CHECK(!engine.isCapturing());
    }

    SUBCASE("mode-switching")
    {
        engine.setMode(CaptureMode::Disabled);
        CHECK(engine.getMode() == CaptureMode::Disabled);
        CHECK(!engine.isCapturing());

        engine.setMode(CaptureMode::Capture);
        CHECK(engine.getMode() == CaptureMode::Capture);
        CHECK(engine.isCapturing());

        engine.setMode(CaptureMode::Replay);
        CHECK(engine.getMode() == CaptureMode::Replay);
        CHECK(!engine.isCapturing());

        engine.setMode(CaptureMode::SyncTest);
        CHECK(engine.getMode() == CaptureMode::SyncTest);
        CHECK(!engine.isCapturing());

        // Reset to disabled
        engine.setMode(CaptureMode::Disabled);
    }

    SUBCASE("object-registration")
    {
        engine.setMode(CaptureMode::Disabled);

        int obj1 = 42;
        int obj2 = 100;

        uint64_t id1 = engine.registerObject(&obj1, "TestType1");
        uint64_t id2 = engine.registerObject(&obj2, "TestType2");

        CHECK(id1 != 0);
        CHECK(id2 != 0);
        CHECK(id1 != id2);

        CHECK(engine.getObject(id1) == &obj1);
        CHECK(engine.getObject(id2) == &obj2);

        // Same pointer should return same ID
        uint64_t id1Again = engine.registerObject(&obj1, "TestType1");
        CHECK(id1Again == id1);

        // Release and verify
        engine.releaseObject(id1);
        CHECK(engine.getObject(id1) == nullptr);
        CHECK(engine.getObject(id2) == &obj2);

        // Cleanup
        engine.releaseObject(id2);
    }

    SUBCASE("null-object-registration")
    {
        uint64_t id = engine.registerObject(nullptr, "NullType");
        CHECK(id == 0);
    }

    SUBCASE("capture-to-file")
    {
        std::string testFile = rhi::testing::getCaseTempDirectory() + "/capture-test.jsonl";

        // Enable capture mode and set output
        engine.setMode(CaptureMode::Capture);
        engine.setOutputPath(testFile);

        // Record a simple call
        int testObject = 123;
        uint64_t objId = engine.registerObject(&testObject, "TestObject");

        uint64_t callId = engine.beginCall("TestInterface", "testMethod", objId);
        engine.addArg(callId, "arg1", "\"hello\"");
        engine.addArg(callId, "arg2", "42");
        engine.endCall(callId, "SLANG_OK", "{\"output\":1}");

        // Flush and close
        engine.flush();
        engine.close();

        // Read back the file and verify
        std::ifstream file(testFile);
        REQUIRE(file.is_open());

        std::string line;
        REQUIRE(std::getline(file, line));

        // Basic validation - check that required fields are present
        CHECK(line.find("\"seq\":") != std::string::npos);
        CHECK(line.find("\"iface\":\"TestInterface\"") != std::string::npos);
        CHECK(line.find("\"method\":\"testMethod\"") != std::string::npos);
        CHECK(line.find("\"arg1\":\"hello\"") != std::string::npos);
        CHECK(line.find("\"arg2\":42") != std::string::npos);
        CHECK(line.find("\"result\":\"SLANG_OK\"") != std::string::npos);

        file.close();

        // Cleanup
        engine.releaseObject(objId);
        engine.setMode(CaptureMode::Disabled);
    }

    SUBCASE("disabled-mode-no-output")
    {
        std::string testFile = rhi::testing::getCaseTempDirectory() + "/disabled-test.jsonl";

        // Remove file if it exists
        std::remove(testFile.c_str());

        engine.setMode(CaptureMode::Disabled);
        engine.setOutputPath(testFile);

        // Try to record - should be no-op
        uint64_t callId = engine.beginCall("TestInterface", "shouldNotRecord", 0);
        CHECK(callId == 0);

        // File should not be created (since we didn't open it in disabled mode)
        std::ifstream file(testFile);
        CHECK(!file.is_open());
    }

    SUBCASE("multiple-calls")
    {
        std::string testFile = rhi::testing::getCaseTempDirectory() + "/multi-call-test.jsonl";

        engine.setMode(CaptureMode::Capture);
        engine.setOutputPath(testFile);

        // Record multiple calls
        for (int i = 0; i < 5; ++i)
        {
            uint64_t callId = engine.beginCall("TestInterface", "call", 1);
            engine.addArg(callId, "index", std::to_string(i));
            engine.endCall(callId, "OK");
        }

        engine.flush();
        engine.close();

        // Count lines in file
        std::ifstream file(testFile);
        REQUIRE(file.is_open());

        int lineCount = 0;
        std::string line;
        while (std::getline(file, line))
        {
            if (!line.empty())
            {
                lineCount++;
            }
        }
        CHECK(lineCount == 5);

        file.close();
        engine.setMode(CaptureMode::Disabled);
    }
}

TEST_CASE("slang-capture-json-serialization")
{
    SUBCASE("basic-types")
    {
        CHECK(toJson(nullptr) == "null");
        CHECK(toJson(true) == "true");
        CHECK(toJson(false) == "false");
        CHECK(toJson(42) == "42");
        CHECK(toJson(static_cast<unsigned int>(100)) == "100");
        CHECK(toJson(static_cast<int64_t>(-123456789)) == "-123456789");
        CHECK(toJson(static_cast<uint64_t>(987654321)) == "987654321");
    }

    SUBCASE("string-escaping")
    {
        // Simple string
        CHECK(escapeJsonString("hello") == "\"hello\"");

        // Empty string
        CHECK(escapeJsonString("") == "\"\"");

        // Null string
        CHECK(escapeJsonString(static_cast<const char*>(nullptr)) == "null");

        // Quotes in string
        CHECK(escapeJsonString("say \"hello\"") == "\"say \\\"hello\\\"\"");

        // Backslash in string
        CHECK(escapeJsonString("path\\to\\file") == "\"path\\\\to\\\\file\"");

        // Newline and tab
        CHECK(escapeJsonString("line1\nline2") == "\"line1\\nline2\"");
        CHECK(escapeJsonString("col1\tcol2") == "\"col1\\tcol2\"");

        // Carriage return
        CHECK(escapeJsonString("text\rmore") == "\"text\\rmore\"");

        // Mixed special characters
        CHECK(escapeJsonString("a\"b\\c\nd") == "\"a\\\"b\\\\c\\nd\"");
    }

    SUBCASE("slang-result")
    {
        CHECK(std::string(slangResultToString(SLANG_OK)) == "SLANG_OK");
        CHECK(std::string(slangResultToString(SLANG_E_NOT_FOUND)) == "SLANG_E_NOT_FOUND");
        CHECK(std::string(slangResultToString(SLANG_E_INVALID_ARG)) == "SLANG_E_INVALID_ARG");
        CHECK(std::string(slangResultToString(SLANG_E_OUT_OF_MEMORY)) == "SLANG_E_OUT_OF_MEMORY");

        // slangResultToJson wraps in quotes
        CHECK(slangResultToJson(SLANG_OK) == "\"SLANG_OK\"");
    }

    SUBCASE("slang-compile-target")
    {
        CHECK(std::string(slangCompileTargetToString(SLANG_SPIRV)) == "SLANG_SPIRV");
        CHECK(std::string(slangCompileTargetToString(SLANG_DXIL)) == "SLANG_DXIL");
        CHECK(std::string(slangCompileTargetToString(SLANG_HLSL)) == "SLANG_HLSL");
        CHECK(std::string(slangCompileTargetToString(SLANG_METAL)) == "SLANG_METAL");
        CHECK(std::string(slangCompileTargetToString(SLANG_TARGET_UNKNOWN)) == "SLANG_TARGET_UNKNOWN");

        CHECK(slangCompileTargetToJson(SLANG_SPIRV) == "\"SLANG_SPIRV\"");
    }

    SUBCASE("slang-enums")
    {
        CHECK(
            std::string(slangMatrixLayoutModeToString(SLANG_MATRIX_LAYOUT_ROW_MAJOR)) == "SLANG_MATRIX_LAYOUT_ROW_MAJOR"
        );
        CHECK(
            std::string(slangMatrixLayoutModeToString(SLANG_MATRIX_LAYOUT_COLUMN_MAJOR)) ==
            "SLANG_MATRIX_LAYOUT_COLUMN_MAJOR"
        );

        CHECK(
            std::string(slangFloatingPointModeToString(SLANG_FLOATING_POINT_MODE_FAST)) ==
            "SLANG_FLOATING_POINT_MODE_FAST"
        );
        CHECK(
            std::string(slangFloatingPointModeToString(SLANG_FLOATING_POINT_MODE_PRECISE)) ==
            "SLANG_FLOATING_POINT_MODE_PRECISE"
        );

        CHECK(std::string(slangStageToString(SLANG_STAGE_VERTEX)) == "SLANG_STAGE_VERTEX");
        CHECK(std::string(slangStageToString(SLANG_STAGE_FRAGMENT)) == "SLANG_STAGE_FRAGMENT");
        CHECK(std::string(slangStageToString(SLANG_STAGE_COMPUTE)) == "SLANG_STAGE_COMPUTE");
    }

    SUBCASE("target-desc")
    {
        slang::TargetDesc desc = {};
        desc.format = SLANG_SPIRV;
        desc.floatingPointMode = SLANG_FLOATING_POINT_MODE_FAST;
        desc.forceGLSLScalarBufferLayout = true;

        std::string json = toJson(desc);

        CHECK(json.find("\"format\":\"SLANG_SPIRV\"") != std::string::npos);
        CHECK(json.find("\"floatingPointMode\":\"SLANG_FLOATING_POINT_MODE_FAST\"") != std::string::npos);
        CHECK(json.find("\"forceGLSLScalarBufferLayout\":true") != std::string::npos);
    }

    SUBCASE("session-desc")
    {
        slang::TargetDesc targetDesc = {};
        targetDesc.format = SLANG_DXIL;

        const char* searchPaths[] = {"/shaders", "/includes"};

        slang::SessionDesc desc = {};
        desc.targets = &targetDesc;
        desc.targetCount = 1;
        desc.searchPaths = searchPaths;
        desc.searchPathCount = 2;
        desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

        std::string json = toJson(desc);

        CHECK(json.find("\"targetCount\":1") != std::string::npos);
        CHECK(json.find("\"searchPathCount\":2") != std::string::npos);
        CHECK(json.find("\"defaultMatrixLayoutMode\":\"SLANG_MATRIX_LAYOUT_COLUMN_MAJOR\"") != std::string::npos);
        CHECK(json.find("\"/shaders\"") != std::string::npos);
        CHECK(json.find("\"/includes\"") != std::string::npos);
    }

    SUBCASE("array-helpers")
    {
        // Empty array
        CHECK(toJsonArray(static_cast<const char* const*>(nullptr), 0) == "[]");

        // String array
        const char* strings[] = {"one", "two", "three"};
        std::string result = toJsonArray(strings, 3);
        CHECK(result.find("\"one\"") != std::string::npos);
        CHECK(result.find("\"two\"") != std::string::npos);
        CHECK(result.find("\"three\"") != std::string::npos);
    }

    SUBCASE("preprocessor-macro")
    {
        slang::PreprocessorMacroDesc macro;
        macro.name = "DEBUG";
        macro.value = "1";

        std::string json = toJson(macro);
        CHECK(json.find("\"name\":\"DEBUG\"") != std::string::npos);
        CHECK(json.find("\"value\":\"1\"") != std::string::npos);
    }
}
