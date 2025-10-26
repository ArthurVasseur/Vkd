#include "CpuContext.hpp"

#include "Vkd/DeviceMemory/DeviceMemory.hpp"

namespace vkd::software
{
	CpuContext::CpuContext() :
		m_boundPipeline(nullptr),
		m_boundVertexBuffer(nullptr),
		m_vertexBufferOffset(0)
	{
	}

	VkResult CpuContext::BindPipeline(const OpBindPipeline& op)
	{
		VKD_AUTO_PROFILER_SCOPE;

		m_boundPipeline = op.Pipeline;
		return VK_SUCCESS;
	}

	VkResult CpuContext::BindVertexBuffer(const OpBindVertexBuffer& op)
	{
		return VK_SUCCESS;
	}


	VkResult CpuContext::Draw(uint32_t vertexCount, uint32_t firstVertex)
	{
		VKD_AUTO_PROFILER_SCOPE;

		if (m_boundPipeline == nullptr)
			return VK_ERROR_VALIDATION_FAILED_EXT;

		return VK_SUCCESS;
	}

	VkResult CpuContext::CopyBuffer(const vkd::Buffer::OpCopy& op)
	{
		VKD_AUTO_PROFILER_SCOPE;

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

	VkResult CpuContext::FillBuffer(const vkd::Buffer::OpFill& op)
	{
		VKD_AUTO_PROFILER_SCOPE;

		cct::UByte* data = nullptr;
		op.dst->GetMemory()->Map(op.offset, op.size, reinterpret_cast<void**>(&data));

		cct::UInt32* data32 = reinterpret_cast<cct::UInt32*>(data);
		size_t count = op.size / sizeof(cct::UInt32);
		for (size_t i = 0; i < count; ++i)
			data32[i] = op.data;

		op.dst->GetMemory()->Unmap();

		return VK_SUCCESS;
	}
}
