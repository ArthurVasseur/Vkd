/**
 * @file CpuContext.hpp
 * @brief CPU rendering context for software rasterization
 * @date 2025-10-27
 *
 * Maintains the state for CPU-based rendering operations including vertex buffers,
 * pipeline state, and viewport configuration.
 */

#pragma once

#include <functional>
#include <vector>

#include "Vkd/Buffer/Buffer.hpp"
#include "Vkd/CommandBuffer/Ops.hpp"
#include "Vkd/Framebuffer/Framebuffer.hpp"
#include "Vkd/Image/Image.hpp"
#include "Vkd/RenderPass/RenderPass.hpp"

namespace vkd::software
{
	class Pipeline;

	class CpuContext
	{
	public:
		explicit CpuContext(const VkAllocationCallbacks& allocationCallbacks);
		~CpuContext() = default;

		VkResult BindPipeline(OpBindPipeline op);
		VkResult BindVertexBuffer(OpBindVertexBuffer buffer);
		VkResult BindIndexBuffer(OpBindIndexBuffer op);
		VkResult BindDescriptorSets(OpBindDescriptorSets op);
		VkResult SetViewport(OpSetViewport op);
		VkResult SetScissor(OpSetScissor op);
		VkResult Draw(vkd::OpDraw op);
		VkResult DrawIndexed(vkd::OpDrawIndexed op);
		VkResult CopyBuffer(vkd::Buffer::OpCopy op);
		VkResult CopyBuffer2(vkd::Buffer::OpCopy2 op);
		VkResult UpdateBuffer(vkd::Buffer::OpUpdate op);
		VkResult FillBuffer(vkd::Buffer::OpFill op);
		VkResult CopyBufferToImage(vkd::Buffer::OpCopyBufferToImage op);
		VkResult CopyImageToBuffer(vkd::Buffer::OpCopyImageToBuffer op);
		VkResult CopyImage(vkd::Image::OpCopy op);
		VkResult ClearColorImage(vkd::Image::OpClearColorImage op);
		VkResult ClearDepthStencilImage(vkd::Image::OpClearDepthStencilImage op);
		VkResult ClearAttachments(vkd::OpClearAttachments op);
		VkResult ResolveImage(vkd::Image::OpResolve op);
		VkResult BeginRenderPass(vkd::OpBeginRenderPass op);
		VkResult EndRenderPass(vkd::OpEndRenderPass op);
		VkResult ResetQueryPool(vkd::OpResetQueryPool op);
		VkResult BeginQuery(vkd::OpBeginQuery op);
		VkResult EndQuery(vkd::OpEndQuery op);
		VkResult WriteTimestamp(vkd::OpWriteTimestamp op);
		VkResult CopyQueryPoolResults(vkd::OpCopyQueryPoolResults op);

		inline void Reset();

	private:
		// Restricted to `area` if given (clamped to the image bounds); nullptr clears the whole image.
		static VkResult ClearImageWithColor(vkd::Image* image, const VkClearColorValue& color, const VkRect2D* area = nullptr);

		// Shared by Draw/DrawIndexed: everything from pipeline/render-pass validation through rasterization,
		// parameterized only by the primitive count and how to turn a primitive-relative index (0..count-1)
		// into the actual vertex index (identity for Draw, an index-buffer lookup for DrawIndexed).
		VkResult DrawPrimitives(UInt32 vertexCount, UInt32 instanceCount, UInt32 firstInstance, const std::function<UInt32(UInt32)>& resolveVertexIndex);

		vkd::Pipeline* m_boundPipeline = nullptr;
		std::vector<Buffer*> m_boundVertexBuffers;
		std::vector<VkDeviceSize> m_vertexBufferOffsets;
		Buffer* m_boundIndexBuffer = nullptr;
		VkDeviceSize m_indexBufferOffset = 0;
		VkIndexType m_indexType = VK_INDEX_TYPE_UINT32;
		std::vector<vkd::DescriptorSet*> m_boundDescriptorSets;
		std::vector<VkViewport> m_dynamicViewports;
		std::vector<VkRect2D> m_dynamicScissors;

		vkd::RenderPass* m_currentRenderPass = nullptr;
		vkd::Framebuffer* m_currentFramebuffer = nullptr;
		VkRect2D m_renderArea = {};
	};
} // namespace vkd::software

#include "VkdSoftware/CpuContext/CpuContext.inl"
