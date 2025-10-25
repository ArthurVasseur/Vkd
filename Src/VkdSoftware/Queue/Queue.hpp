//
// Created by arthur on 25/10/2025.
//

#pragma once

#include "Vkd/Queue/Queue.hpp"

namespace vkd::software
{
	class Queue : public vkd::Queue
	{
	public:
		Queue() = default;
		~Queue() override = default;

		VkResult Create(Device& owner, uint32_t queueFamilyIndex, uint32_t queueIndex) override;

	protected:
		VkResult Submit(uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) override;
		VkResult WaitIdle() override;
		VkResult BindSparse(uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence) override;

	private:
		// Future: Add CPU rasterization pipeline, command buffer execution, etc.
	};
}
