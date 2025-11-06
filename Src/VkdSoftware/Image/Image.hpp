/**
 * @file Image.hpp
 * @brief Software renderer image implementation
 * @date 2025-11-06
 *
 * CPU-accessible image implementation for the software renderer.
 */

#pragma once

#include "Vkd/Image/Image.hpp"

namespace vkd::software
{
	class Image : public vkd::Image
	{
	public:
		Image() = default;
		~Image() override = default;
	};
}
