#pragma once
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <slang/slang.h>
#include <slang/slang-com-ptr.h>

class Shader
{
public:
	explicit Shader(std::string_view p_SearchPath, std::span<const char*> p_ModuleNames);

	std::vector<uint32_t> getSpirv(std::string_view p_EntryPointName, const std::string& p_Module) const;

private:
	Slang::ComPtr<slang::IGlobalSession> m_GlobalSession;
	Slang::ComPtr<slang::ISession> m_Session;
	std::unordered_map<std::string, Slang::ComPtr<slang::IModule>> m_Modules;

};

