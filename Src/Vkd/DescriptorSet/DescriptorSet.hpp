/**
 * @file DescriptorSet.hpp
 * @brief Vulkan descriptor set abstraction
 * @date 2026-08-25
 *
 * Stores the resources (buffers, for now) bound to each binding of a descriptor set,
 * as written by vkUpdateDescriptorSets.
 */

#pragma once

#include <unordered_map>
#include <vector>

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;
	class DescriptorSetLayout;
	class Buffer;

	class DescriptorSet : public ObjectBase
	{
	public:
		struct BufferBinding
		{
			Buffer* buffer = nullptr;
			VkDeviceSize offset = 0;
			VkDeviceSize range = 0; // VK_WHOLE_SIZE means "offset to the end of the buffer"
		};

		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_DESCRIPTOR_SET;
		VKD_NON_DISPATCHABLE_HANDLE(DescriptorSet);

		DescriptorSet();
		~DescriptorSet() override = default;

		virtual VkResult Create(Device& owner, DescriptorSetLayout& layout, const VkAllocationCallbacks& allocationCallbacks);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline DescriptorSetLayout* GetLayout() const;

		void SetBufferBinding(UInt32 binding, UInt32 arrayElement, const BufferBinding& info);
		[[nodiscard]] const BufferBinding* FindBufferBinding(UInt32 binding, UInt32 arrayElement = 0) const noexcept;

	protected:
		Device* m_owner;
		DescriptorSetLayout* m_layout;
		std::unordered_map<UInt32, std::vector<BufferBinding>> m_bufferBindings;
	};
} // namespace vkd

#include "DescriptorSet.inl"
