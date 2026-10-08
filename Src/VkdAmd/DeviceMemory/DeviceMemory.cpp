#include "VkdAmd/DeviceMemory/DeviceMemory.hpp"

#include "VkdAmd/Device/Device.hpp"

namespace vkd::amd
{
	DeviceMemory::DeviceMemory(const VkAllocationCallbacks& allocationCallbacks) :
		vkd::DeviceMemory(allocationCallbacks)
	{
	}

	VkResult DeviceMemory::Create(vkd::Device& owner, const VkMemoryAllocateInfo& info,
								  const VkAllocationCallbacks& allocationCallbacks)
	{
		VkResult result = vkd::DeviceMemory::Create(owner, info, allocationCallbacks);
		if (result != VK_SUCCESS)
			return result;

		auto* amdDevice = static_cast<AmdDevice*>(&owner);
		const uint64_t size = info.allocationSize == 0 ? 1 : info.allocationSize;
		if (!amdDevice->AllocateGpuMemory(size, true, m_allocation) || m_allocation.GetGpuVa() == 0)
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;
		return VK_SUCCESS;
	}

	D3DGPU_VIRTUAL_ADDRESS DeviceMemory::GetGpuVaAt(VkDeviceSize offset, VkDeviceSize& contiguousBytes) const
	{
		contiguousBytes = 0;
		if (m_allocation.GetGpuVa() == 0 || offset >= m_allocation.GetSize())
			return 0;
		contiguousBytes = m_allocation.GetSize() - offset;
		return m_allocation.GetGpuVa() + offset;
	}

	void* DeviceMemory::MapHost()
	{
		void* ptr = nullptr;
		if (Map(0, VK_WHOLE_SIZE, &ptr) != VK_SUCCESS)
			return nullptr;
		return ptr;
	}

	void DeviceMemory::UnmapHost()
	{
		Unmap();
	}

	VkResult DeviceMemory::Map(VkDeviceSize offset, VkDeviceSize size, void** ppData)
	{
		if (offset >= m_size)
			return VK_ERROR_MEMORY_MAP_FAILED;

		const VkDeviceSize available = m_size - offset;
		if (size != VK_WHOLE_SIZE && size > available)
			return VK_ERROR_MEMORY_MAP_FAILED;

		void* base = m_allocation.MapToHost();
		if (!base)
			return VK_ERROR_MEMORY_MAP_FAILED;
		*ppData = static_cast<uint8_t*>(base) + offset;
		return VK_SUCCESS;
	}

	void DeviceMemory::Unmap()
	{
		m_allocation.UnmapFromHost();
	}
} // namespace vkd::amd
