#pragma once
#include <string>
#include <vector>

#include <slang/slang.h>
#include <slang/slang-com-ptr.h>

class Shader
{
public:
	explicit Shader(std::string_view p_FilePath);

	std::vector<uint32_t> getSpirv(std::string_view p_EntryPointName) const;

private:
	Slang::ComPtr<slang::IGlobalSession> m_GlobalSession;
	Slang::ComPtr<slang::ISession> m_Session;
	Slang::ComPtr<slang::IModule> m_Module;
};

