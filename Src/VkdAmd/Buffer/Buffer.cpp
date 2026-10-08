#include "VkdAmd/Buffer/Buffer.hpp"

namespace vkd::amd
{
	void Buffer::GetMemoryRequirements(VkMemoryRequirements& memoryRequirements) const
	{
		vkd::Buffer::GetMemoryRequirements(memoryRequirements);
		memoryRequirements.alignment = 0x10000;
		memoryRequirements.size = (GetSize() + 0xFFFFull) & ~0xFFFFull;
		if (memoryRequirements.size == 0)
			memoryRequirements.size = 0x10000;
		memoryRequirements.memoryTypeBits = 0x1;
	}
} // namespace vkd::amd
