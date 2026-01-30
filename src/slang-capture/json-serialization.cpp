#include "json-serialization.h"

#include <iomanip>
#include <sstream>

namespace slang_capture {

// ============================================================================
// String Utilities
// ============================================================================

std::string escapeJsonString(const char* str)
{
    if (!str)
    {
        return "null";
    }

    std::ostringstream oss;
    oss << '"';

    for (const char* p = str; *p; ++p)
    {
        char c = *p;
        switch (c)
        {
        case '"':
            oss << "\\\"";
            break;
        case '\\':
            oss << "\\\\";
            break;
        case '\b':
            oss << "\\b";
            break;
        case '\f':
            oss << "\\f";
            break;
        case '\n':
            oss << "\\n";
            break;
        case '\r':
            oss << "\\r";
            break;
        case '\t':
            oss << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                // Control character - escape as \uXXXX
                oss << "\\u" << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>(c);
            }
            else
            {
                oss << c;
            }
            break;
        }
    }

    oss << '"';
    return oss.str();
}

std::string escapeJsonString(const std::string& str)
{
    return escapeJsonString(str.c_str());
}

// ============================================================================
// SlangResult to JSON
// ============================================================================

const char* slangResultToString(SlangResult result)
{
    if (SLANG_SUCCEEDED(result))
    {
        return "SLANG_OK";
    }

    switch (result)
    {
    case SLANG_E_NOT_IMPLEMENTED:
        return "SLANG_E_NOT_IMPLEMENTED";
    case SLANG_E_NO_INTERFACE:
        return "SLANG_E_NO_INTERFACE";
    case SLANG_E_ABORT:
        return "SLANG_E_ABORT";
    case SLANG_E_INVALID_HANDLE:
        return "SLANG_E_INVALID_HANDLE";
    case SLANG_E_INVALID_ARG:
        return "SLANG_E_INVALID_ARG";
    case SLANG_E_OUT_OF_MEMORY:
        return "SLANG_E_OUT_OF_MEMORY";
    case SLANG_E_BUFFER_TOO_SMALL:
        return "SLANG_E_BUFFER_TOO_SMALL";
    case SLANG_E_UNINITIALIZED:
        return "SLANG_E_UNINITIALIZED";
    case SLANG_E_PENDING:
        return "SLANG_E_PENDING";
    case SLANG_E_CANNOT_OPEN:
        return "SLANG_E_CANNOT_OPEN";
    case SLANG_E_NOT_FOUND:
        return "SLANG_E_NOT_FOUND";
    case SLANG_E_INTERNAL_FAIL:
        return "SLANG_E_INTERNAL_FAIL";
    case SLANG_E_NOT_AVAILABLE:
        return "SLANG_E_NOT_AVAILABLE";
    case SLANG_E_TIME_OUT:
        return "SLANG_E_TIME_OUT";
    default:
        return "SLANG_FAIL";
    }
}

// ============================================================================
// Slang Enums to JSON
// ============================================================================

const char* slangCompileTargetToString(SlangCompileTarget target)
{
    switch (target)
    {
    case SLANG_TARGET_UNKNOWN:
        return "SLANG_TARGET_UNKNOWN";
    case SLANG_TARGET_NONE:
        return "SLANG_TARGET_NONE";
    case SLANG_GLSL:
        return "SLANG_GLSL";
    case SLANG_HLSL:
        return "SLANG_HLSL";
    case SLANG_SPIRV:
        return "SLANG_SPIRV";
    case SLANG_SPIRV_ASM:
        return "SLANG_SPIRV_ASM";
    case SLANG_DXBC:
        return "SLANG_DXBC";
    case SLANG_DXBC_ASM:
        return "SLANG_DXBC_ASM";
    case SLANG_DXIL:
        return "SLANG_DXIL";
    case SLANG_DXIL_ASM:
        return "SLANG_DXIL_ASM";
    case SLANG_C_SOURCE:
        return "SLANG_C_SOURCE";
    case SLANG_CPP_SOURCE:
        return "SLANG_CPP_SOURCE";
    case SLANG_HOST_EXECUTABLE:
        return "SLANG_HOST_EXECUTABLE";
    case SLANG_SHADER_SHARED_LIBRARY:
        return "SLANG_SHADER_SHARED_LIBRARY";
    case SLANG_SHADER_HOST_CALLABLE:
        return "SLANG_SHADER_HOST_CALLABLE";
    case SLANG_CUDA_SOURCE:
        return "SLANG_CUDA_SOURCE";
    case SLANG_PTX:
        return "SLANG_PTX";
    case SLANG_CUDA_OBJECT_CODE:
        return "SLANG_CUDA_OBJECT_CODE";
    case SLANG_OBJECT_CODE:
        return "SLANG_OBJECT_CODE";
    case SLANG_HOST_CPP_SOURCE:
        return "SLANG_HOST_CPP_SOURCE";
    case SLANG_HOST_HOST_CALLABLE:
        return "SLANG_HOST_HOST_CALLABLE";
    case SLANG_CPP_PYTORCH_BINDING:
        return "SLANG_CPP_PYTORCH_BINDING";
    case SLANG_METAL:
        return "SLANG_METAL";
    case SLANG_METAL_LIB:
        return "SLANG_METAL_LIB";
    case SLANG_METAL_LIB_ASM:
        return "SLANG_METAL_LIB_ASM";
    case SLANG_HOST_SHARED_LIBRARY:
        return "SLANG_HOST_SHARED_LIBRARY";
    case SLANG_WGSL:
        return "SLANG_WGSL";
    default:
        return "SLANG_TARGET_UNKNOWN";
    }
}

