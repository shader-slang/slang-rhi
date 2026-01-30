#pragma once

#include <slang.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace slang_capture {

/// JSON serialization utilities for Slang API capture.
/// These functions convert Slang types to JSON string representations.

// ============================================================================
// String Utilities
// ============================================================================

/// Escape a string for JSON output.
/// Handles quotes, backslashes, control characters, etc.
std::string escapeJsonString(const char* str);
std::string escapeJsonString(const std::string& str);

// ============================================================================
// Basic Types to JSON
// ============================================================================

inline std::string toJson(std::nullptr_t)
{
    return "null";
}

inline std::string toJson(bool value)
{
    return value ? "true" : "false";
}

inline std::string toJson(int value)
{
    return std::to_string(value);
}

inline std::string toJson(unsigned int value)
{
    return std::to_string(value);
}

inline std::string toJson(long value)
{
    return std::to_string(value);
}

inline std::string toJson(unsigned long value)
{
    return std::to_string(value);
}

inline std::string toJson(long long value)
{
    return std::to_string(value);
}

inline std::string toJson(unsigned long long value)
{
    return std::to_string(value);
}

inline std::string toJson(double value)
{
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

inline std::string toJson(const char* str)
{
    if (!str)
    {
        return "null";
    }
    return escapeJsonString(str);
}

inline std::string toJson(const std::string& str)
{
    return escapeJsonString(str);
}

/// Convert a void pointer to JSON (as hex string for debugging).
inline std::string toJson(const void* ptr)
{
    if (!ptr)
    {
        return "null";
    }
    std::ostringstream oss;
    oss << "\"0x" << std::hex << reinterpret_cast<uintptr_t>(ptr) << "\"";
    return oss.str();
}


// ============================================================================
// SlangResult to JSON
// ============================================================================

/// Convert SlangResult to a human-readable string.
const char* slangResultToString(SlangResult result);

/// Convert SlangResult to JSON string.
std::string slangResultToJson(SlangResult result);

// ============================================================================
// Slang Enums to JSON
// ============================================================================

/// Convert SlangCompileTarget to string.
const char* slangCompileTargetToString(SlangCompileTarget target);

/// Convert SlangCompileTarget to JSON string.
std::string slangCompileTargetToJson(SlangCompileTarget target);

/// Convert SlangMatrixLayoutMode to string.
const char* slangMatrixLayoutModeToString(SlangMatrixLayoutMode mode);

/// Convert SlangMatrixLayoutMode to JSON string.
std::string slangMatrixLayoutModeToJson(SlangMatrixLayoutMode mode);

/// Convert SlangFloatingPointMode to string.
const char* slangFloatingPointModeToString(SlangFloatingPointMode mode);

/// Convert SlangFloatingPointMode to JSON string.
std::string slangFloatingPointModeToJson(SlangFloatingPointMode mode);

/// Convert SlangLineDirectiveMode to string.
const char* slangLineDirectiveModeToString(SlangLineDirectiveMode mode);

/// Convert SlangLineDirectiveMode to JSON string.
std::string slangLineDirectiveModeToJson(SlangLineDirectiveMode mode);

/// Convert SlangStage to string.
const char* slangStageToString(SlangStage stage);

/// Convert SlangStage to JSON string.
std::string slangStageToJson(SlangStage stage);

// ============================================================================
// Slang Structs to JSON
// ============================================================================

/// Convert slang::TargetDesc to JSON.
std::string toJson(const slang::TargetDesc& desc);

/// Convert slang::SessionDesc to JSON.
std::string toJson(const slang::SessionDesc& desc);

/// Convert slang::PreprocessorMacroDesc to JSON.
std::string toJson(const slang::PreprocessorMacroDesc& macro);

// ============================================================================
// Array Helpers
// ============================================================================

/// Convert an array of strings to JSON.
std::string toJsonArray(const char* const* strings, size_t count);

/// Convert an array of TargetDesc to JSON.
std::string toJsonArray(const slang::TargetDesc* targets, size_t count);

/// Convert an array of PreprocessorMacroDesc to JSON.
std::string toJsonArray(const slang::PreprocessorMacroDesc* macros, size_t count);

} // namespace slang_capture
