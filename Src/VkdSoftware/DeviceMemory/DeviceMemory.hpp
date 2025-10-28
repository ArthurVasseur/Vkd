//
// Created by arthur on 26/10/2025.
//

#pragma once

#include <vector>

#include "Vkd/DeviceMemory/DeviceMemory.hpp"

namespace vkd::software
{
	class DeviceMemory : public vkd::DeviceMemory
	{
	public:
		DeviceMemory();
		~DeviceMemory() override = default;

		VkResult Create(vkd::Device& owner, const VkMemoryAllocateInfo& info, const VkAllocationCallbacks& allocationCallbacks) override;

		[[nodiscard]] inline uint8_t* Data();
		[[nodiscard]] inline const uint8_t* Data() const;

	protected:
		VkResult Map(VkDeviceSize offset, VkDeviceSize size, void** ppData) override;
		void Unmap() override;

	private:
		std::vector<uint8_t> m_data;
		size_t m_mapOffset;
	};
}

#include "DeviceMemory.inl"
