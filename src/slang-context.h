#pragma once

#include <slang-rhi.h>

#include "core/common.h"

#include <span>
#include <string>
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
        std::span<const Capability> capabilities = {},
        std::span<const slang::PreprocessorMacroDesc> additionalPreprocessorMacros = {},
        std::span<const slang::CompilerOptionEntry> additionalCompilerOptions = {}
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

        m_defaultMatrixLayoutMode = desc.defaultMatrixLayoutMode;
        m_searchPaths.clear();
        for (uint32_t i = 0; i < desc.searchPathCount; i++)
        {
            m_searchPaths.emplace_back(desc.searchPaths[i]);
        }

        m_preprocessorMacros.clear();
        for (uint32_t i = 0; i < desc.preprocessorMacroCount; i++)
        {
            addPreprocessorMacro(desc.preprocessorMacros[i]);
        }
        for (size_t i = 0; i < additionalPreprocessorMacros.size(); i++)
        {
            addPreprocessorMacro(additionalPreprocessorMacros[i]);
        }

        m_compilerOptions.clear();
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
            addCompilerOption(entry);
        }
        for (uint32_t i = 0; i < desc.compilerOptionEntryCount; i++)
        {
            addCompilerOption(desc.compilerOptionEntries[i]);
        }
        for (size_t i = 0; i < additionalCompilerOptions.size(); i++)
        {
            addCompilerOption(additionalCompilerOptions[i]);
        }

        m_targetDesc = {};
        m_targetDesc.format = compileTarget;
        auto targetProfile = desc.targetProfile;
        if (targetProfile == nullptr)
            targetProfile = defaultProfileName;
        m_targetDesc.profile = globalSession->findProfile(targetProfile);
        m_targetDesc.floatingPointMode = desc.floatingPointMode;
        m_targetDesc.lineDirectiveMode = desc.lineDirectiveMode;
        m_targetDesc.flags = desc.targetFlags;
        // WGSL uniform buffers require aligned aggregates. Forcing scalar layout here
        // gives synthesized entry-point buffers different offsets from WGSL reflection.
        m_targetDesc.forceGLSLScalarBufferLayout = compileTarget != SLANG_WGSL;

        return createSession(session.writeRef());
    }

    /// Creates a fresh session with the same target and compiler configuration as the device.
    Result createSession(slang::ISession** outSession) const
    {
        if (!outSession)
            return SLANG_E_INVALID_ARG;
        *outSession = nullptr;
        if (!globalSession)
            return SLANG_E_NOT_AVAILABLE;

        std::vector<const char*> searchPaths;
        searchPaths.reserve(m_searchPaths.size());
        for (const std::string& path : m_searchPaths)
            searchPaths.push_back(path.c_str());

        std::vector<slang::PreprocessorMacroDesc> preprocessorMacros;
        preprocessorMacros.reserve(m_preprocessorMacros.size());
        for (const OwnedPreprocessorMacro& owned : m_preprocessorMacros)
        {
            slang::PreprocessorMacroDesc macro = {};
            macro.name = owned.name.c_str();
            macro.value = owned.hasValue ? owned.value.c_str() : nullptr;
            preprocessorMacros.push_back(macro);
        }

        std::vector<slang::CompilerOptionEntry> compilerOptions;
        compilerOptions.reserve(m_compilerOptions.size());
        for (const OwnedCompilerOption& owned : m_compilerOptions)
        {
            slang::CompilerOptionEntry entry = owned.entry;
            if (entry.value.kind == slang::CompilerOptionValueKind::String)
            {
                entry.value.stringValue0 = owned.hasStringValue0 ? owned.stringValue0.c_str() : nullptr;
                entry.value.stringValue1 = owned.hasStringValue1 ? owned.stringValue1.c_str() : nullptr;
            }
            compilerOptions.push_back(entry);
        }

        slang::SessionDesc desc = {};
        desc.defaultMatrixLayoutMode = m_defaultMatrixLayoutMode;
        desc.searchPaths = searchPaths.data();
        desc.searchPathCount = SlangInt(searchPaths.size());
        desc.preprocessorMacros = preprocessorMacros.data();
        desc.preprocessorMacroCount = SlangInt(preprocessorMacros.size());
        desc.compilerOptionEntries = compilerOptions.data();
        desc.compilerOptionEntryCount = uint32_t(compilerOptions.size());
        desc.targets = &m_targetDesc;
        desc.targetCount = 1;
        return globalSession->createSession(desc, outSession);
    }

private:
    // SlangDesc only borrows its strings and arrays during device creation. Keep owned copies so
    // createSession() can reproduce the device session after those caller-owned values expire.
    struct OwnedPreprocessorMacro
    {
        std::string name;
        std::string value;
        bool hasValue = false;
    };

    struct OwnedCompilerOption
    {
        slang::CompilerOptionEntry entry;
        std::string stringValue0;
        std::string stringValue1;
        bool hasStringValue0 = false;
        bool hasStringValue1 = false;
    };

    void addPreprocessorMacro(const slang::PreprocessorMacroDesc& macro)
    {
        OwnedPreprocessorMacro owned;
        owned.name = macro.name ? macro.name : "";
        owned.hasValue = macro.value != nullptr;
        if (owned.hasValue)
            owned.value = macro.value;
        m_preprocessorMacros.push_back(std::move(owned));
    }

    void addCompilerOption(const slang::CompilerOptionEntry& entry)
    {
        OwnedCompilerOption owned;
        owned.entry = entry;
        if (entry.value.kind == slang::CompilerOptionValueKind::String)
        {
            owned.entry.value.stringValue0 = nullptr;
            owned.entry.value.stringValue1 = nullptr;
            owned.hasStringValue0 = entry.value.stringValue0 != nullptr;
            owned.hasStringValue1 = entry.value.stringValue1 != nullptr;
            if (owned.hasStringValue0)
                owned.stringValue0 = entry.value.stringValue0;
            if (owned.hasStringValue1)
                owned.stringValue1 = entry.value.stringValue1;
        }
        m_compilerOptions.push_back(std::move(owned));
    }

    SlangMatrixLayoutMode m_defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
    std::vector<std::string> m_searchPaths;
    std::vector<OwnedPreprocessorMacro> m_preprocessorMacros;
    std::vector<OwnedCompilerOption> m_compilerOptions;
    slang::TargetDesc m_targetDesc = {};
};

} // namespace rhi
