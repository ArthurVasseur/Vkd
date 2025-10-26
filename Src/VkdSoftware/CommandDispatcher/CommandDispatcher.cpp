#include "CommandDispatcher.hpp"

namespace vkd::software
{
	VkResult CommandDispatcher::Execute(const CommandBuffer& cb)
	{
		if (!cb.IsSealed())
			return VK_ERROR_VALIDATION_FAILED_EXT;

		const auto& ops = cb.GetOps();
		for (const auto& op : ops)
		{
			VkResult result = std::visit([this](const auto& operation)
			{
				return (*this)(operation);
			}, op);

			if (result != VK_SUCCESS)
				return result;
		}

		return VK_SUCCESS;
	}

	VkResult CommandDispatcher::operator()(const vkd::Buffer::OpFill& op)
	{
		return m_context.FillBuffer(op);
	}

	VkResult CommandDispatcher::operator()(const vkd::Buffer::OpCopy& op)
	{
		return m_context.CopyBuffer(op);
	}

	//VkResult CommandDispatcher::operator()(const Op_BindPipeline& op)
	//{
	//	return m_context.BindPipeline(op.pipeline);
	//}

	//VkResult CommandDispatcher::operator()(const Op_BindVertexBuffer& op)
	//{
	//	return m_context.BindVertexBuffer(op.buffer, op.offset);
	//}

	//VkResult CommandDispatcher::operator()(const Op_Draw& op)
	//{
	//	return m_context.Draw(op.vertexCount, op.firstVertex);
	//}

	//VkResult CommandDispatcher::operator()(const Op_CopyBuffer& op)
	//{
	//	return m_context.CopyBuffer(op.src, op.dst, op.size, op.srcOffset, op.dstOffset);
	//}
}
