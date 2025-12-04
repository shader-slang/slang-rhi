#pragma once

#include <slang-rhi.h>

#include "core/common.h"

#include <vector>

namespace rhi {

class SlangContext
{
public:
    ComPtr<slang::IGlobalSession> globalSession;
    ComPtr<slang::ISession> session;
    Result initialize(
        const SlangDesc& desc,
        SlangCompileTarget compileTarget,
        const char* defaultProfileName,
        span<const Capability> capabilities = {},
        span<const slang::PreprocessorMacroDesc> additionalPreprocessorMacros = {},
        span<const slang::CompilerOptionEntry> additionalCompilerOptions = {}
    )
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

        std::vector<slang::PreprocessorMacroDesc> preprocessorMacros;
        for (uint32_t i = 0; i < desc.preprocessorMacroCount; i++)
        {
            preprocessorMacros.push_back(desc.preprocessorMacros[i]);
        }
        for (size_t i = 0; i < additionalPreprocessorMacros.size(); i++)
        {
            preprocessorMacros.push_back(additionalPreprocessorMacros[i]);
        }
        slangSessionDesc.preprocessorMacros = preprocessorMacros.data();
        slangSessionDesc.preprocessorMacroCount = preprocessorMacros.size();

        std::vector<slang::CompilerOptionEntry> compilerOptions;
        for (Capability capability : capabilities)
        {
            const char* capabilityName = rhiGetInstance()->getCapabilityName(capability);
            SlangCapabilityID capabilityID = globalSession->findCapability(capabilityName);
            if (capabilityID == SLANG_CAPABILITY_UNKNOWN)
            {
                continue;
            }
            slang::CompilerOptionEntry entry = {};
            entry.name = slang::CompilerOptionName::Capability;
            entry.value.kind = slang::CompilerOptionValueKind::Int;
            entry.value.intValue0 = capabilityID;
            compilerOptions.push_back(entry);
        }
        for (uint32_t i = 0; i < desc.compilerOptionEntryCount; i++)
        {
            compilerOptions.push_back(desc.compilerOptionEntries[i]);
        }
        for (size_t i = 0; i < additionalCompilerOptions.size(); i++)
        {
            compilerOptions.push_back(additionalCompilerOptions[i]);
        }
        slangSessionDesc.compilerOptionEntries = compilerOptions.data();
        slangSessionDesc.compilerOptionEntryCount = compilerOptions.size();

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

        SLANG_RETURN_ON_FAIL(globalSession->createSession(slangSessionDesc, session.writeRef()));
        return SLANG_OK;
    }
};

} // namespace rhi
