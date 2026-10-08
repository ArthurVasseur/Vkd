/**
 * @file Device.hpp
 * @brief AMD logical device
 * @date 2026-08-31
 *
 * vkCreateDevice @ 0x180213760. Sequence before queues (SequenceRE 2026-05-31):
 * companion device → AMD D3DKMTCreateDevice → paging queue → 23 DRIVERPRIVATE
 * escapes → 22 init allocs → D3DKMTCreateContextVirtual.
 */

#pragma once

#include <mutex>
#include <vector>

#include "Vkd/Device/Device.hpp"

#include "VkdAmd/Shader/RpmPipelines.hpp"
#include "VkdAmd/Wddm/WddmAllocation.hpp"
#include "VkdAmd/Wddm/WddmDevice.hpp"

namespace vkd::amd
{
	class AmdDevice : public Device
	{
	public:
		explicit AmdDevice(const VkAllocationCallbacks& allocationCallbacks);
		~AmdDevice() override;

		[[nodiscard]] wddm::WddmDevice& GetWddmDevice()
		{
			return m_wddm;
		}
		[[nodiscard]] const wddm::WddmDevice& GetWddmDevice() const
		{
			return m_wddm;
		}

		bool AllocateGpuMemory(uint64_t sizeInBytes, bool hostVisible, wddm::WddmAllocation& out);
		bool AllocateGpuMemory(uint64_t sizeInBytes, uint32_t surfFlags, wddm::WddmAllocation& out);
		// Shader code and descriptor tables share one 4GB VA window ([4GB, 8GB), as PAL reserves
		// it): AMDVLK's shaders rebuild 64-bit table addresses from a 32-bit user-data pointer and
		// the high half of their own PC (s_getpc_b64).
		bool AllocateShaderWindowMemory(uint64_t sizeInBytes, uint32_t surfFlags, wddm::WddmAllocation& out);
		// GFX11 NGG writes vertex attributes to a memory ring (SPI_ATTRIBUTE_RING_BASE/SIZE); one per
		// device, allocated on the first draw. 0 if the allocation failed.
		D3DGPU_VIRTUAL_ADDRESS GetAttributeRingVa();
		// PAL's shader-ring descriptor table ("global table", USER_DATA_0 of every graphics stage):
		// 16-byte buffer descriptors indexed by ShaderRingSrd (gfx9ShaderRingSet.h); only entry 11,
		// VertexAttributes = the attribute ring, is filled. Returns the 32-bit pointer shaders get
		// (the table is in the shader window), 0 if the allocation failed.
		uint32_t GetShaderRingTable();
		// A PAL RPM compute pipeline (Shader/RpmPipelines.hpp) uploaded to the shader window: the code
		// and its internal table, both reachable from 32-bit pointers. All of them are uploaded on the
		// first use of any.
		struct RpmProgram
		{
			const RpmPipeline* pipeline = nullptr;
			D3DGPU_VIRTUAL_ADDRESS codeVa = 0;
			D3DGPU_VIRTUAL_ADDRESS tableVa = 0;
		};
		bool GetRpmProgram(RpmPipelineId id, RpmProgram& out);

		VkResult Create(vkd::PhysicalDevice& owner, const VkDeviceCreateInfo& createInfo,
						const VkAllocationCallbacks& allocationCallbacks) override;

		DispatchableObjectResult<vkd::Queue> CreateQueueForFamily(uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags) override;
		Result<vkd::CommandPool*, VkResult> CreateCommandPool(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::Fence*, VkResult> CreateFence(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::Semaphore*, VkResult> CreateSemaphore(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::Event*, VkResult> CreateEvent(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::QueryPool*, VkResult> CreateQueryPool(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::PipelineCache*, VkResult> CreatePipelineCache(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::Buffer*, VkResult> CreateBuffer(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::BufferView*, VkResult> CreateBufferView(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::Image*, VkResult> CreateImage(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::DeviceMemory*, VkResult> CreateDeviceMemory(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::Pipeline*, VkResult> CreatePipeline(const VkAllocationCallbacks& allocationCallbacks) override;
		VkDeviceAddress GetBufferAddress(const vkd::Buffer& buffer) const override;
		// Linear image rows the CB/TA can address: the pitch is aligned to 256 bytes (AddrLib
		// Gfx11Lib::HwlComputeSurfaceInfoLinear, 256 / bytesPerElement texels), and the hardware
		// derives it from the width -- GFX11 has no pitch register.
		[[nodiscard]] VkDeviceSize GetImageRowPitchAlignment() const override
		{
			return 256;
		}
		Result<vkd::RenderPass*, VkResult> CreateRenderPass(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::ImageView*, VkResult> CreateImageView(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::Framebuffer*, VkResult> CreateFramebuffer(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::ShaderModule*, VkResult> CreateShaderModule(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::PipelineLayout*, VkResult> CreatePipelineLayout(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::DescriptorSetLayout*, VkResult> CreateDescriptorSetLayout(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::DescriptorPool*, VkResult> CreateDescriptorPool(const VkAllocationCallbacks& allocationCallbacks) override;
		Result<vkd::Sampler*, VkResult> CreateSampler(const VkAllocationCallbacks& allocationCallbacks) override;

	private:
		bool AllocateGpuMemory(uint64_t sizeInBytes, uint32_t surfFlags, wddm::WddmAllocation& out, uint64_t minimumVa, uint64_t maximumVa);
		void CreateCompanionDevice();
		void DestroyCompanionDevice();

		wddm::WddmDevice m_wddm;
		wddm::WddmAllocation m_attributeRing;
		wddm::WddmAllocation m_shaderRingTable;
		wddm::WddmAllocation m_rpmCode;
		RpmProgram m_rpmPrograms[static_cast<size_t>(RpmPipelineId::Count)];
		std::mutex m_ringMutex; // first-use allocation of the rings, from any command buffer
		D3DKMT_HANDLE m_companionDevice = 0;
		D3DKMT_HANDLE m_companionAdapter = 0;
	};
} // namespace vkd::amd
