#include <spdlog/spdlog.h>

#include "engine/engine.hpp"

int main(int, char**)
{
#ifndef NDEBUG
	spdlog::set_level(spdlog::level::debug);
#else
	spdlog::set_level(spdlog::level::info);
#endif
	spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

	Engine engine{};
#ifndef NDEBUG
	engine.init(true);
#else
	try
	{
		engine.init(false);
	}
	catch (const std::exception& e)
	{
		spdlog::error("Failed to initialize engine");
		return -1;
	}
#endif

	engine.run();

	engine.destroy();
}