const char* slangMatrixLayoutModeToString(SlangMatrixLayoutMode mode)
{
    switch (mode)
    {
    case SLANG_MATRIX_LAYOUT_MODE_UNKNOWN:
        return "SLANG_MATRIX_LAYOUT_MODE_UNKNOWN";
    case SLANG_MATRIX_LAYOUT_ROW_MAJOR:
        return "SLANG_MATRIX_LAYOUT_ROW_MAJOR";
    case SLANG_MATRIX_LAYOUT_COLUMN_MAJOR:
        return "SLANG_MATRIX_LAYOUT_COLUMN_MAJOR";
    default:
        return "SLANG_MATRIX_LAYOUT_MODE_UNKNOWN";
    }
}

const char* slangFloatingPointModeToString(SlangFloatingPointMode mode)
{
    switch (mode)
    {
    case SLANG_FLOATING_POINT_MODE_DEFAULT:
        return "SLANG_FLOATING_POINT_MODE_DEFAULT";
    case SLANG_FLOATING_POINT_MODE_FAST:
        return "SLANG_FLOATING_POINT_MODE_FAST";
    case SLANG_FLOATING_POINT_MODE_PRECISE:
        return "SLANG_FLOATING_POINT_MODE_PRECISE";
    default:
        return "SLANG_FLOATING_POINT_MODE_DEFAULT";
    }
}

const char* slangLineDirectiveModeToString(SlangLineDirectiveMode mode)
{
    switch (mode)
    {
    case SLANG_LINE_DIRECTIVE_MODE_DEFAULT:
        return "SLANG_LINE_DIRECTIVE_MODE_DEFAULT";
    case SLANG_LINE_DIRECTIVE_MODE_NONE:
        return "SLANG_LINE_DIRECTIVE_MODE_NONE";
    case SLANG_LINE_DIRECTIVE_MODE_STANDARD:
        return "SLANG_LINE_DIRECTIVE_MODE_STANDARD";
    case SLANG_LINE_DIRECTIVE_MODE_GLSL:
        return "SLANG_LINE_DIRECTIVE_MODE_GLSL";
    case SLANG_LINE_DIRECTIVE_MODE_SOURCE_MAP:
        return "SLANG_LINE_DIRECTIVE_MODE_SOURCE_MAP";
    default:
        return "SLANG_LINE_DIRECTIVE_MODE_DEFAULT";
    }
}

const char* slangStageToString(SlangStage stage)
{
    switch (stage)
    {
    case SLANG_STAGE_NONE:
        return "SLANG_STAGE_NONE";
    case SLANG_STAGE_VERTEX:
        return "SLANG_STAGE_VERTEX";
    case SLANG_STAGE_HULL:
        return "SLANG_STAGE_HULL";
    case SLANG_STAGE_DOMAIN:
        return "SLANG_STAGE_DOMAIN";
    case SLANG_STAGE_GEOMETRY:
        return "SLANG_STAGE_GEOMETRY";
    case SLANG_STAGE_FRAGMENT:
        return "SLANG_STAGE_FRAGMENT";
    case SLANG_STAGE_COMPUTE:
        return "SLANG_STAGE_COMPUTE";
    case SLANG_STAGE_RAY_GENERATION:
        return "SLANG_STAGE_RAY_GENERATION";
    case SLANG_STAGE_INTERSECTION:
        return "SLANG_STAGE_INTERSECTION";
    case SLANG_STAGE_ANY_HIT:
        return "SLANG_STAGE_ANY_HIT";
    case SLANG_STAGE_CLOSEST_HIT:
        return "SLANG_STAGE_CLOSEST_HIT";
    case SLANG_STAGE_MISS:
        return "SLANG_STAGE_MISS";
    case SLANG_STAGE_CALLABLE:
        return "SLANG_STAGE_CALLABLE";
    case SLANG_STAGE_MESH:
        return "SLANG_STAGE_MESH";
    case SLANG_STAGE_AMPLIFICATION:
        return "SLANG_STAGE_AMPLIFICATION";
    default:
        return "SLANG_STAGE_NONE";
    }
}

