//
// Created by arthur on 25/10/2025.
//

#pragma once

#include <vulkan/vulkan.h>

#include "Vkd/ObjectBase/ObjectBase.hpp"
#include "Vkd/Buffer/Buffer.hpp"

namespace vkd
{
	class CommandPool;

	class CommandBuffer : public ObjectBase
	{
	public:
		enum class State
		{
			Initial,
			Recording,
			Executable,
			Pending,
			Invalid
		};
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
		static void VKAPI_CALL CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data);
		static void VKAPI_CALL CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions);

		virtual VkResult Begin(const VkCommandBufferBeginInfo& beginInfo) = 0;
		virtual VkResult End() = 0;
		virtual VkResult Reset(VkCommandBufferResetFlags flags) = 0;
		inline void PushFill(VkBuffer dst, VkDeviceSize off, VkDeviceSize size, uint32_t data);
		inline void PushCopy(VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions);


		inline VkResult MarkSubmitted();
		inline VkResult MarkComplete();

		using Op = Nz::TypeListInstantiate<Buffer::Op, std::variant>;
	protected:
		inline VkResult Transition(State to, std::initializer_list<State> allowed);
	private:
		CommandPool* m_owner;
		VkCommandBufferLevel m_level;
		State m_state;
		std::vector<Op> m_ops;
	};
}

#include "Vkd/CommandBuffer/CommandBuffer.inl"
