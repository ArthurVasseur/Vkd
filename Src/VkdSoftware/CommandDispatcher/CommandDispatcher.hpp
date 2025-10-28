//
// Created by arthur on 27/10/2025.
//

#pragma once

#include "Vkd/Buffer/Buffer.hpp"
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