// ============================================================================
// Enum to JSON functions
// ============================================================================

std::string slangResultToJson(SlangResult result)
{
    return std::string("\"") + slangResultToString(result) + "\"";
}

std::string slangCompileTargetToJson(SlangCompileTarget target)
{
    return std::string("\"") + slangCompileTargetToString(target) + "\"";
}

std::string slangMatrixLayoutModeToJson(SlangMatrixLayoutMode mode)
{
    return std::string("\"") + slangMatrixLayoutModeToString(mode) + "\"";
}

std::string slangFloatingPointModeToJson(SlangFloatingPointMode mode)
{
    return std::string("\"") + slangFloatingPointModeToString(mode) + "\"";
}

std::string slangLineDirectiveModeToJson(SlangLineDirectiveMode mode)
{
    return std::string("\"") + slangLineDirectiveModeToString(mode) + "\"";
}

std::string slangStageToJson(SlangStage stage)
{
    return std::string("\"") + slangStageToString(stage) + "\"";
}

// ============================================================================
// Slang Structs to JSON
// ============================================================================

std::string toJson(const slang::PreprocessorMacroDesc& macro)
{
    std::ostringstream oss;
    oss << "{";
    oss << "\"name\":" << toJson(macro.name);
    oss << ",\"value\":" << toJson(macro.value);
    oss << "}";
    return oss.str();
}

std::string toJson(const slang::TargetDesc& desc)
{
    std::ostringstream oss;
    oss << "{";
    oss << "\"format\":" << slangCompileTargetToJson(desc.format);
    oss << ",\"profile\":" << toJson(static_cast<int>(desc.profile));
    oss << ",\"flags\":" << toJson(static_cast<unsigned int>(desc.flags));
    oss << ",\"floatingPointMode\":" << slangFloatingPointModeToJson(desc.floatingPointMode);
    oss << ",\"lineDirectiveMode\":" << slangLineDirectiveModeToJson(desc.lineDirectiveMode);
    oss << ",\"forceGLSLScalarBufferLayout\":" << toJson(desc.forceGLSLScalarBufferLayout);
    oss << ",\"compilerOptionEntryCount\":" << toJson(static_cast<int>(desc.compilerOptionEntryCount));
    oss << "}";
    return oss.str();
}

std::string toJson(const slang::SessionDesc& desc)
{
    std::ostringstream oss;
    oss << "{";
    oss << "\"targets\":" << toJsonArray(desc.targets, static_cast<size_t>(desc.targetCount));
    oss << ",\"targetCount\":" << toJson(static_cast<int>(desc.targetCount));
    oss << ",\"flags\":" << toJson(static_cast<unsigned int>(desc.flags));
    oss << ",\"defaultMatrixLayoutMode\":" << slangMatrixLayoutModeToJson(desc.defaultMatrixLayoutMode);
    oss << ",\"searchPaths\":" << toJsonArray(desc.searchPaths, static_cast<size_t>(desc.searchPathCount));
    oss << ",\"searchPathCount\":" << toJson(static_cast<int>(desc.searchPathCount));
    oss << ",\"preprocessorMacros\":"
        << toJsonArray(desc.preprocessorMacros, static_cast<size_t>(desc.preprocessorMacroCount));
    oss << ",\"preprocessorMacroCount\":" << toJson(static_cast<int>(desc.preprocessorMacroCount));
    oss << ",\"enableEffectAnnotations\":" << toJson(desc.enableEffectAnnotations);
    oss << ",\"allowGLSLSyntax\":" << toJson(desc.allowGLSLSyntax);
    oss << "}";
    return oss.str();
}

// ============================================================================
// Array Helpers
// ============================================================================

std::string toJsonArray(const char* const* strings, size_t count)
{
    if (!strings || count == 0)
    {
        return "[]";
    }

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < count; ++i)
    {
        if (i > 0)
        {
            oss << ",";
        }
        oss << toJson(strings[i]);
    }
    oss << "]";
    return oss.str();
}

std::string toJsonArray(const slang::TargetDesc* targets, size_t count)
{
    if (!targets || count == 0)
    {
        return "[]";
    }

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < count; ++i)
    {
        if (i > 0)
        {
            oss << ",";
        }
        oss << toJson(targets[i]);
    }
    oss << "]";
    return oss.str();
}

std::string toJsonArray(const slang::PreprocessorMacroDesc* macros, size_t count)
{
    if (!macros || count == 0)
    {
        return "[]";
    }

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < count; ++i)
    {
        if (i > 0)
        {
            oss << ",";
        }
        oss << toJson(macros[i]);
    }
    oss << "]";
    return oss.str();
}

} // namespace slang_capture
