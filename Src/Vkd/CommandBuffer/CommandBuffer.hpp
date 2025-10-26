//
// Created by arthur on 25/10/2025.
//

#pragma once

#include <vulkan/vulkan.h>

#include "Vkd/ObjectBase/ObjectBase.hpp"

namespace vkd
{
	class CommandPool;

	class CommandBuffer : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_COMMAND_BUFFER;
		VKD_DISPATCHABLE_HANDLE(CommandBuffer);

		CommandBuffer();
		~CommandBuffer() override = default;

		virtual VkResult Create(CommandPool& owner, VkCommandBufferLevel level);

		[[nodiscard]] inline CommandPool* GetOwner() const;
		[[nodiscard]] inline VkCommandBufferLevel GetLevel() const;

		// Vulkan API entry points
		static VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo);
		static VkResult VKAPI_CALL EndCommandBuffer(VkCommandBuffer commandBuffer);
		static VkResult VKAPI_CALL ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags);

		virtual VkResult Begin(const VkCommandBufferBeginInfo& beginInfo) = 0;
		virtual VkResult End() = 0;
		virtual VkResult Reset(VkCommandBufferResetFlags flags) = 0;

	private:
		CommandPool* m_owner;
		VkCommandBufferLevel m_level;
	};
}

#include "CommandBuffer.inl"
