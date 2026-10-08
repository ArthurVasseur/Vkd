/**
 * @file CommandDispatcher.cpp
 * @brief Implementation of command dispatcher
 * @date 2025-10-27
 */

#include "VkdSoftware/CommandDispatcher/CommandDispatcher.hpp"

#include "Vkd/Synchronization/Event/Event.hpp"

namespace vkd::software
{
	VkResult CommandDispatcher::Execute(const vkd::CommandBuffer& cb)
	{
		VKD_AUTO_PROFILER_SCOPE();

		if (!cb.IsSealed())
			return VK_ERROR_VALIDATION_FAILED_EXT;

		const auto& ops = cb.GetOps();
		for (const auto& op : ops)
		{
			VkResult result = std::visit([this]<typename T>(T operation)
										 { return (*this)(std::move(operation)); }, op);

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

	VkResult CommandDispatcher::operator()(vkd::Buffer::OpCopy2 op)
	{
		return m_context->CopyBuffer2(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::Buffer::OpUpdate op)
	{
		return m_context->UpdateBuffer(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::Buffer::OpCopyBufferToImage op)
	{
		return m_context->CopyBufferToImage(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::Buffer::OpCopyImageToBuffer op)
	{
		return m_context->CopyImageToBuffer(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::Image::OpCopy op)
	{
		return m_context->CopyImage(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::Image::OpClearColorImage op)
	{
		return m_context->ClearColorImage(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::OpBindVertexBuffer op)
	{
		return m_context->BindVertexBuffer(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::OpBindIndexBuffer op)
	{
		return m_context->BindIndexBuffer(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::OpBindDescriptorSets op)
	{
		return m_context->BindDescriptorSets(std::move(op));
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

	VkResult CommandDispatcher::operator()(vkd::OpSetViewport op)
	{
		return m_context->SetViewport(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::OpSetScissor op)
	{
		return m_context->SetScissor(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::OpBeginRenderPass op)
	{
		return m_context->BeginRenderPass(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::OpEndRenderPass op)
	{
		return m_context->EndRenderPass(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::OpResetQueryPool op)
	{
		return m_context->ResetQueryPool(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::OpBeginQuery op)
	{
		return m_context->BeginQuery(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::OpEndQuery op)
	{
		return m_context->EndQuery(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::OpWriteTimestamp op)
	{
		return m_context->WriteTimestamp(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::OpCopyQueryPoolResults op)
	{
		return m_context->CopyQueryPoolResults(std::move(op));
	}
	VkResult CommandDispatcher::operator()(vkd::Image::OpClearDepthStencilImage op)
	{
		return m_context->ClearDepthStencilImage(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::Image::OpResolve op)
	{
		return m_context->ResolveImage(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::Image::OpBlit)
	{
		static bool warned = false;
		if (!warned)
		{
			warned = true;
			cct::Logger::Warning("CmdBlitImage: not implemented by the software driver, ignored");
		}
		return VK_SUCCESS;
	}

	VkResult CommandDispatcher::operator()(vkd::OpClearAttachments op)
	{
		return m_context->ClearAttachments(std::move(op));
	}

	VkResult CommandDispatcher::operator()(vkd::OpSetEvent op)
	{
		CCT_ASSERT(op.EventObject, "Invalid event");
		return op.Signaled ? op.EventObject->Set() : op.EventObject->Reset();
	}

	VkResult CommandDispatcher::operator()(vkd::OpWaitEvents op)
	{
		// Commands run in submission order on one thread: events set earlier by this queue are
		// already set, and host-set events must have been set before the submit to be waited on.
		(void)op;
		return VK_SUCCESS;
	}

	VkResult CommandDispatcher::operator()(vkd::OpExecuteCommands op)
	{
		for (const vkd::CommandBuffer* secondary : op.CommandBuffers)
		{
			if (!secondary)
				continue;
			if (const VkResult result = Execute(*secondary); result != VK_SUCCESS)
				return result;
		}
		return VK_SUCCESS;
	}
	VkResult CommandDispatcher::operator()(vkd::OpPushConstants op)
	{
		// The CPU rasterizer doesn't read push constants yet.
		(void)op;
		return VK_SUCCESS;
	}

	VkResult CommandDispatcher::operator()(vkd::OpDispatch op)
	{
		(void)op;
		static bool warned = false;
		if (!warned)
		{
			warned = true;
			cct::Logger::Warning("CmdDispatch: compute is not implemented by the software driver, dispatches are ignored");
		}
		return VK_SUCCESS;
	}
} // namespace vkd::software
