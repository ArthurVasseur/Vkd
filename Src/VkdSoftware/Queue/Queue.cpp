//
// Created by arthur on 27/10/2025.
//

#include "VkdSoftware/Queue/Queue.hpp"
#include "VkdSoftware/CommandBuffer/CommandBuffer.hpp"
#include "Vkd/Defines.hpp"
#include "VkdSoftware/CommandDispatcher/CommandDispatcher.hpp"
#include "VkdSoftware/CpuContext/CpuContext.hpp"
#include "VkdSoftware/Synchronization/Fence/Fence.hpp"

namespace vkd::software
{
	VkResult Queue::Create(Device& owner, uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags)
	{
		return vkd::Queue::Create(owner, queueFamilyIndex, queueIndex, flags);
	}

	VkResult Queue::Submit(uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
	{
		VKD_AUTO_PROFILER_SCOPE();
		VKD_CHECK(submitCount && pSubmits);

		std::vector<vkd::CommandBuffer*> cmdBuffers;
		cmdBuffers.resize(pSubmits->commandBufferCount);
		for (std::size_t i = 0; i < pSubmits->commandBufferCount; ++i)
		{
			VKD_FROM_HANDLE(vkd::CommandBuffer, cmdBufferObj, pSubmits->pCommandBuffers[i]);
			cmdBuffers[i] = cmdBufferObj;
		}

		std::thread thread([cmdBuffers, fence]() //TODO: ThreadPool
		{
			for (auto* cmdBufferObj : cmdBuffers)
			{
				CpuContext cpuContext;
				CommandDispatcher commandDispatcher(cpuContext);
				commandDispatcher.Execute(*cmdBufferObj);
			}

			if (fence)
			{
				VKD_FROM_HANDLE(vkd::Fence, fenceObj, fence);
				fenceObj->Signal();
			}
		});

		thread.detach(); // TODO: Big problem

		return VK_SUCCESS;
	}

	VkResult Queue::WaitIdle()
	{
		VKD_AUTO_PROFILER_SCOPE();

		// TODO: implement waiting for all queue operations to complete
		// For now, since we execute synchronously, this is a no-op
		return VK_SUCCESS;
	}

	VkResult Queue::BindSparse(uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence)
	{
		VKD_AUTO_PROFILER_SCOPE();

		// Sparse binding is an optional feature, not implemented for software queue
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}
}
