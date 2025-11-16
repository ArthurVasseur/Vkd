/**
 * @file Buffer.hpp
 * @brief Vulkan buffer object abstraction
 * @date 2025-10-26
 *
 * Represents a Vulkan buffer resource and handles memory binding.
 */

#pragma once

#include <vector>

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;
	class DeviceMemory;
	class Image;

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

		struct OpCopy2
		{
			Buffer* src;
			Buffer* dst;
			std::vector<VkBufferCopy2> regions;
		};

		struct OpUpdate
		{
			Buffer* dst;
			VkDeviceSize offset;
			std::vector<UInt8> data;
		};

		struct OpCopyBufferToImage
		{
			Buffer* src;
			Image* dst;
			VkImageLayout dstLayout;
			std::vector<VkBufferImageCopy> regions;
		};

		struct OpCopyImageToBuffer
		{
			Image* src;
			VkImageLayout srcLayout;
			Buffer* dst;
			std::vector<VkBufferImageCopy> regions;
		};

		using Op = Nz::TypeList<OpFill, OpCopy, OpCopy2, OpUpdate, OpCopyBufferToImage, OpCopyImageToBuffer>;

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
} // namespace vkd

#include "Buffer.inl"
