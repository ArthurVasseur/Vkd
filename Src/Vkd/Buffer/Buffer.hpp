/**
 * @file Buffer.hpp
 * @brief Vulkan buffer object abstraction
 * @date 2025-10-26
 *
 * Represents a Vulkan buffer resource and handles memory binding.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "Vkd/ObjectBase/ObjectBase.hpp"

namespace vkd
{
	class Device;
	class DeviceMemory;

	class Buffer : public ObjectBase
	{
	public:
		struct OpFill
		{
			Buffer* dst;
			VkDeviceSize offset;
			VkDeviceSize size;
			uint32_t data;
		};

		struct OpCopy
		{
			Buffer* src;
			Buffer* dst;
			std::vector<VkBufferCopy> regions;
		};
		using Op = Nz::TypeList<OpFill, OpCopy>;

		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_BUFFER;
		VKD_NON_DISPATCHABLE_HANDLE(Buffer);

		Buffer();
		~Buffer() override = default;

		virtual VkResult Create(Device& owner, const VkBufferCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks);
		void BindBufferMemory(DeviceMemory& deviceMemory, VkDeviceSize memoryOffset);
		void GetMemoryRequirements(VkMemoryRequirements& memoryRequirements) const;

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline VkDeviceSize GetSize() const;
		[[nodiscard]] inline VkBufferUsageFlags GetUsage() const;
		[[nodiscard]] inline DeviceMemory* GetMemory() const;
		[[nodiscard]] inline VkDeviceSize GetMemoryOffset() const;
		[[nodiscard]] inline bool IsBound() const;

	protected:
		Device* m_owner;
		VkDeviceSize m_size;
		VkBufferUsageFlags m_usage;
		DeviceMemory* m_memory;
		VkDeviceSize m_memoryOffset;
	};
}

#include "Buffer.inl"
