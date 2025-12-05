/**
 * @file BufferView.hpp
 * @brief Vulkan buffer view abstraction
 * @date 2025-12-05
 *
 * Represents a buffer view object describing how to access a buffer.
 */

#pragma once

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;

	class BufferView : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_BUFFER_VIEW;
		VKD_NON_DISPATCHABLE_HANDLE(BufferView);

		BufferView();
		~BufferView() override = default;

		virtual VkResult Create(Device& owner, const VkBufferViewCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline VkBuffer GetBuffer() const;
		[[nodiscard]] inline VkFormat GetFormat() const;
		[[nodiscard]] inline VkDeviceSize GetOffset() const;
		[[nodiscard]] inline VkDeviceSize GetRange() const;

	protected:
		Device* m_owner;
		VkBuffer m_buffer;
		VkFormat m_format;
		VkDeviceSize m_offset;
		VkDeviceSize m_range;
	};
} // namespace vkd

#include "BufferView.inl"
