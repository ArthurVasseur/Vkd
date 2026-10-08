/**
 * @file CommandDispatcher.hpp
 * @brief Command dispatcher for CPU-based rendering
 * @date 2025-10-27
 *
 * Executes recorded command buffer operations on the CPU.
 */

#pragma once

#include "Vkd/Buffer/Buffer.hpp"
#include "Vkd/Image/Image.hpp"
#include "VkdSoftware/CommandBuffer/CommandBuffer.hpp"
#include "VkdSoftware/CpuContext/CpuContext.hpp"

namespace vkd::software
{
	class CommandDispatcher
	{
	public:
		explicit CommandDispatcher(CpuContext& ctx);
		~CommandDispatcher() = default;

		VkResult Execute(const vkd::CommandBuffer& cb);

	private:
		VkResult operator()(vkd::Buffer::OpFill op);
		VkResult operator()(vkd::Buffer::OpCopy op);
		VkResult operator()(vkd::Buffer::OpCopy2 op);
		VkResult operator()(vkd::Buffer::OpUpdate op);
		VkResult operator()(vkd::Buffer::OpCopyBufferToImage op);
		VkResult operator()(vkd::Buffer::OpCopyImageToBuffer op);
		VkResult operator()(vkd::Image::OpCopy op);
		VkResult operator()(vkd::Image::OpClearColorImage op);
		VkResult operator()(vkd::OpBindVertexBuffer op);
		VkResult operator()(vkd::OpBindIndexBuffer op);
		VkResult operator()(vkd::OpBindDescriptorSets op);
		VkResult operator()(vkd::OpDraw op);
		VkResult operator()(vkd::OpDrawIndexed op);
		VkResult operator()(vkd::OpDrawIndirect op);
		VkResult operator()(vkd::OpDrawIndexedIndirect op);
		VkResult operator()(vkd::OpBindPipeline op);
		VkResult operator()(vkd::OpSetViewport op);
		VkResult operator()(vkd::OpSetScissor op);
		VkResult operator()(vkd::OpBeginRenderPass op);
		VkResult operator()(vkd::OpEndRenderPass op);
		VkResult operator()(vkd::OpResetQueryPool op);
		VkResult operator()(vkd::OpBeginQuery op);
		VkResult operator()(vkd::OpEndQuery op);
		VkResult operator()(vkd::OpWriteTimestamp op);
		VkResult operator()(vkd::OpCopyQueryPoolResults op);
		VkResult operator()(vkd::Image::OpClearDepthStencilImage op);
		VkResult operator()(vkd::Image::OpResolve op);
		VkResult operator()(vkd::Image::OpBlit op);
		VkResult operator()(vkd::OpClearAttachments op);
		VkResult operator()(vkd::OpSetEvent op);
		VkResult operator()(vkd::OpWaitEvents op);
		VkResult operator()(vkd::OpExecuteCommands op);
		VkResult operator()(vkd::OpPushConstants op);
		VkResult operator()(vkd::OpDispatch op);

		CpuContext* m_context;
	};
} // namespace vkd::software

#include "VkdSoftware/CommandDispatcher/CommandDispatcher.inl"
