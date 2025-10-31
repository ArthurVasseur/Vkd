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
		VkResult Draw(vkd::OpDraw op);
		VkResult CopyBuffer(vkd::Buffer::OpCopy op);
		VkResult FillBuffer(vkd::Buffer::OpFill op);

		inline void Reset();

	private:
		vkd::Pipeline* m_boundPipeline = nullptr;
		std::vector<Buffer*> m_boundVertexBuffers;
		std::vector<VkDeviceSize> m_vertexBufferOffsets;
	};
}

#include "VkdSoftware/CpuContext/CpuContext.inl"
