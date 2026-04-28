/**
 * @file Tests/main.cpp
 * @brief Unit tests entry point
 * @date 2025-10-31
 */

#include "Concerto/Core/Logger/Logger.hpp"
#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>

int main(int argc, char const* const argv[])
{
	cct::Logger logger;
	cct::Logger::SetContext(&logger);
	return Catch::Session().run(argc, argv);
}