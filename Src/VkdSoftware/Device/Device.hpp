/**
 * @file Device.hpp
 * @brief Software renderer logical device implementation
 * @date 2025-04-23
 *
 * CPU-based software rendering logical device with thread pool and memory allocator.
 */

#pragma once

#include "Vkd/Device/Device.hpp"
#include "VkdUtils/Allocator/Allocator.hpp"
#include "VkdUtils/ThreadPool/ThreadPool.hpp"

namespace vkd::software
{
	class SoftwareDevice : public Device
	{
	public:
		SoftwareDevice();
		~SoftwareDevice() override;

		VkResult Create(vkd::PhysicalDevice& owner, const VkDeviceCreateInfo& pDeviceCreateInfo, const VkAllocationCallbacks& allocationCallbacks) override;

		[[nodiscard]] ThreadPool& GetThreadPool();
		[[nodiscard]] Allocator& GetAllocator();

		DispatchableObjectResult<vkd::Queue> CreateQueueForFamily(uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags) override;
		Result<vkd::CommandPool*, VkResult> CreateCommandPool(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::Fence*, VkResult> CreateFence(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::Buffer*, VkResult> CreateBuffer(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::BufferView*, VkResult> CreateBufferView(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::Image*, VkResult> CreateImage(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::DeviceMemory*, VkResult> CreateDeviceMemory(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::Pipeline*, VkResult> CreatePipeline(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::RenderPass*, VkResult> CreateRenderPass(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::ImageView*, VkResult> CreateImageView(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::Framebuffer*, VkResult> CreateFramebuffer(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::ShaderModule*, VkResult> CreateShaderModule(const VkAllocationCallbacks& allocationCallbacks) override;

	private:
		ThreadPool m_threadPool;
		Allocator m_allocator;
	};
} // namespace vkd::software
