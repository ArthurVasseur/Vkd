/**
 * @file Device.cpp
 * @brief AMD logical device — WDDM pre-context sequence then queues
 * @date 2026-08-31
 */

#include "VkdAmd/Device/Device.hpp"

#include <cstring>
#include <utility>

#include <Concerto/Core/Logger/Logger.hpp>

#include "Vkd/BufferView/BufferView.hpp"
#include "Vkd/DescriptorPool/DescriptorPool.hpp"
#include "Vkd/DescriptorSetLayout/DescriptorSetLayout.hpp"
#include "Vkd/Framebuffer/Framebuffer.hpp"
#include "Vkd/Image/Image.hpp"
#include "Vkd/ImageView/ImageView.hpp"
#include "Vkd/Memory/Memory.hpp"
#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"
#include "Vkd/Pipeline/Pipeline.hpp"
#include "Vkd/PipelineCache/PipelineCache.hpp"
#include "Vkd/PipelineLayout/PipelineLayout.hpp"
#include "Vkd/QueryPool/QueryPool.hpp"
#include "Vkd/RenderPass/RenderPass.hpp"
#include "Vkd/Sampler/Sampler.hpp"
#include "Vkd/ShaderModule/ShaderModule.hpp"
#include "Vkd/Synchronization/Event/HostEvent.hpp"
#include "Vkd/Synchronization/Semaphore/HostSemaphore.hpp"

#include "VkdAmd/Buffer/Buffer.hpp"
#include "VkdAmd/CommandPool/CommandPool.hpp"
#include "VkdAmd/DeviceMemory/DeviceMemory.hpp"
#include "VkdAmd/Fence/Fence.hpp"
#include "VkdAmd/Image/Image.hpp"
#include "VkdAmd/PhysicalDevice/PhysicalDevice.hpp"
#include "VkdAmd/Pipeline/Pipeline.hpp"
#include "VkdAmd/PrivateData.hpp"
#include "VkdAmd/Queue/Queue.hpp"
#include "VkdAmd/Shader/Gfx11Descriptors.hpp"

namespace vkd::amd
{
	AmdDevice::AmdDevice(const VkAllocationCallbacks& allocationCallbacks) :
		Device(allocationCallbacks)
	{
	}

	AmdDevice::~AmdDevice()
	{
		DestroyQueues();
		DestroyCompanionDevice();
	}

	void AmdDevice::CreateCompanionDevice()
	{
		for (const auto& adapter : wddm::EnumerateAdapters())
		{
			if (adapter.vendorId == 0x1002)
				continue;

			D3DKMT_CREATEDEVICE create = {};
			create.hAdapter = adapter.handle;
			if (D3DKMTCreateDevice(&create) == STATUS_SUCCESS)
			{
				m_companionDevice = create.hDevice;
				m_companionAdapter = adapter.handle;
				cct::Logger::Info("AmdDevice: companion device hDevice=0x{:X} adapter LUID {:08X} ({})",
								  static_cast<uint64_t>(m_companionDevice),
								  adapter.luid.LowPart,
								  static_cast<const char*>(adapter.name));
				return;
			}
		}
		cct::Logger::Warning("AmdDevice: no companion adapter found");
	}

	void AmdDevice::DestroyCompanionDevice()
	{
		if (m_companionDevice == 0)
			return;
		D3DKMT_DESTROYDEVICE destroy = {};
		destroy.hDevice = m_companionDevice;
		D3DKMTDestroyDevice(&destroy);
		m_companionDevice = 0;
		m_companionAdapter = 0;
	}

	bool AmdDevice::AllocateGpuMemory(uint64_t sizeInBytes, bool hostVisible, wddm::WddmAllocation& out)
	{
		return AllocateGpuMemory(sizeInBytes,
								 hostVisible ? kSurfaceFlagsHostVisible : kSurfaceFlagsDeviceLocal,
								 out);
	}

	D3DGPU_VIRTUAL_ADDRESS AmdDevice::GetAttributeRingVa()
	{
		// 512KB: the SPI_ATTRIBUTE_RING_SIZE AMDVLK programs (0x00020007, MEM_SIZE 7 = 8 x 64KB).
		std::lock_guard lock(m_ringMutex);
		if (!m_attributeRing.IsValid())
			AllocateGpuMemory(kAttributeRingBytes, kSurfaceFlagsHostVisible, m_attributeRing);
		return m_attributeRing.GetGpuVa();
	}

