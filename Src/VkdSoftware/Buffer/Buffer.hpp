/**
 * @file Buffer.hpp
 * @brief Software renderer buffer implementation
 * @date 2025-10-26
 *
 * CPU-accessible buffer implementation for the software renderer.
 */

#pragma once

#include "Vkd/Buffer/Buffer.hpp"

namespace vkd::software
{
	class Buffer : public vkd::Buffer
	{
	public:
		Buffer() = default;
		~Buffer() override = default;
	};
}
