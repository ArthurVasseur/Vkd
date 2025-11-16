/**
 * @file DeviceMemory.hpp
 * @brief Software renderer device memory implementation
 * @date 2025-10-26
 *
 * Device memory allocation using the TLSF allocator for CPU-accessible memory.
 */

#pragma once

#include "Vkd/DeviceMemory/DeviceMemory.hpp"
#include "VkdUtils/Allocator/Allocator.hpp"

namespace vkd::software
{
	class DeviceMemory : public vkd::DeviceMemory
	{
	public:
		DeviceMemory();
		~DeviceMemory() override;

		VkResult Create(vkd::Device& owner, const VkMemoryAllocateInfo& info, const VkAllocationCallbacks& allocationCallbacks) override;

		[[nodiscard]] inline UByte* Data();
		[[nodiscard]] inline const UByte* Data() const;

	protected:
		VkResult Map(VkDeviceSize offset, VkDeviceSize size, void** ppData) override;
		void Unmap() override;

	private:
		vkd::Allocation m_allocation;
		std::size_t m_mapOffset;
	};
} // namespace vkd::software

#include "DeviceMemory.inl"