	bool AmdDevice::GetRpmProgram(RpmPipelineId id, RpmProgram& out)
	{
		std::lock_guard lock(m_ringMutex);
		if (!m_rpmCode.IsValid())
		{
			// Code 256-byte aligned (COMPUTE_PGM_LO), each internal table right after its code.
			uint64_t bytes = 0;
			uint64_t offsets[std::size(kRpmPipelines)][2] = {};
			for (size_t i = 0; i < std::size(kRpmPipelines); ++i)
			{
				offsets[i][0] = bytes;
				bytes = (bytes + kRpmPipelines[i].code.size_bytes() + 255) & ~uint64_t{255};
				offsets[i][1] = bytes;
				bytes = (bytes + kRpmPipelines[i].internalTable.size_bytes() + 255) & ~uint64_t{255};
			}
			wddm::WddmAllocation code;
			if (!AllocateShaderWindowMemory(bytes, kSurfaceFlagsInitA, code))
				return false;
			auto* cpu = static_cast<uint8_t*>(code.MapToHost());
			if (!cpu)
				return false;
			std::memset(cpu, 0, static_cast<size_t>(bytes));
			for (size_t i = 0; i < std::size(kRpmPipelines); ++i)
			{
				const RpmPipeline& pipeline = kRpmPipelines[i];
				std::memcpy(cpu + offsets[i][0], pipeline.code.data(), pipeline.code.size_bytes());
				std::memcpy(cpu + offsets[i][1], pipeline.internalTable.data(), pipeline.internalTable.size_bytes());
				m_rpmPrograms[i] = {&pipeline, code.GetGpuVa() + offsets[i][0], code.GetGpuVa() + offsets[i][1]};
			}
			code.UnmapFromHost();
			m_rpmCode = std::move(code);
		}
		out = m_rpmPrograms[static_cast<size_t>(id)];
		return out.pipeline != nullptr;
	}

	uint32_t AmdDevice::GetShaderRingTable()
	{
		constexpr uint32_t kVertexAttributesSrd = 11; // ShaderRingSrd::VertexAttributes
		const D3DGPU_VIRTUAL_ADDRESS ring = GetAttributeRingVa();
		if (ring == 0)
			return 0;
		std::lock_guard lock(m_ringMutex);
		if (!m_shaderRingTable.IsValid())
		{
			if (!AllocateShaderWindowMemory(0x1000, kSurfaceFlagsHostVisible, m_shaderRingTable))
				return 0;
			auto* table = static_cast<uint8_t*>(m_shaderRingTable.MapToHost());
			if (!table)
				return 0;
			std::memset(table, 0, 0x100);
			gfx11::WriteAttributeRingDescriptor(table + kVertexAttributesSrd * 16, ring, kAttributeRingBytes);
			m_shaderRingTable.UnmapFromHost();
		}
		return static_cast<uint32_t>(m_shaderRingTable.GetGpuVa());
	}

	bool AmdDevice::AllocateShaderWindowMemory(uint64_t sizeInBytes, uint32_t surfFlags, wddm::WddmAllocation& out)
	{
		return AllocateGpuMemory(sizeInBytes, surfFlags, out, kShaderWindowBase, kShaderWindowBase + kShaderWindowSize);
	}

	bool AmdDevice::AllocateGpuMemory(uint64_t sizeInBytes, uint32_t surfFlags, wddm::WddmAllocation& out)
	{
		return AllocateGpuMemory(sizeInBytes, surfFlags, out, 0, 0);
	}

	bool AmdDevice::AllocateGpuMemory(uint64_t sizeInBytes, uint32_t surfFlags, wddm::WddmAllocation& out, uint64_t minimumVa,
									  uint64_t maximumVa)
	{
		// The blob's size fields (+0x54/+0xA8/+0xB8/+0x114) must match what WddmAllocation maps.
		const uint64_t mappedSize = wddm::WddmAllocation::MappedSize(sizeInBytes);
		if (mappedSize > UINT32_MAX)
			return false;

		ResourcePrivateData resData;
		const AllocBlobBuffer blob = MakeAllocBlobForSurfFlags(surfFlags, static_cast<uint32_t>(mappedSize));

		wddm::AllocationDesc desc = {};
		desc.sizeInBytes = sizeInBytes;
		desc.alignment = 0x10000;
		desc.resourcePrivateData = &resData;
		desc.resourcePrivateDataSize = sizeof(resData);
		desc.allocPrivateData = blob.ptr();
		desc.allocPrivateDataSize = kAllocBlobSize;
		desc.adapter = m_wddm.GetAdapterHandle();
		desc.minimumVa = minimumVa;
		desc.maximumVa = maximumVa;

		return out.Create(m_wddm.GetDeviceHandle(), m_wddm.GetPagingQueue().GetHandle(), desc,
						  m_wddm.GetPagingQueue().GetSyncObject());
	}

