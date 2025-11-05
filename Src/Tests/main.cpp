/**
 * @file Tests/main.cpp
 * @brief Unit tests entry point
 * @date 2025-10-31
 */

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>

int main(int argc, char const* const argv[])
{
	return Catch::Session().run(argc, argv);
}