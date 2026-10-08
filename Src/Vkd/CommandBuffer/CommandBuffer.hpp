/**
 * @file CommandBuffer.hpp
 * @brief Vulkan command buffer abstraction
 * @date 2025-10-25
 *
 * Represents a command buffer for recording and executing Vulkan commands.
 */

#pragma once

#include <span>

#include "Vkd/Buffer/Buffer.hpp"
#include "Vkd/CommandBuffer/Ops.hpp"
#include "Vkd/Image/Image.hpp"
#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

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
		using BufferImageOps = Nz::TypeListConcat<Buffer::Op, Image::Op>;
		using AllOps = Nz::TypeListConcat<BufferImageOps, vkd::Op>;
		using Op = Nz::TypeListInstantiate<AllOps, std::variant>;

		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_COMMAND_BUFFER;
		VKD_DISPATCHABLE_HANDLE(CommandBuffer);

		explicit CommandBuffer(const VkAllocationCallbacks& allocationCallbacks);
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
		static void VKAPI_CALL CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions);
		static void VKAPI_CALL CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter);
		static void VKAPI_CALL CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions);
		static void VKAPI_CALL CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions);
		static void VKAPI_CALL CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges);
		static void VKAPI_CALL CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline);
		static void VKAPI_CALL CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets);
		static void VKAPI_CALL CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);
		static void VKAPI_CALL CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets);
		static void VKAPI_CALL CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues);
		static void VKAPI_CALL CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers);
		static void VKAPI_CALL CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
		static void VKAPI_CALL CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
		static void VKAPI_CALL CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride);
		static void VKAPI_CALL CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride);
		static void VKAPI_CALL CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
		static void VKAPI_CALL CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset);
		static void VKAPI_CALL CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports);
		static void VKAPI_CALL CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors);
		static void VKAPI_CALL CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth);
		static void VKAPI_CALL CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor);
		static void VKAPI_CALL CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]);
		static void VKAPI_CALL CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds);
		static void VKAPI_CALL CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask);
		static void VKAPI_CALL CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask);
		static void VKAPI_CALL CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference);
		static void VKAPI_CALL CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents);
		static void VKAPI_CALL CmdEndRenderPass(VkCommandBuffer commandBuffer);
		static void VKAPI_CALL CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents);
		static void VKAPI_CALL CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers);
		static void VKAPI_CALL CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount);
		static void VKAPI_CALL CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags);
		static void VKAPI_CALL CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query);
		static void VKAPI_CALL CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query);
		static void VKAPI_CALL CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags);
		static void VKAPI_CALL CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges);
		static void VKAPI_CALL CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects);
		static void VKAPI_CALL CmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions);
		static void VKAPI_CALL CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask);
		static void VKAPI_CALL CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask);
		static void VKAPI_CALL CmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers);

		inline VkResult Begin(const VkCommandBufferBeginInfo& beginInfo);
		inline VkResult End();
		inline VkResult Reset(VkCommandBufferResetFlags flags);
		inline void PushFillBuffer(VkBuffer dst, VkDeviceSize off, VkDeviceSize size, UInt32 data);
		inline void PushCopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, UInt32 regionCount, const VkBufferCopy* pRegions);
		inline void PushCopyBuffer2(VkBuffer srcBuffer, VkBuffer dstBuffer, UInt32 regionCount, const VkBufferCopy2* pRegions);
		inline void PushUpdateBuffer(VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData);
		inline void PushCopyImage(VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, UInt32 regionCount, const VkImageCopy* pRegions);
		inline void PushCopyBufferToImage(VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, UInt32 regionCount, const VkBufferImageCopy* pRegions);
		inline void PushCopyImageToBuffer(VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, UInt32 regionCount, const VkBufferImageCopy* pRegions);
		inline void PushClearColorImage(VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, UInt32 rangeCount, const VkImageSubresourceRange* pRanges);
		inline void PushBindPipeline(VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline);
		inline void PushBindVertexBuffer(std::span<const VkBuffer> pBuffers, std::span<const VkDeviceSize> pOffsets, UInt32 firstBinding);
		inline void PushBindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);
		inline void PushBindDescriptorSets(VkPipelineBindPoint pipelineBindPoint, UInt32 firstSet, std::span<const VkDescriptorSet> pDescriptorSets, std::span<const UInt32> pDynamicOffsets);
		inline void PushDraw(UInt32 vertexCount, UInt32 instanceCount, UInt32 firstVertex, UInt32 firstInstance);
		inline void PushDrawIndexed(UInt32 indexCount, UInt32 instanceCount, UInt32 firstIndex, cct::Int32 vertexOffset, UInt32 firstInstance);
		inline void PushSetViewport(std::span<const VkViewport> pViewports, UInt32 firstViewport);
		inline void PushSetScissor(std::span<const VkRect2D> pScissors, UInt32 firstScissor);
		inline void PushBeginRenderPass(const VkRenderPassBeginInfo& beginInfo, VkSubpassContents contents);
		inline void PushEndRenderPass();
		inline void PushResetQueryPool(VkQueryPool queryPool, UInt32 firstQuery, UInt32 queryCount);
		inline void PushBeginQuery(VkQueryPool queryPool, UInt32 query, VkQueryControlFlags flags);
		inline void PushEndQuery(VkQueryPool queryPool, UInt32 query);
		inline void PushWriteTimestamp(VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, UInt32 query);
		inline void PushCopyQueryPoolResults(VkQueryPool queryPool, UInt32 firstQuery, UInt32 queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags);
		inline void PushClearDepthStencilImage(VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue& value, UInt32 rangeCount, const VkImageSubresourceRange* pRanges);
		inline void PushClearAttachments(UInt32 attachmentCount, const VkClearAttachment* pAttachments, UInt32 rectCount, const VkClearRect* pRects);
		inline void PushResolveImage(VkImage srcImage, VkImage dstImage, UInt32 regionCount, const VkImageResolve* pRegions);
		inline void PushSetEvent(VkEvent event, bool signaled);
		inline void PushWaitEvents(UInt32 eventCount, const VkEvent* pEvents);
		inline void PushExecuteCommands(UInt32 commandBufferCount, const VkCommandBuffer* pCommandBuffers);
		inline void PushPushConstants(VkShaderStageFlags stageFlags, UInt32 offset, UInt32 size, const void* pValues);
		inline void PushDispatch(UInt32 groupCountX, UInt32 groupCountY, UInt32 groupCountZ);

		inline VkResult MarkSubmitted();
		inline VkResult MarkComplete();

		[[nodiscard]] inline std::span<const Op> GetOps() const;
		[[nodiscard]] inline bool IsSealed() const;

	protected:
		inline VkResult Transition(State to, std::initializer_list<State> allowed);

		template<typename OpT>
		inline void Record(OpT&& op);

	private:
		CommandPool* m_owner;
		VkCommandBufferLevel m_level;
		State m_state;
		VkCommandBufferUsageFlags m_usageFlags;
		UInt32 m_pendingSubmits;
		vkd::vector<Op> m_ops;
		VkResult m_recordingError;
	};
} // namespace vkd

#include "Vkd/CommandBuffer/CommandBuffer.inl"
