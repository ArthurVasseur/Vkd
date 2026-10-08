#pragma once

#include "Vkd/Buffer/Buffer.hpp"

namespace vkd::amd
{
	class Buffer : public vkd::Buffer
	{
	public:
		using vkd::Buffer::Buffer;

		VkResult Create(Device& owner, const VkBufferCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks) override;
		void GetMemoryRequirements(VkMemoryRequirements& memoryRequirements) const override;
	};
} // namespace vkd::amd
