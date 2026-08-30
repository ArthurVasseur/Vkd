/**
 * @file Device.cpp
 * @brief Implementation of software renderer logical device
 * @date 2025-04-23
 */

#include "VkdSoftware/Device/Device.hpp"

#include "Vkd/DescriptorPool/DescriptorPool.hpp"
#include "Vkd/DescriptorSetLayout/DescriptorSetLayout.hpp"
#include "Vkd/Instance/Instance.hpp"
#include "Vkd/Memory/Memory.hpp"
#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"
#include "Vkd/PipelineLayout/PipelineLayout.hpp"
#include "Vkd/QueryPool/QueryPool.hpp"
#include "Vkd/Sampler/Sampler.hpp"
#include "VkdSoftware/Buffer/Buffer.hpp"
#include "VkdSoftware/BufferView/BufferView.hpp"
#include "VkdSoftware/CommandPool/CommandPool.hpp"
#include "VkdSoftware/DeviceMemory/DeviceMemory.hpp"
#include "VkdSoftware/Framebuffer/Framebuffer.hpp"
#include "VkdSoftware/Image/Image.hpp"
#include "VkdSoftware/ImageView/ImageView.hpp"
#include "VkdSoftware/Pipeline/Pipeline.hpp"
#include "VkdSoftware/Queue/Queue.hpp"
#include "VkdSoftware/RenderPass/RenderPass.hpp"
#include "VkdSoftware/ShaderModule/ShaderModule.hpp"
#include "VkdSoftware/Synchronization/Event/Event.hpp"
#include "VkdSoftware/Synchronization/Fence/Fence.hpp"
#include "VkdSoftware/Synchronization/Semaphore/Semaphore.hpp"
#include "VkdUtils/System/System.hpp"

namespace vkd::software
{
	SoftwareDevice::SoftwareDevice(const VkAllocationCallbacks& allocationCallbacks) :
		Device(allocationCallbacks),
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

		if (cct::Logger::GetContext() == nullptr)
			cct::Logger::SetContext(&owner.GetOwner().GetLogger());
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

	see::Executor& SoftwareDevice::GetShaderExecutor()
	{
		return m_shaderExecutor;
	}

