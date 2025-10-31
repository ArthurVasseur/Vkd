/**
 * @file DeviceMemory.hpp
 * @brief Vulkan device memory abstraction
 * @date 2025-10-26
 *
 * Represents a device memory allocation.
 */

#pragma once

#include <vulkan/vulkan.h>

#include "Vkd/ObjectBase/ObjectBase.hpp"

namespace vkd
{
	class Device;

	class DeviceMemory : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_DEVICE_MEMORY;
		VKD_NON_DISPATCHABLE_HANDLE(DeviceMemory);

		DeviceMemory();
		~DeviceMemory() override = default;

		virtual VkResult Create(Device& owner, const VkMemoryAllocateInfo& info, const VkAllocationCallbacks& allocationCallbacks);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline VkDeviceSize GetSize() const;
		[[nodiscard]] inline UInt32 GetTypeIndex() const;
		[[nodiscard]] inline bool IsMapped() const;

		virtual VkResult Map(VkDeviceSize offset, VkDeviceSize size, void** ppData) = 0;
		virtual void Unmap() = 0;

		Device* m_owner;
		VkDeviceSize m_size;
		UInt32 m_typeIndex;
		bool m_mapped;
	};
}

#include "DeviceMemory.inl"
