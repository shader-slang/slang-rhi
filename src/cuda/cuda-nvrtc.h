#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

enum nvrtcResult
{
    NVRTC_SUCCESS = 0,
    NVRTC_ERROR_OUT_OF_MEMORY = 1,
    NVRTC_ERROR_PROGRAM_CREATION_FAILURE = 2,
    NVRTC_ERROR_INVALID_INPUT = 3,
    NVRTC_ERROR_INVALID_PROGRAM = 4,
    NVRTC_ERROR_INVALID_OPTION = 5,
    NVRTC_ERROR_COMPILATION = 6,
    NVRTC_ERROR_BUILTIN_OPERATION_FAILURE = 7,
    NVRTC_ERROR_NO_NAME_EXPRESSIONS_AFTER_COMPILATION = 8,
    NVRTC_ERROR_NO_LOWERED_NAMES_BEFORE_COMPILATION = 9,
    NVRTC_ERROR_NAME_EXPRESSION_NOT_VALID = 10,
    NVRTC_ERROR_INTERNAL_ERROR = 11,
    NVRTC_ERROR_TIME_FILE_WRITE_FAILED = 12
};

typedef void* nvrtcProgram;

typedef const char*(nvrtcGetErrorStringFunc)(nvrtcResult result);
typedef nvrtcResult(nvrtcVersionFunc)(int* major, int* minor);
typedef nvrtcResult(nvrtcCreateProgramFunc)(
    nvrtcProgram* prog,
    const char* src,
    const char* name,
    int numHeaders,
    const char* const* headers,
    const char* const* includeNames
);
typedef nvrtcResult(nvrtcDestroyProgramFunc)(nvrtcProgram* prog);
typedef nvrtcResult(nvrtcCompileProgramFunc)(nvrtcProgram prog, int numOptions, const char* const* options);
typedef nvrtcResult(nvrtcGetPTXSizeFunc)(nvrtcProgram prog, size_t* ptxSizeRet);
typedef nvrtcResult(nvrtcGetPTXFunc)(nvrtcProgram prog, char* ptx);
typedef nvrtcResult(nvrtcGetProgramLogSizeFunc)(nvrtcProgram prog, size_t* logSizeRet);
typedef nvrtcResult(nvrtcGetProgramLogFunc)(nvrtcProgram prog, char* log);

/// NVRTC (NVIDIA Runtime Compiler) API wrapper
class NVRTC
{
public:
    NVRTC();
    ~NVRTC();

    Result initialize(IDebugCallback* debugCallback = nullptr);

    struct CompileResult
    {
        nvrtcResult result;
        std::string ptx;
        std::string log;
    };

    Result compilePTX(const char* source, CompileResult& outResult);

    // Raw NVRTC API
    nvrtcGetErrorStringFunc* nvrtcGetErrorString = nullptr;
    nvrtcVersionFunc* nvrtcVersion = nullptr;
    nvrtcCreateProgramFunc* nvrtcCreateProgram = nullptr;
    nvrtcDestroyProgramFunc* nvrtcDestroyProgram = nullptr;
    nvrtcCompileProgramFunc* nvrtcCompileProgram = nullptr;
    nvrtcGetPTXSizeFunc* nvrtcGetPTXSize = nullptr;
    nvrtcGetPTXFunc* nvrtcGetPTX = nullptr;
    nvrtcGetProgramLogSizeFunc* nvrtcGetProgramLogSize = nullptr;
    nvrtcGetProgramLogFunc* nvrtcGetProgramLog = nullptr;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace rhi::cuda
