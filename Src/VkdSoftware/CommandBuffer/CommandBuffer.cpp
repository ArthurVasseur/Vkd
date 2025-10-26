#include "CommandBuffer.hpp"

namespace vkd::software
{
	VkResult CommandBuffer::Begin(const VkCommandBufferBeginInfo& beginInfo)
	{
		VKD_AUTO_PROFILER_SCOPE;

		// TODO: implement CPU execution - begin recording commands
		return VK_SUCCESS;
	}

	VkResult CommandBuffer::End()
	{
		VKD_AUTO_PROFILER_SCOPE;

		// TODO: implement CPU execution - end recording commands
		return VK_SUCCESS;
	}

	VkResult CommandBuffer::Reset(VkCommandBufferResetFlags flags)
	{
		VKD_AUTO_PROFILER_SCOPE;

		// TODO: implement CPU execution - reset command buffer state
		return VK_SUCCESS;
	}
}
