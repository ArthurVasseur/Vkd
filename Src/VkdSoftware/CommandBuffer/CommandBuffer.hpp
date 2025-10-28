//
// Created by arthur on 25/10/2025.
//

#pragma once

#include "Vkd/CommandBuffer/CommandBuffer.hpp"

namespace vkd::software
{
	class CommandBuffer : public vkd::CommandBuffer
	{
	public:
		CommandBuffer() = default;
		~CommandBuffer() override = default;
	};
}

#include "VkdSoftware/CommandBuffer/CommandBuffer.inl"
