#pragma once

#include "slang-gfx.h"
#include "core/slang-basic.h"

namespace gfx
{
    class SlangContext
    {
    public:
        Slang::ComPtr<slang::IGlobalSession> globalSession;
        Slang::ComPtr<slang::ISession> session;
        Result initialize(const gfx::IDevice::SlangDesc& desc,
            uint32_t extendedDescCount,
            void** extendedDescs,
            SlangCompileTarget compileTarget,
            const char* defaultProfileName,
            Slang::ConstArrayView<slang::PreprocessorMacroDesc> additionalMacros)
        {
            if (desc.slangGlobalSession)
            {
                globalSession = desc.slangGlobalSession;
            }
            else
            {
                SLANG_RETURN_ON_FAIL(slang::createGlobalSession(globalSession.writeRef()));
            }

            slang::SessionDesc slangSessionDesc = {};
            slangSessionDesc.defaultMatrixLayoutMode = desc.defaultMatrixLayoutMode;
            slangSessionDesc.searchPathCount = desc.searchPathCount;
            slangSessionDesc.searchPaths = desc.searchPaths;
            slangSessionDesc.preprocessorMacroCount = desc.preprocessorMacroCount + additionalMacros.getCount();
            std::vector<slang::PreprocessorMacroDesc> macros;
            for (GfxCount i = 0; i < desc.preprocessorMacroCount; i++)
            {
                macros.push_back(desc.preprocessorMacros[i]);
            }
            for (GfxCount i = 0; i < additionalMacros.getCount(); i++)
            {
                macros.push_back(additionalMacros[i]);
            }
            slangSessionDesc.preprocessorMacros = macros.data();
            slang::TargetDesc targetDesc = {};
            targetDesc.format = compileTarget;
            auto targetProfile = desc.targetProfile;
            if (targetProfile == nullptr)
                targetProfile = defaultProfileName;
            targetDesc.profile = globalSession->findProfile(targetProfile);
            targetDesc.floatingPointMode = desc.floatingPointMode;
            targetDesc.lineDirectiveMode = desc.lineDirectiveMode;
            targetDesc.flags = desc.targetFlags;
            targetDesc.forceGLSLScalarBufferLayout = true;

            slangSessionDesc.targets = &targetDesc;
            slangSessionDesc.targetCount = 1;

            for (uint32_t i = 0; i < extendedDescCount; i++)
            {
                if ((*(StructType*)extendedDescs[i]) == StructType::SlangSessionExtendedDesc)
                {
                    auto extDesc = (SlangSessionExtendedDesc*)extendedDescs[i];
                    slangSessionDesc.compilerOptionEntryCount = extDesc->compilerOptionEntryCount;
                    slangSessionDesc.compilerOptionEntries = extDesc->compilerOptionEntries;
                    break;
                }
            }

            SLANG_RETURN_ON_FAIL(globalSession->createSession(slangSessionDesc, session.writeRef()));
            return SLANG_OK;
        }
    };
}
