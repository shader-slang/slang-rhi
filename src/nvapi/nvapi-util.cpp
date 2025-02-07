#include "nvapi-util.h"
#include "nvapi-include.h"

#include "core/common.h"

namespace rhi {

static Result g_initStatus = SLANG_E_UNINITIALIZED;

Result NVAPIUtil::initialize()
{
#if SLANG_RHI_ENABLE_NVAPI
    if (g_initStatus == SLANG_E_UNINITIALIZED)
    {
        NvAPI_Status ret = NVAPI_OK;
        ret = NvAPI_Initialize();
        g_initStatus = (ret == NVAPI_OK) ? SLANG_OK : SLANG_E_NOT_AVAILABLE;
    }
#else
    g_initStatus = SLANG_E_NOT_AVAILABLE;
#endif

    return g_initStatus;
}

bool NVAPIUtil::isAvailable()
{
    return SLANG_SUCCEEDED(g_initStatus);
}

NVAPIShaderExtension NVAPIUtil::findShaderExtension(slang::ProgramLayout* layout)
{
#if SLANG_RHI_ENABLE_NVAPI
    if (!layout || !NVAPIUtil::isAvailable())
        return {};
    slang::TypeLayoutReflection* globalTypeLayout = layout->getGlobalParamsVarLayout()->getTypeLayout();
    int index = globalTypeLayout->findFieldIndexByName("g_NvidiaExt");
    if (index != -1)
    {
        slang::VariableLayoutReflection* field = globalTypeLayout->getFieldByIndex((unsigned int)index);
        return {field->getBindingSpace(), field->getBindingIndex()};
    }
#endif
    return {};
}

Result NVAPIUtil::handleFail(int res, const char* file, int line, const char* call)
{
#if SLANG_RHI_ENABLE_NVAPI
#ifdef _DEBUG
    NvAPI_ShortString msg;
    NvAPI_GetErrorMessage(NvAPI_Status(res), msg);
    printf("%s returned error %s (%d)\n", call, msg, res);
    printf("at %s:%d\n", file, line);
#endif
    SLANG_RHI_ASSERT_FAILURE("NVAPI returned an error");
#endif
    return SLANG_FAIL;
}

} // namespace rhi
