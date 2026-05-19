#pragma once
#include <functional>

template<typename... Args>
class Signal
{
public:
	using Func = std::function<void(Args...)>;

	Signal()= default;

	void connect(const Func& p_Func)
	{
		m_Funcs.push_back(p_Func);
	}

	template <typename T>
	void connect(T* p_Instance, void (T::* p_Method)(Args...))
	{
		Func l_Func = [=](Args... p_Args) { return (p_Instance->*p_Method)(p_Args...); };
		m_Funcs.push_back(l_Func);
	}

	void emit(Args... p_Args)
	{
		for (const Func& l_Func : m_Funcs)
		{
			l_Func(p_Args...);
		}
	}

private:
	std::vector<Func> m_Funcs;
};

