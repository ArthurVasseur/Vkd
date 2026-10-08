#pragma once

#include "Vkd/Buffer/Buffer.hpp"

namespace vkd::amd
{
	class Buffer : public vkd::Buffer
	{
	public:
		using vkd::Buffer::Buffer;

		void GetMemoryRequirements(VkMemoryRequirements& memoryRequirements) const override;
	};
} // namespace vkd::amd
