/**
 * @file CommandBuffer.hpp
 * @brief Software renderer command buffer implementation
 * @date 2025-10-25
 *
 * Command buffer implementation that records draw calls for CPU execution.
 */

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
} // namespace vkd::software

#include "VkdSoftware/CommandBuffer/CommandBuffer.inl"