	VkResult AmdDevice::Create(vkd::PhysicalDevice& owner, const VkDeviceCreateInfo& createInfo,
							   const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* amdPhysical = static_cast<amd::PhysicalDevice*>(&owner);

		CreateCompanionDevice();

		if (!m_wddm.Create(amdPhysical->GetAdapterInfo().handle))
		{
			CCT_ASSERT_FALSE("D3DKMTCreateDevice failed");
			DestroyCompanionDevice();
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		// AMDVLK also sends 23 private escapes and creates 22 init allocations here. Neither is
		// needed (wddm-dump captures in Amd/Src/WddmDump/Captures/EscapeData.hpp; without them the
		// CTS transfer/clear subsets and vkd-test-app are unchanged, 2026-10-07).

		return Device::Create(owner, createInfo, allocationCallbacks);
	}

	DispatchableObjectResult<vkd::Queue> AmdDevice::CreateQueueForFamily(uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags)
	{
		vkd::PhysicalDevice* physicalDevice = GetOwner();
		auto properties = physicalDevice->GetQueueFamilyProperties();
		VKD_CHECK(queueFamilyIndex < properties.size());

		auto* queue = mem::NewDeviceDispatchable<Queue>(GetAllocationCallbacks());
		if (!queue)
		{
			CCT_ASSERT_FALSE("Failed to allocate AmdQueue");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		VkResult result = queue->Object->Create(*this, queueFamilyIndex, queueIndex, flags);
		if (result != VK_SUCCESS)
			return result;

		return reinterpret_cast<DispatchableObject<vkd::Queue>*>(queue);
	}

	Result<vkd::CommandPool*, VkResult> AmdDevice::CreateCommandPool(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* pool = mem::NewDevice<CommandPool>(allocationCallbacks);
		if (!pool)
		{
			CCT_ASSERT_FALSE("Failed to allocate CommandPool");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return pool;
	}

	Result<vkd::Fence*, VkResult> AmdDevice::CreateFence(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* fence = mem::NewObject<Fence>(allocationCallbacks);
		if (!fence)
		{
			CCT_ASSERT_FALSE("Failed to allocate Fence");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return fence;
	}

	// Submissions complete before Queue::Submit returns, so semaphores and events only need CPU
	// state: Submit waits on and signals the semaphores, and applies vkCmdSetEvent/vkCmdResetEvent.
	Result<vkd::Semaphore*, VkResult> AmdDevice::CreateSemaphore(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* semaphore = mem::NewObject<HostSemaphore>(allocationCallbacks);
		if (!semaphore)
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		return semaphore;
	}

	Result<vkd::Event*, VkResult> AmdDevice::CreateEvent(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* event = mem::NewObject<HostEvent>(allocationCallbacks);
		if (!event)
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		return event;
	}

	// Query state lives on the host (vkd::QueryPool), updated when a submission completes
	// (ApplyHostOps); timestamps carry the GPU clock the command buffer wrote.
	Result<vkd::QueryPool*, VkResult> AmdDevice::CreateQueryPool(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* queryPool = mem::NewObject<vkd::QueryPool>(allocationCallbacks);
		if (!queryPool)
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		return queryPool;
	}

	Result<vkd::PipelineCache*, VkResult> AmdDevice::CreatePipelineCache(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* cache = mem::NewObject<vkd::PipelineCache>(allocationCallbacks);
		if (!cache)
		{
			CCT_ASSERT_FALSE("Failed to allocate PipelineCache");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return cache;
	}

	Result<vkd::Buffer*, VkResult> AmdDevice::CreateBuffer(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* buffer = mem::NewObject<Buffer>(allocationCallbacks);
		if (!buffer)
		{
			CCT_ASSERT_FALSE("Failed to allocate Buffer");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return buffer;
	}

	// The shared object: no shader reads texel buffers on AMD yet (no SPIR-V compiler), but the view
	// itself is plain state.
	Result<vkd::BufferView*, VkResult> AmdDevice::CreateBufferView(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* bufferView = mem::NewObject<vkd::BufferView>(allocationCallbacks);
		if (!bufferView)
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		return bufferView;
	}

	Result<vkd::Image*, VkResult> AmdDevice::CreateImage(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* image = mem::NewObject<amd::Image>(allocationCallbacks);
		if (!image)
		{
			CCT_ASSERT_FALSE("Failed to allocate Image");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return image;
	}

	Result<vkd::DeviceMemory*, VkResult> AmdDevice::CreateDeviceMemory(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* memory = mem::NewObject<DeviceMemory>(allocationCallbacks);
		if (!memory)
		{
			CCT_ASSERT_FALSE("Failed to allocate DeviceMemory");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return memory;
	}

	VkDeviceAddress AmdDevice::GetBufferAddress(const vkd::Buffer& buffer) const
	{
		const auto* memory = static_cast<const amd::DeviceMemory*>(buffer.GetMemory());
		if (!memory || memory->GetGpuVa() == 0)
			return 0;
		return memory->GetGpuVa() + buffer.GetMemoryOffset();
	}

	Result<vkd::Pipeline*, VkResult> AmdDevice::CreatePipeline(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* pipeline = mem::NewObject<amd::Pipeline>(allocationCallbacks);
		if (!pipeline)
		{
			CCT_ASSERT_FALSE("Failed to allocate Pipeline");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return pipeline;
	}

	Result<vkd::RenderPass*, VkResult> AmdDevice::CreateRenderPass(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* renderPass = mem::NewObject<vkd::RenderPass>(allocationCallbacks);
		if (!renderPass)
		{
			CCT_ASSERT_FALSE("Failed to allocate RenderPass");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return renderPass;
	}

	Result<vkd::ImageView*, VkResult> AmdDevice::CreateImageView(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* imageView = mem::NewObject<vkd::ImageView>(allocationCallbacks);
		if (!imageView)
		{
			CCT_ASSERT_FALSE("Failed to allocate ImageView");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return imageView;
	}

	Result<vkd::Framebuffer*, VkResult> AmdDevice::CreateFramebuffer(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* framebuffer = mem::NewObject<vkd::Framebuffer>(allocationCallbacks);
		if (!framebuffer)
		{
			CCT_ASSERT_FALSE("Failed to allocate Framebuffer");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return framebuffer;
	}

	Result<vkd::ShaderModule*, VkResult> AmdDevice::CreateShaderModule(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* shaderModule = mem::NewObject<vkd::ShaderModule>(allocationCallbacks);
		if (!shaderModule)
		{
			CCT_ASSERT_FALSE("Failed to allocate ShaderModule");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return shaderModule;
	}

	Result<vkd::PipelineLayout*, VkResult> AmdDevice::CreatePipelineLayout(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* layout = mem::NewObject<vkd::PipelineLayout>(allocationCallbacks);
		if (!layout)
		{
			CCT_ASSERT_FALSE("Failed to allocate PipelineLayout");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return layout;
	}

	Result<vkd::DescriptorSetLayout*, VkResult> AmdDevice::CreateDescriptorSetLayout(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* layout = mem::NewObject<vkd::DescriptorSetLayout>(allocationCallbacks);
		if (!layout)
		{
			CCT_ASSERT_FALSE("Failed to allocate DescriptorSetLayout");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return layout;
	}

	Result<vkd::DescriptorPool*, VkResult> AmdDevice::CreateDescriptorPool(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* pool = mem::NewObject<vkd::DescriptorPool>(allocationCallbacks);
		if (!pool)
		{
			CCT_ASSERT_FALSE("Failed to allocate DescriptorPool");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return pool;
	}

	Result<vkd::Sampler*, VkResult> AmdDevice::CreateSampler(const VkAllocationCallbacks& allocationCallbacks)
	{
		auto* sampler = mem::NewObject<vkd::Sampler>(allocationCallbacks);
		if (!sampler)
		{
			CCT_ASSERT_FALSE("Failed to allocate Sampler");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return sampler;
	}
} // namespace vkd::amd
