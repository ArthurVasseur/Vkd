/**
 * @file Device.hpp
 * @brief Software renderer logical device implementation
 * @date 2025-04-23
 *
 * CPU-based software rendering logical device with thread pool and memory allocator.
 */

#pragma once

#include "Vkd/Device/Device.hpp"
#include "VkdUtils/ThreadPool/ThreadPool.hpp"
#include "VkdUtils/Allocator/Allocator.hpp"

namespace vkd::software
{
	class SoftwareDevice : public Device
	{
	public:
		SoftwareDevice();
		~SoftwareDevice() override;

		[[nodiscard]] ThreadPool& GetThreadPool();
		[[nodiscard]] Allocator& GetAllocator();

		DispatchableObjectResult<vkd::Queue> CreateQueueForFamily(uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags) override;
		Result<vkd::CommandPool*, VkResult> CreateCommandPool() override;
		Result<vkd::Fence*, VkResult> CreateFence() override;
		Result<vkd::Buffer*, VkResult> CreateBuffer() override;
		Result<vkd::DeviceMemory*, VkResult> CreateDeviceMemory() override;
		Result<vkd::Pipeline*, VkResult> CreatePipeline() override;
	private:
		ThreadPool m_threadPool;
		Allocator m_allocator;
	};
}
