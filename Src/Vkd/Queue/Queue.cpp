/**
 * @file Queue.cpp
 * @brief Implementation of Vulkan queue
 * @date 2025-10-27
 */

#include "Vkd/Queue/Queue.hpp"

#include "Vkd/Device/Device.hpp"

namespace vkd
{
	VkResult Queue::QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Queue, queueObj, queue);
		if (!queueObj)
		{
			CCT_ASSERT_FALSE("Invalid VkQueue handle");
			return VK_ERROR_DEVICE_LOST;
		}

		return queueObj->Submit(submitCount, pSubmits, fence);
	}

	VkResult Queue::QueueWaitIdle(VkQueue queue)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Queue, queueObj, queue);
		if (!queueObj)
		{
			CCT_ASSERT_FALSE("Invalid VkQueue handle");
			return VK_ERROR_DEVICE_LOST;
		}

		return queueObj->WaitIdle();
	}

	VkResult Queue::QueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Queue, queueObj, queue);
		if (!queueObj)
		{
			CCT_ASSERT_FALSE("Invalid VkQueue handle");
			return VK_ERROR_DEVICE_LOST;
		}

		return queueObj->BindSparse(bindInfoCount, pBindInfo, fence);
	}
} // namespace vkd
