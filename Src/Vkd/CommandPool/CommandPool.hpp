/**
 * @file CommandPool.hpp
 * @brief Vulkan command pool abstraction
 * @date 2025-10-25
 *
 * Represents a command pool for allocating command buffers.
 */

#pragma once

#include <vulkan/vulkan.h>

#include "Vkd/ObjectBase/ObjectBase.hpp"

namespace vkd
{
	class Device;
	class CommandBuffer;

	class CommandPool : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_COMMAND_POOL;
		VKD_NON_DISPATCHABLE_HANDLE(CommandPool);

		CommandPool();
		~CommandPool() override = default;

		virtual VkResult Create(Device& owner, const VkCommandPoolCreateInfo& createInfo, const VkAllocationCallbacks& pAllocator);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline VkCommandPoolCreateFlags GetFlags() const;
		[[nodiscard]] inline UInt32 GetQueueFamilyIndex() const;

		DispatchableObjectResult<CommandBuffer> AllocateCommandBuffer(VkCommandBufferLevel level);

		// Vulkan API entry points
		

		virtual VkResult Reset(VkCommandPoolResetFlags flags) = 0;
		virtual DispatchableObjectResult<CommandBuffer> CreateCommandBuffer(VkCommandBufferLevel level) = 0;

	private:
		Device* m_owner;
		VkCommandPoolCreateFlags m_flags;
		UInt32 m_queueFamilyIndex;
	};
}

#include "CommandPool.inl"
