#include "shader.hpp"

#include <stdexcept>

#include "spdlog/spdlog.h"

Shader::Shader(const std::string_view p_SearchPath, const std::span<const char*> p_ModuleNames)
{
    if (slang::createGlobalSession(m_GlobalSession.writeRef()) != SLANG_OK)
    {
        throw std::runtime_error("Failed to create Slang Global Session.");
    }

    slang::SessionDesc l_SessionDesc = {};
    slang::TargetDesc l_TargetDesc = {};
    l_TargetDesc.format = SLANG_SPIRV;
    l_TargetDesc.profile = m_GlobalSession->findProfile("spirv_1_6");

    l_SessionDesc.targets = &l_TargetDesc;
    l_SessionDesc.targetCount = 1;

    const char* searchPaths[] = { p_SearchPath.data() };
    l_SessionDesc.searchPaths = searchPaths;
    l_SessionDesc.searchPathCount = 1;

    if (m_GlobalSession->createSession(l_SessionDesc, m_Session.writeRef()) != SLANG_OK)
    {
        throw std::runtime_error("Failed to create Slang Session.");
    }

    Slang::ComPtr<slang::IBlob> l_DiagnosticBlob;

    m_Modules.reserve(p_ModuleNames.size());
	for (const char* p_ModuleName : p_ModuleNames)
    {
        Slang::ComPtr<slang::IModule> l_Module{m_Session->loadModule(p_ModuleName, l_DiagnosticBlob.writeRef())};

        if (l_DiagnosticBlob)
        {
            spdlog::error("Slang Parsing Diagnostics:\n{}", static_cast<const char*>(l_DiagnosticBlob->getBufferPointer()));
        }

        if (!l_Module)
        {
            throw std::runtime_error("Failed to load and parse Slang module: " + std::string(p_ModuleName));
        }

        m_Modules.emplace(std::string(p_ModuleName), l_Module);
    }
}

std::vector<uint32_t> Shader::getSpirv(const std::string_view p_EntryPointName, const std::string& p_Module) const
{
    Slang::ComPtr<slang::IEntryPoint> l_EntryPoint;
    SlangResult l_Res = m_Modules.at(p_Module)->findEntryPointByName(p_EntryPointName.data(), l_EntryPoint.writeRef());
    if (l_Res != SLANG_OK || !l_EntryPoint) 
    {
        throw std::runtime_error("Could not find entry point '" + std::string(p_EntryPointName) + "' in shader file.");
    }

    std::vector<slang::IComponentType*> l_ComponentTypes{};
    l_ComponentTypes.reserve(m_Modules.size() + 1);
	for (auto& [_, module] : m_Modules)
	{
		l_ComponentTypes.push_back(module.get());
	}
    l_ComponentTypes.push_back(l_EntryPoint.get());

    Slang::ComPtr<slang::IComponentType> l_ComposedProgram;
    Slang::ComPtr<slang::IBlob> l_DiagnosticBlob;

    l_Res = m_Session->createCompositeComponentType(l_ComponentTypes.data(), l_ComponentTypes.size(), l_ComposedProgram.writeRef(), l_DiagnosticBlob.writeRef());

    if (l_DiagnosticBlob) 
    {
        spdlog::error("Slang Linking Diagnostics:\n{}", static_cast<const char*>(l_DiagnosticBlob->getBufferPointer()));
    }

    if (l_Res != SLANG_OK) 
    {
        throw std::runtime_error("Failed to link entry point components for " + std::string(p_EntryPointName) + ": error " + std::to_string(l_Res));
    }

    Slang::ComPtr<slang::IBlob> l_SpirvBlob;
    l_Res = l_ComposedProgram->getEntryPointCode(0, 0, l_SpirvBlob.writeRef(), l_DiagnosticBlob.writeRef());

    if (l_DiagnosticBlob)
    {
        spdlog::error("Slang Code Generation Diagnostics:\n{}", static_cast<const char*>(l_DiagnosticBlob->getBufferPointer()));
    }

    if (l_Res != SLANG_OK || !l_SpirvBlob) 
    {
		throw std::runtime_error("Failed to generate SPIR-V bytecode for entry point " + std::string(p_EntryPointName) + ": error " + std::to_string(l_Res));
    }

    const uint32_t* spirvBegin = static_cast<const uint32_t*>(l_SpirvBlob->getBufferPointer());
    const size_t dwordCount = l_SpirvBlob->getBufferSize() / sizeof(uint32_t);

    return { spirvBegin, spirvBegin + dwordCount };
}
