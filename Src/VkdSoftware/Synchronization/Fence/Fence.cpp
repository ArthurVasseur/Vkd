#include "Fence.hpp"

namespace vkd::software
{
	VkResult Fence::GetStatus()
	{
		// TODO: implement CPU execution - return fence status
		// Return VK_SUCCESS if signaled, VK_NOT_READY if not
		return VK_SUCCESS;
	}

	VkResult Fence::Wait(uint64_t timeout)
	{
		// TODO: implement CPU execution - wait for fence with timeout
		return VK_SUCCESS;
	}

	VkResult Fence::Reset()
	{
		// TODO: implement CPU execution - reset fence to unsignaled state
		return VK_SUCCESS;
	}
}
