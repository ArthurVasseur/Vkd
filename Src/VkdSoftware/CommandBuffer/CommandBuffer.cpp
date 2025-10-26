#include "CommandBuffer.hpp"

namespace vkd::software
{
	VkResult CommandBuffer::Begin(const VkCommandBufferBeginInfo& beginInfo)
	{
		VKD_AUTO_PROFILER_SCOPE;
		Transition(State::Recording, { State::Initial });

		return VK_SUCCESS;
	}

	VkResult CommandBuffer::End()
	{
		VKD_AUTO_PROFILER_SCOPE;
		Transition(State::Executable, { State::Recording });

		return VK_SUCCESS;
	}

	VkResult CommandBuffer::Reset(VkCommandBufferResetFlags flags)
	{
		VKD_AUTO_PROFILER_SCOPE;
		Transition(State::Initial, { State::Executable, State::Pending });

		return VK_SUCCESS;
	}
}