	DispatchableObjectResult<vkd::Queue> SoftwareDevice::CreateQueueForFamily(uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags)
	{
		PhysicalDevice* physicalDevice = GetOwner();
		auto properties = physicalDevice->GetQueueFamilyProperties();
		VKD_CHECK(queueFamilyIndex < properties.size());

		auto* queue = mem::NewDeviceDispatchable<Queue>(GetAllocationCallbacks());
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

	Result<vkd::CommandPool*, VkResult> SoftwareDevice::CreateCommandPool(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* pool = mem::NewDevice<CommandPool>(allocationCallbacks);
		if (!pool)
		{
			CCT_ASSERT_FALSE("Failed to allocate CommandPool");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return pool;
	}

	Result<vkd::Fence*, VkResult> SoftwareDevice::CreateFence(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* fence = mem::NewObject<Fence>(allocationCallbacks);
		if (!fence)
		{
			CCT_ASSERT_FALSE("Failed to allocate Fence");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return fence;
	}

	Result<vkd::Semaphore*, VkResult> SoftwareDevice::CreateSemaphore(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* semaphore = mem::NewObject<Semaphore>(allocationCallbacks);
		if (!semaphore)
		{
			CCT_ASSERT_FALSE("Failed to allocate Semaphore");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return semaphore;
	}

	Result<vkd::Event*, VkResult> SoftwareDevice::CreateEvent(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* event = mem::NewObject<Event>(allocationCallbacks);
		if (!event)
		{
			CCT_ASSERT_FALSE("Failed to allocate Event");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return event;
	}

	Result<vkd::QueryPool*, VkResult> SoftwareDevice::CreateQueryPool(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* queryPool = mem::NewObject<vkd::QueryPool>(allocationCallbacks);
		if (!queryPool)
		{
			CCT_ASSERT_FALSE("Failed to allocate QueryPool");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return queryPool;
	}

	Result<vkd::Buffer*, VkResult> SoftwareDevice::CreateBuffer(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* buffer = mem::NewObject<Buffer>(allocationCallbacks);
		if (!buffer)
		{
			CCT_ASSERT_FALSE("Failed to allocate Buffer");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return buffer;
	}

	Result<vkd::BufferView*, VkResult> SoftwareDevice::CreateBufferView(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* bufferView = mem::NewObject<BufferView>(allocationCallbacks);
		if (!bufferView)
		{
			CCT_ASSERT_FALSE("Failed to allocate BufferView");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return bufferView;
	}

	Result<vkd::Image*, VkResult> SoftwareDevice::CreateImage(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* image = mem::NewObject<Image>(allocationCallbacks);
		if (!image)
		{
			CCT_ASSERT_FALSE("Failed to allocate Image");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return image;
	}

	Result<vkd::DeviceMemory*, VkResult> SoftwareDevice::CreateDeviceMemory(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* memory = mem::NewObject<DeviceMemory>(allocationCallbacks);
		if (!memory)
		{
			CCT_ASSERT_FALSE("Failed to allocate DeviceMemory");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return memory;
	}

	Result<vkd::Pipeline*, VkResult> SoftwareDevice::CreatePipeline(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* pipeline = mem::NewObject<Pipeline>(allocationCallbacks);
		if (!pipeline)
		{
			CCT_ASSERT_FALSE("Failed to allocate Pipeline");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return pipeline;
	}

	Result<vkd::RenderPass*, VkResult> SoftwareDevice::CreateRenderPass(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* renderPass = mem::NewObject<RenderPass>(allocationCallbacks);
		if (!renderPass)
		{
			CCT_ASSERT_FALSE("Failed to allocate RenderPass");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return renderPass;
	}

	Result<vkd::ImageView*, VkResult> SoftwareDevice::CreateImageView(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* imageView = mem::NewObject<ImageView>(allocationCallbacks);
		if (!imageView)
		{
			CCT_ASSERT_FALSE("Failed to allocate ImageView");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return imageView;
	}

	Result<vkd::Framebuffer*, VkResult> SoftwareDevice::CreateFramebuffer(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* framebuffer = mem::NewObject<Framebuffer>(allocationCallbacks);
		if (!framebuffer)
		{
			CCT_ASSERT_FALSE("Failed to allocate Framebuffer");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return framebuffer;
	}

	Result<vkd::ShaderModule*, VkResult> SoftwareDevice::CreateShaderModule(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* shaderModule = mem::NewObject<ShaderModule>(allocationCallbacks);
		if (!shaderModule)
		{
			CCT_ASSERT_FALSE("Failed to allocate ShaderModule");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return shaderModule;
	}

	Result<vkd::PipelineLayout*, VkResult> SoftwareDevice::CreatePipelineLayout(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* layout = mem::NewObject<vkd::PipelineLayout>(allocationCallbacks);
		if (!layout)
		{
			CCT_ASSERT_FALSE("Failed to allocate PipelineLayout");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return layout;
	}

	Result<vkd::DescriptorSetLayout*, VkResult> SoftwareDevice::CreateDescriptorSetLayout(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* layout = mem::NewObject<vkd::DescriptorSetLayout>(allocationCallbacks);
		if (!layout)
		{
			CCT_ASSERT_FALSE("Failed to allocate DescriptorSetLayout");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return layout;
	}

	Result<vkd::DescriptorPool*, VkResult> SoftwareDevice::CreateDescriptorPool(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* pool = mem::NewObject<vkd::DescriptorPool>(allocationCallbacks);
		if (!pool)
		{
			CCT_ASSERT_FALSE("Failed to allocate DescriptorPool");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return pool;
	}

	Result<vkd::Sampler*, VkResult> SoftwareDevice::CreateSampler(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* sampler = mem::NewObject<vkd::Sampler>(allocationCallbacks);
		if (!sampler)
		{
			CCT_ASSERT_FALSE("Failed to allocate Sampler");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return sampler;
	}
} // namespace vkd::software
