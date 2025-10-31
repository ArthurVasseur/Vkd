/**
 * @file CpuContext.cpp
 * @brief Implementation of CPU rendering context
 * @date 2025-10-27
 */

#include "Vkd/DeviceMemory/DeviceMemory.hpp"
#include "VkdSoftware/CpuContext/CpuContext.hpp"

namespace vkd::software
{
	CpuContext::CpuContext()
	{
	}

	VkResult CpuContext::BindPipeline(OpBindPipeline op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.Pipeline != nullptr, "Pipeline cannot be null");

		m_boundPipeline = op.Pipeline;

		return VK_SUCCESS;
	}

	VkResult CpuContext::BindVertexBuffer(OpBindVertexBuffer op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(!op.Buffers.empty(), "No vertex buffers provided");
		CCT_ASSERT(!op.Offsets.empty(), "No offsets provided");
		CCT_ASSERT(op.Buffers.size() == op.Offsets.size(), "Buffers and offsets size mismatch");

		const UInt32 maxBinding = op.FirstBinding + static_cast<UInt32>(op.Buffers.size());
		if (maxBinding > m_boundVertexBuffers.size())
		{
			m_boundVertexBuffers.resize(maxBinding, nullptr);
			m_vertexBufferOffsets.resize(maxBinding, 0);
		}

		for (size_t i = 0; i < op.Buffers.size(); ++i)
		{
			const UInt32 binding = op.FirstBinding + static_cast<UInt32>(i);
			m_boundVertexBuffers[binding] = op.Buffers[i];
			m_vertexBufferOffsets[binding] = op.Offsets[i];
		}

		return VK_SUCCESS;
	}


	VkResult CpuContext::Draw(vkd::OpDraw op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		return VK_SUCCESS;
	}

	VkResult CpuContext::CopyBuffer(vkd::Buffer::OpCopy op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		for (auto& region : op.regions)
		{
			CCT_ASSERT(op.src && op.src->GetMemory(), "Invalid pointer");
			CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

			cct::UByte* srcData = nullptr;
			op.src->GetMemory()->Map(region.srcOffset, region.size, reinterpret_cast<void**>(&srcData));

			cct::UByte* dstData = nullptr;
			op.dst->GetMemory()->Map(region.dstOffset, region.size, reinterpret_cast<void**>(&dstData));

			std::memcpy(dstData, srcData, region.size);

			op.dst->GetMemory()->Unmap();
			op.src->GetMemory()->Unmap();
		}

		return VK_SUCCESS;
	}

	VkResult CpuContext::FillBuffer(vkd::Buffer::OpFill op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		cct::UByte* data = nullptr;
		op.dst->GetMemory()->Map(op.offset, op.size, reinterpret_cast<void**>(&data));

		UInt32* data32 = reinterpret_cast<UInt32*>(data);
		size_t count = op.size / sizeof(UInt32);
		for (size_t i = 0; i < count; ++i)
			data32[i] = op.data;

		op.dst->GetMemory()->Unmap();

		return VK_SUCCESS;
	}
}
