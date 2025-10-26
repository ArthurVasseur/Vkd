//
// Created by arthur on 26/10/2025.
//

#pragma once

#include "Vkd/Buffer/Buffer.hpp"

namespace vkd::software
{
	class Buffer : public vkd::Buffer
	{
	public:
		Buffer();
		~Buffer() override = default;
	};
}
