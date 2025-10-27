#include "CommandDispatcher.hpp"

namespace vkd::software
{
	VkResult CommandDispatcher::Execute(const vkd::CommandBuffer& cb)
	{
		if (!cb.IsSealed())
			return VK_ERROR_VALIDATION_FAILED_EXT;

		const auto& ops = cb.GetOps();
		for (const auto& op : ops)
		{
			VkResult result = std::visit([this]<typename T>(T operation)
			{
				return (*this)(std::move(operation));
			}, op);

			if (result != VK_SUCCESS)
				return result;
		}

		return VK_SUCCESS;
	}

	VkResult CommandDispatcher::operator()(vkd::Buffer::OpFill op)
	{
		return m_context->FillBuffer(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::Buffer::OpCopy op)
	{
		return m_context->CopyBuffer(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::OpBindVertexBuffer op)
	{
		return m_context->BindVertexBuffer(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::OpDraw op)
	{
		return m_context->Draw(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::OpDrawIndexed op)
	{
		// TODO: Implement DrawIndexed in CpuContext
		return VK_SUCCESS;
	}

	VkResult CommandDispatcher::operator()(vkd::OpDrawIndirect op)
	{
		// TODO: Implement DrawIndirect in CpuContext
		return VK_SUCCESS;
	}

	VkResult CommandDispatcher::operator()(vkd::OpDrawIndexedIndirect op)
	{
		// TODO: Implement DrawIndexedIndirect in CpuContext
		return VK_SUCCESS;
	}

	VkResult CommandDispatcher::operator()(vkd::OpBindPipeline op)
	{
		return m_context->BindPipeline(std::move(op));
	}
}
