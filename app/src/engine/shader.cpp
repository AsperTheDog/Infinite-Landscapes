#include "shader.hpp"

#include <stdexcept>

#include "spdlog/spdlog.h"

Shader::Shader(const std::string_view p_FilePath)
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

    if (m_GlobalSession->createSession(l_SessionDesc, m_Session.writeRef()) != SLANG_OK) 
    {
        throw std::runtime_error("Failed to create Slang Session.");
    }

    Slang::ComPtr<slang::IBlob> l_DiagnosticBlob;
    m_Module = m_Session->loadModule(p_FilePath.data(), l_DiagnosticBlob.writeRef());

    if (l_DiagnosticBlob) 
    {
        spdlog::error("Slang Parsing Diagnostics:\n{}", static_cast<const char*>(l_DiagnosticBlob->getBufferPointer()));
    }

    if (!m_Module) 
    {
        throw std::runtime_error("Failed to load and parse Slang file: " + std::string(p_FilePath));
    }
}

std::vector<uint32_t> Shader::getSpirv(const std::string_view p_EntryPointName) const
{
    Slang::ComPtr<slang::IEntryPoint> l_EntryPoint;
    SlangResult l_Res = m_Module->findEntryPointByName(p_EntryPointName.data(), l_EntryPoint.writeRef());
    if (l_Res != SLANG_OK || !l_EntryPoint) 
    {
        throw std::runtime_error("Could not find entry point '" + std::string(p_EntryPointName) + "' in shader file.");
    }

    slang::IComponentType* l_ComponentTypes[] = { m_Module.get(), l_EntryPoint.get() };

    Slang::ComPtr<slang::IComponentType> l_ComposedProgram;
    Slang::ComPtr<slang::IBlob> l_DiagnosticBlob;

    l_Res = m_Session->createCompositeComponentType(l_ComponentTypes, 2, l_ComposedProgram.writeRef(), l_DiagnosticBlob.writeRef());

    if (l_DiagnosticBlob) 
    {
        spdlog::error("Slang Linking Diagnostics:\n{}", static_cast<const char*>(l_DiagnosticBlob->getBufferPointer()));
    }

    if (l_Res != SLANG_OK) 
    {
        throw std::runtime_error("Failed to link entry point components for: " + std::string(p_EntryPointName));
    }

    Slang::ComPtr<slang::IBlob> l_SpirvBlob;
    l_Res = l_ComposedProgram->getEntryPointCode(0, 0, l_SpirvBlob.writeRef(), l_DiagnosticBlob.writeRef());

    if (l_Res != SLANG_OK || !l_SpirvBlob) 
    {
        throw std::runtime_error("Failed to generate SPIR-V bytecode for entry point: " + std::string(p_EntryPointName));
    }

    const uint32_t* spirvBegin = static_cast<const uint32_t*>(l_SpirvBlob->getBufferPointer());
    const size_t dwordCount = l_SpirvBlob->getBufferSize() / sizeof(uint32_t);

    return { spirvBegin, spirvBegin + dwordCount };
}
