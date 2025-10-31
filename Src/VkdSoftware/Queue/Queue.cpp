/**
 * @file Queue.cpp
 * @brief Implementation of software renderer queue
 * @date 2025-10-27
 */

#include "VkdSoftware/Device/Device.hpp"
#include "VkdSoftware/Queue/Queue.hpp"
#include "VkdSoftware/CommandBuffer/CommandBuffer.hpp"
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

		auto* softwareDevice = static_cast<SoftwareDevice*>(GetOwner());
		auto& threadPool = softwareDevice->GetThreadPool();

		std::lock_guard<std::mutex> lock(m_submitMutex);
		auto previousSubmit = std::move(m_previousSubmit);

		m_previousSubmit = threadPool.Submit([cmdBuffers, fence, previousSubmit = std::move(previousSubmit)]() mutable -> bool
		{
			// Wait for the previous submit to complete before starting the new one
			if (previousSubmit.valid())
			{
				previousSubmit.wait();
			}

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
			return true;
		});

		return VK_SUCCESS;
	}

	VkResult Queue::WaitIdle()
	{
		VKD_AUTO_PROFILER_SCOPE();

		std::lock_guard<std::mutex> lock(m_submitMutex);

		// Wait for the previous submit to complete
		if (m_previousSubmit.valid())
		{
			m_previousSubmit.wait();
		}

		return VK_SUCCESS;
	}

	VkResult Queue::BindSparse(uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence)
	{
		VKD_AUTO_PROFILER_SCOPE();

		// Sparse binding is an optional feature, not implemented for software queue
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}
}
