/**
 * @file CommandBuffer.hpp
 * @brief Vulkan command buffer abstraction
 * @date 2025-10-25
 *
 * Represents a command buffer for recording and executing Vulkan commands.
 */

#pragma once

#include <span>
#include <vulkan/vulkan.h>

#include "Vkd/ObjectBase/ObjectBase.hpp"
#include "Vkd/Buffer/Buffer.hpp"
#include "Vkd/CommandBuffer/Ops.hpp"

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
		using Op = Nz::TypeListInstantiate<Nz::TypeListConcat<Buffer::Op, vkd::Op>,std::variant>;

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
		static void VKAPI_CALL CmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo);
		static void VKAPI_CALL CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData);
		static void VKAPI_CALL CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline);
		static void VKAPI_CALL CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets);
		static void VKAPI_CALL CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);

		inline VkResult Begin(const VkCommandBufferBeginInfo& beginInfo);
		inline VkResult End();
		inline VkResult Reset(VkCommandBufferResetFlags flags);
		inline void PushFillBuffer(VkBuffer dst, VkDeviceSize off, VkDeviceSize size, UInt32 data);
		inline void PushCopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, UInt32 regionCount, const VkBufferCopy* pRegions);
		inline void PushCopyBuffer2(VkBuffer srcBuffer, VkBuffer dstBuffer, UInt32 regionCount, const VkBufferCopy2* pRegions);
		inline void PushUpdateBuffer(VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData);
		inline void PushBindPipeline(VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline);
		inline void PushBindVertexBuffer(std::span<const VkBuffer> pBuffers, std::span<const VkDeviceSize> pOffsets, UInt32 firstBinding);
		inline void PushDraw(UInt32 vertexCount, UInt32 instanceCount, UInt32 firstVertex, UInt32 firstInstance);

		inline VkResult MarkSubmitted();
		inline VkResult MarkComplete();

		[[nodiscard]] inline std::span<const Op> GetOps() const;
		[[nodiscard]] inline bool IsSealed() const;

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
