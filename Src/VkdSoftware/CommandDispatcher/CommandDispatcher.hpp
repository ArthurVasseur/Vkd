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
		VkResult operator()(vkd::OpDraw op);
		VkResult operator()(vkd::OpDrawIndexed op);
		VkResult operator()(vkd::OpDrawIndirect op);
		VkResult operator()(vkd::OpDrawIndexedIndirect op);
		VkResult operator()(vkd::OpBindPipeline op);

		CpuContext* m_context;
	};
}

#include "VkdSoftware/CommandDispatcher/CommandDispatcher.inl"
