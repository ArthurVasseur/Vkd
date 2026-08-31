/**
 * @file Queue.cpp
 * @brief Implementation of software renderer queue
 * @date 2025-10-27
 */

#include "VkdSoftware/Queue/Queue.hpp"

#include "VkdSoftware/CommandBuffer/CommandBuffer.hpp"
#include "VkdSoftware/CommandDispatcher/CommandDispatcher.hpp"
#include "VkdSoftware/CpuContext/CpuContext.hpp"
#include "VkdSoftware/Device/Device.hpp"
#include "VkdSoftware/Synchronization/Fence/Fence.hpp"
#include "VkdSoftware/Synchronization/Semaphore/Semaphore.hpp"

namespace vkd::software
{
	VkResult Queue::Create(Device& owner, uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags)
	{
		return vkd::Queue::Create(owner, queueFamilyIndex, queueIndex, flags);
	}

	VkResult Queue::Submit(uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
	{
		VKD_AUTO_PROFILER_SCOPE();
		if (submitCount > 0)
			VKD_CHECK(pSubmits);

		struct Batch
		{
			vkd::vector<vkd::Semaphore*> waitSemaphores;
			vkd::vector<vkd::CommandBuffer*> commandBuffers;
			vkd::vector<vkd::Semaphore*> signalSemaphores;
		};

		vkd::vector<Batch> batches(CommandAllocator<Batch>(GetAllocationCallbacks()));
		batches.reserve(submitCount);
		for (uint32_t s = 0; s < submitCount; ++s)
		{
			const VkSubmitInfo& submit = pSubmits[s];
			Batch batch{
				.waitSemaphores = vkd::vector<vkd::Semaphore*>(CommandAllocator<vkd::Semaphore*>(GetAllocationCallbacks())),
				.commandBuffers = vkd::vector<vkd::CommandBuffer*>(CommandAllocator<vkd::CommandBuffer*>(GetAllocationCallbacks())),
				.signalSemaphores = vkd::vector<vkd::Semaphore*>(CommandAllocator<vkd::Semaphore*>(GetAllocationCallbacks())),
			};

			batch.waitSemaphores.reserve(submit.waitSemaphoreCount);
			for (uint32_t i = 0; i < submit.waitSemaphoreCount; ++i)
			{
				VKD_FROM_HANDLE(vkd::Semaphore, semaphoreObj, submit.pWaitSemaphores[i]);
				batch.waitSemaphores.push_back(semaphoreObj);
			}

			batch.commandBuffers.reserve(submit.commandBufferCount);
			for (uint32_t i = 0; i < submit.commandBufferCount; ++i)
			{
				VKD_FROM_HANDLE(vkd::CommandBuffer, cmdBufferObj, submit.pCommandBuffers[i]);
				const VkResult markResult = cmdBufferObj->MarkSubmitted();
				if (markResult != VK_SUCCESS)
				{
					for (auto& previous : batches)
					{
						for (auto* previousBuffer : previous.commandBuffers)
							previousBuffer->MarkComplete();
					}
					for (auto* previousBuffer : batch.commandBuffers)
						previousBuffer->MarkComplete();
					return markResult;
				}
				batch.commandBuffers.push_back(cmdBufferObj);
			}

			batch.signalSemaphores.reserve(submit.signalSemaphoreCount);
			for (uint32_t i = 0; i < submit.signalSemaphoreCount; ++i)
			{
				VKD_FROM_HANDLE(vkd::Semaphore, semaphoreObj, submit.pSignalSemaphores[i]);
				batch.signalSemaphores.push_back(semaphoreObj);
			}

			batches.push_back(std::move(batch));
		}

		auto* softwareDevice = static_cast<SoftwareDevice*>(GetOwner());
		auto& threadPool = softwareDevice->GetThreadPool();

		std::lock_guard<std::mutex> lock(m_submitMutex);
		auto previousSubmit = std::move(m_previousSubmit);

		m_previousSubmit = threadPool.Submit([batches = std::move(batches), fence, previousSubmit = std::move(previousSubmit), allocationCallbacks = &GetAllocationCallbacks()]() mutable -> bool
											 {
			if (previousSubmit.valid())
				previousSubmit.wait();

			for (auto& batch : batches)
			{
				for (auto* semaphore : batch.waitSemaphores)
					semaphore->Wait();

				for (auto* cmdBufferObj : batch.commandBuffers)
				{
					CpuContext cpuContext(*allocationCallbacks);
					CommandDispatcher commandDispatcher(cpuContext);
					commandDispatcher.Execute(*cmdBufferObj);
					cmdBufferObj->MarkComplete();
				}

				for (auto* semaphore : batch.signalSemaphores)
					semaphore->Signal();
			}

			if (fence)
			{
				VKD_FROM_HANDLE(vkd::Fence, fenceObj, fence);
				fenceObj->Signal();
			}
			return true; });

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
		return VK_ERROR_VALIDATION_FAILED_EXT;
	}
} // namespace vkd::software
