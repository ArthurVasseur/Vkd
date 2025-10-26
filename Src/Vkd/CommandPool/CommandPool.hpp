//
// Created by arthur on 25/10/2025.
//

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
		VKD_DISPATCHABLE_HANDLE(CommandPool);

		CommandPool();
		~CommandPool() override = default;

		virtual VkResult Create(Device& owner, const VkCommandPoolCreateInfo& createInfo, const VkAllocationCallbacks& pAllocator);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline VkCommandPoolCreateFlags GetFlags() const;
		[[nodiscard]] inline cct::UInt32 GetQueueFamilyIndex() const;

		DispatchableObjectResult<CommandBuffer> AllocateCommandBuffer(VkCommandBufferLevel level);

		// Vulkan API entry points
		

		virtual VkResult Reset(VkCommandPoolResetFlags flags) = 0;
		virtual DispatchableObjectResult<CommandBuffer> DoCreateCommandBuffer(VkCommandBufferLevel level) = 0;

	private:
		Device* m_owner;
		VkCommandPoolCreateFlags m_flags;
		cct::UInt32 m_queueFamilyIndex;
	};
}

#include "CommandPool.inl"
