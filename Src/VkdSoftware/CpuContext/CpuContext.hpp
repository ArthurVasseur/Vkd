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
#include <unordered_map>
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
		VkResult CopyBuffer(vkd::Buffer::OpCopy op);
		VkResult CopyBuffer2(vkd::Buffer::OpCopy2 op);
		VkResult UpdateBuffer(vkd::Buffer::OpUpdate op);
		VkResult FillBuffer(vkd::Buffer::OpFill op);
		VkResult CopyBufferToImage(vkd::Buffer::OpCopyBufferToImage op);
		VkResult CopyImageToBuffer(vkd::Buffer::OpCopyImageToBuffer op);
		VkResult CopyImage(vkd::Image::OpCopy op);
		VkResult ClearColorImage(vkd::Image::OpClearColorImage op);
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

		const VkAllocationCallbacks* m_allocationCallbacks;
		vkd::Pipeline* m_boundPipeline = nullptr;
		vkd::vector<Buffer*> m_boundVertexBuffers;
		vkd::vector<VkDeviceSize> m_vertexBufferOffsets;
		Buffer* m_boundIndexBuffer = nullptr;
		VkDeviceSize m_indexBufferOffset = 0;
		VkIndexType m_indexType = VK_INDEX_TYPE_UINT32;
		vkd::vector<vkd::DescriptorSet*> m_boundDescriptorSets;
		std::unordered_map<UInt64, UInt32> m_dynamicOffsets;
		vkd::vector<VkViewport> m_dynamicViewports;
		vkd::vector<VkRect2D> m_dynamicScissors;

		vkd::RenderPass* m_currentRenderPass = nullptr;
		vkd::Framebuffer* m_currentFramebuffer = nullptr;
		VkRect2D m_renderArea = {};
	};
} // namespace vkd::software

#include "VkdSoftware/CpuContext/CpuContext.inl"
