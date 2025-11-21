/**
 * @file Device.cpp
 * @brief Implementation of software renderer logical device
 * @date 2025-04-23
 */

#include "VkdSoftware/Device/Device.hpp"

#include "Vkd/Memory/Memory.hpp"
#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"
#include "VkdSoftware/Buffer/Buffer.hpp"
#include "VkdSoftware/CommandPool/CommandPool.hpp"
#include "VkdSoftware/DeviceMemory/DeviceMemory.hpp"
#include "VkdSoftware/Framebuffer/Framebuffer.hpp"
#include "VkdSoftware/Image/Image.hpp"
#include "VkdSoftware/ImageView/ImageView.hpp"
#include "VkdSoftware/Pipeline/Pipeline.hpp"
#include "VkdSoftware/Queue/Queue.hpp"
#include "VkdSoftware/RenderPass/RenderPass.hpp"
#include "VkdSoftware/ShaderModule/ShaderModule.hpp"
#include "VkdSoftware/Synchronization/Fence/Fence.hpp"
#include "VkdUtils/System/System.hpp"

namespace vkd::software
{
	SoftwareDevice::SoftwareDevice() :
		m_allocator([]() -> std::size_t
					{
		System system;
		const std::optional<UInt64> availableRam = system.GetAvailableRamBytes();
		if (availableRam)
			return static_cast<std::size_t>(System::ComputeDeviceMemoryHeapSize(*availableRam));
		CCT_ASSERT_FALSE("Could not query system ram, using 256 Mb");
		return 256ULL * 1024ULL * 1024ULL; }())
	{
	}

	SoftwareDevice::~SoftwareDevice()
	{
		m_threadPool.RequestStop();
	}

	VkResult SoftwareDevice::Create(vkd::PhysicalDevice& owner, const VkDeviceCreateInfo& pDeviceCreateInfo, const VkAllocationCallbacks& allocationCallbacks)
	{
		if (!m_allocator.Init())
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;

		cct::Logger::Info("Allocated {} Mb for SoftwareDevice allocator", m_allocator.GetTotal() / (1024ULL * 1024ULL));
		return Device::Create(owner, pDeviceCreateInfo, allocationCallbacks);
	}

	ThreadPool& SoftwareDevice::GetThreadPool()
	{
		return m_threadPool;
	}

	Allocator& SoftwareDevice::GetAllocator()
	{
		return m_allocator;
	}

	DispatchableObjectResult<vkd::Queue> SoftwareDevice::CreateQueueForFamily(uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags)
	{
		PhysicalDevice* physicalDevice = GetOwner();
		auto properties = physicalDevice->GetQueueFamilyProperties();
		VKD_CHECK(queueFamilyIndex < properties.size());

		auto* queue = mem::NewDispatchable<Queue>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
		if (!queue)
		{
			CCT_ASSERT_FALSE("Failed to allocate SoftwareQueue");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		VkResult result = queue->Object->Create(*this, queueFamilyIndex, queueIndex, flags);
		if (result != VK_SUCCESS)
			return result;

		return reinterpret_cast<DispatchableObject<vkd::Queue>*>(queue);
	}

	Result<vkd::CommandPool*, VkResult> SoftwareDevice::CreateCommandPool()
	{
		auto* pool = mem::New<CommandPool>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
		if (!pool)
		{
			CCT_ASSERT_FALSE("Failed to allocate CommandPool");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return pool;
	}

	Result<vkd::Fence*, VkResult> SoftwareDevice::CreateFence()
	{
		auto* fence = mem::New<Fence>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!fence)
		{
			CCT_ASSERT_FALSE("Failed to allocate Fence");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return fence;
	}

	Result<vkd::Buffer*, VkResult> SoftwareDevice::CreateBuffer()
	{
		auto* buffer = mem::New<Buffer>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!buffer)
		{
			CCT_ASSERT_FALSE("Failed to allocate Buffer");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return buffer;
	}

	Result<vkd::Image*, VkResult> SoftwareDevice::CreateImage()
	{
		auto* image = mem::New<Image>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!image)
		{
			CCT_ASSERT_FALSE("Failed to allocate Image");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return image;
	}

	Result<vkd::DeviceMemory*, VkResult> SoftwareDevice::CreateDeviceMemory()
	{
		auto* memory = mem::New<DeviceMemory>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!memory)
		{
			CCT_ASSERT_FALSE("Failed to allocate DeviceMemory");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return memory;
	}

	Result<vkd::Pipeline*, VkResult> SoftwareDevice::CreatePipeline()
	{
		auto* pipeline = mem::New<Pipeline>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!pipeline)
		{
			CCT_ASSERT_FALSE("Failed to allocate Pipeline");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return pipeline;
	}

	Result<vkd::RenderPass*, VkResult> SoftwareDevice::CreateRenderPass()
	{
		auto* renderPass = mem::New<RenderPass>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!renderPass)
		{
			CCT_ASSERT_FALSE("Failed to allocate RenderPass");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return renderPass;
	}

	Result<vkd::ImageView*, VkResult> SoftwareDevice::CreateImageView()
	{
		auto* imageView = mem::New<ImageView>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!imageView)
		{
			CCT_ASSERT_FALSE("Failed to allocate ImageView");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return imageView;
	}

	Result<vkd::Framebuffer*, VkResult> SoftwareDevice::CreateFramebuffer()
	{
		auto* framebuffer = mem::New<Framebuffer>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!framebuffer)
		{
			CCT_ASSERT_FALSE("Failed to allocate Framebuffer");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return framebuffer;
	}

	Result<vkd::ShaderModule*, VkResult> SoftwareDevice::CreateShaderModule()
	{
		auto* shaderModule = mem::New<ShaderModule>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!shaderModule)
		{
			CCT_ASSERT_FALSE("Failed to allocate ShaderModule");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return shaderModule;
	}
} // namespace vkd::software
