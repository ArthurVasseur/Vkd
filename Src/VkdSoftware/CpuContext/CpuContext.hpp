/**
 * @file CpuContext.hpp
 * @brief CPU rendering context for software rasterization
 * @date 2025-10-27
 *
 * Maintains the state for CPU-based rendering operations including vertex buffers,
 * pipeline state, and viewport configuration.
 */

#pragma once

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
		CpuContext();
		~CpuContext() = default;

		VkResult BindPipeline(OpBindPipeline op);
		VkResult BindVertexBuffer(OpBindVertexBuffer buffer);
		VkResult BindDescriptorSets(OpBindDescriptorSets op);
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

		inline void Reset();

	private:
		// Restricted to `area` if given (clamped to the image bounds); nullptr clears the whole image.
		static VkResult ClearImageWithColor(vkd::Image* image, const VkClearColorValue& color, const VkRect2D* area = nullptr);

		vkd::Pipeline* m_boundPipeline = nullptr;
		std::vector<Buffer*> m_boundVertexBuffers;
		std::vector<VkDeviceSize> m_vertexBufferOffsets;
		std::vector<vkd::DescriptorSet*> m_boundDescriptorSets;

		vkd::RenderPass* m_currentRenderPass = nullptr;
		vkd::Framebuffer* m_currentFramebuffer = nullptr;
		VkRect2D m_renderArea = {};
	};
} // namespace vkd::software

#include "VkdSoftware/CpuContext/CpuContext.inl"
