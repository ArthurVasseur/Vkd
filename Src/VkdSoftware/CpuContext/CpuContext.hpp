#pragma once

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

		VkResult BindPipeline(const OpBindPipeline& op);
		VkResult BindVertexBuffer(const OpBindVertexBuffer& buffer);
		VkResult Draw(uint32_t vertexCount, uint32_t firstVertex);
		VkResult CopyBuffer(const vkd::Buffer::OpCopy& op);
		VkResult FillBuffer(const vkd::Buffer::OpFill& op);

		inline void Reset();

	private:
		vkd::Pipeline* m_boundPipeline;
		Buffer* m_boundVertexBuffer;
		size_t m_vertexBufferOffset;
	};
}

#include "VkdSoftware/CpuContext/CpuContext.inl"
