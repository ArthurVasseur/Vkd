#pragma once

#include "Vkd/DeviceMemory/DeviceMemory.hpp"

#include "VkdAmd/Wddm/WddmAllocation.hpp"

namespace vkd::amd
{
	// One WDDM allocation per VkDeviceMemory: contiguous on both the CPU (Lock2) and GPU (one VA)
	// sides, so vkMapMemory hands out the real memory and sees GPU writes. It used to be a list of
	// 64KB allocations behind a CPU shadow copy, a workaround for allocation blobs that declared the
	// wrong size (fixed in PrivateData.hpp SetAllocBlobSize).
	class DeviceMemory : public vkd::DeviceMemory
	{
	public:
		explicit DeviceMemory(const VkAllocationCallbacks& allocationCallbacks);
		~DeviceMemory() override = default;

		VkResult Create(vkd::Device& owner, const VkMemoryAllocateInfo& info,
						const VkAllocationCallbacks& allocationCallbacks) override;

		[[nodiscard]] D3DGPU_VIRTUAL_ADDRESS GetGpuVa() const
		{
			return m_allocation.GetGpuVa();
		}
		[[nodiscard]] wddm::WddmAllocation& GetAllocation()
		{
			return m_allocation;
		}

		// GPU VA of byte `offset`, and how many bytes stay GPU-contiguous from there (the rest of the
		// allocation). Returns 0 past the end.
		[[nodiscard]] D3DGPU_VIRTUAL_ADDRESS GetGpuVaAt(VkDeviceSize offset, VkDeviceSize& contiguousBytes) const;

		void* MapHost();
		void UnmapHost();

	protected:
		VkResult Map(VkDeviceSize offset, VkDeviceSize size, void** ppData) override;
		void Unmap() override;

	private:
		wddm::WddmAllocation m_allocation;
	};
} // namespace vkd::amd
