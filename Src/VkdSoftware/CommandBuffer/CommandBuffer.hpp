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

	protected:
		VkResult Begin(const VkCommandBufferBeginInfo& beginInfo) override;
		VkResult End() override;
		VkResult Reset(VkCommandBufferResetFlags flags) override;

	private:
		// TODO: implement CPU command recording and execution
	};
}
