/**
 * @file CpuContext.cpp
 * @brief Implementation of CPU rendering context
 * @date 2025-10-27
 */

#include "VkdSoftware/CpuContext/CpuContext.hpp"

#include "Vkd/DeviceMemory/DeviceMemory.hpp"

#include <vulkan/utility/vk_format_utils.h>

namespace vkd::software
{
CpuContext::CpuContext()
{
}

VkResult CpuContext::BindPipeline(OpBindPipeline op)
{
	VKD_AUTO_PROFILER_SCOPE();

	CCT_ASSERT(op.PipelineObject != nullptr, "Pipeline cannot be null");

	m_boundPipeline = op.PipelineObject;

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

VkResult CpuContext::CopyBuffer2(vkd::Buffer::OpCopy2 op)
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

VkResult CpuContext::UpdateBuffer(vkd::Buffer::OpUpdate op)
{
	VKD_AUTO_PROFILER_SCOPE();

	CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

	cct::UByte* dstData = nullptr;
	op.dst->GetMemory()->Map(op.offset, op.data.size(), reinterpret_cast<void**>(&dstData));

	std::memcpy(dstData, op.data.data(), op.data.size());

	op.dst->GetMemory()->Unmap();

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

VkResult CpuContext::CopyImage(vkd::Image::OpCopy op)
{
	VKD_AUTO_PROFILER_SCOPE();

	for (auto& region : op.regions)
	{
		CCT_ASSERT(op.src && op.src->GetMemory(), "Invalid pointer");
		CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

		VkDeviceSize pixelSize = vkuFormatElementSize(op.src->GetFormat());
		VkDeviceSize srcRowPitch = op.src->GetExtent().width * pixelSize;
		VkDeviceSize dstRowPitch = op.dst->GetExtent().width * pixelSize;

		VkDeviceSize srcImageSize = srcRowPitch * op.src->GetExtent().height * op.src->GetExtent().depth;
		VkDeviceSize dstImageSize = dstRowPitch * op.dst->GetExtent().height * op.dst->GetExtent().depth;

		cct::UByte* srcBase = nullptr;
		op.src->GetMemory()->Map(0, srcImageSize, reinterpret_cast<void**>(&srcBase));

		cct::UByte* dstBase = nullptr;
		op.dst->GetMemory()->Map(0, dstImageSize, reinterpret_cast<void**>(&dstBase));

		VkDeviceSize rowSize = region.extent.width * pixelSize;
		for (UInt32 z = 0; z < region.extent.depth; ++z)
		{
			for (UInt32 y = 0; y < region.extent.height; ++y)
			{
				VkDeviceSize srcOffset = ((region.srcOffset.z + z) * op.src->GetExtent().height +
										  (region.srcOffset.y + y)) *
											 srcRowPitch +
										 region.srcOffset.x * pixelSize;
				VkDeviceSize dstOffset = ((region.dstOffset.z + z) *
										  op.dst->GetExtent().height + (region.dstOffset.y + y)) *
											 dstRowPitch +
										 region.dstOffset.x * pixelSize;
				std::memcpy(dstBase + dstOffset, srcBase + srcOffset, rowSize);
			}
		}

		op.dst->GetMemory()->Unmap();
		op.src->GetMemory()->Unmap();
	}

	return VK_SUCCESS;
}

VkResult CpuContext::CopyBufferToImage(vkd::Buffer::OpCopyBufferToImage op)
{
	VKD_AUTO_PROFILER_SCOPE();

	for (auto& region : op.regions)
	{
		CCT_ASSERT(op.src && op.src->GetMemory(), "Invalid pointer");
		CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

		VkDeviceSize pixelSize = vkuFormatElementSize(op.dst->GetFormat());
		VkDeviceSize imageRowPitch = op.dst->GetExtent().width * pixelSize;
		VkDeviceSize imageSize = imageRowPitch * op.dst->GetExtent().height * op.dst->GetExtent().depth;

		UInt32 bufferRowLength = region.bufferRowLength ? region.bufferRowLength : region.imageExtent.width;
		VkDeviceSize bufferRowPitch = bufferRowLength * pixelSize;

		cct::UByte* srcBase = nullptr;
		op.src->GetMemory()->Map(region.bufferOffset,
								 bufferRowPitch * region.imageExtent.height * region.imageExtent.depth,
								 reinterpret_cast<void**>(&srcBase));

		cct::UByte* dstBase = nullptr;
		op.dst->GetMemory()->Map(0, imageSize, reinterpret_cast<void**>(&dstBase));

		VkDeviceSize rowSize = region.imageExtent.width * pixelSize;
		for (UInt32 z = 0; z < region.imageExtent.depth; ++z)
		{
			for (UInt32 y = 0; y < region.imageExtent.height; ++y)
			{
				VkDeviceSize srcOffset = (z * bufferRowLength * region.imageExtent.height + y * bufferRowLength) *
										 pixelSize;
				VkDeviceSize dstOffset = ((region.imageOffset.z + z) * op.dst->GetExtent().height +
										  (region.imageOffset.y + y)) *
											 imageRowPitch +
										 region.imageOffset.x * pixelSize;

				std::memcpy(dstBase + dstOffset, srcBase + srcOffset, rowSize);
			}
		}

		op.dst->GetMemory()->Unmap();
		op.src->GetMemory()->Unmap();
	}

	return VK_SUCCESS;
}

VkResult CpuContext::CopyImageToBuffer(vkd::Buffer::OpCopyImageToBuffer op)
{
	VKD_AUTO_PROFILER_SCOPE();

	for (auto& region : op.regions)
	{
		CCT_ASSERT(op.src && op.src->GetMemory(), "Invalid pointer");
		CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

		VkDeviceSize pixelSize = vkuFormatElementSize(op.src->GetFormat());
		VkDeviceSize imageRowPitch = op.src->GetExtent().width * pixelSize;
		VkDeviceSize imageSize = imageRowPitch * op.src->GetExtent().height * op.src->GetExtent().depth;

		UInt32 bufferRowLength = region.bufferRowLength ? region.bufferRowLength : region.imageExtent.width;
		VkDeviceSize bufferRowPitch = bufferRowLength * pixelSize;

		cct::UByte* srcBase = nullptr;
		op.src->GetMemory()->Map(0, imageSize, reinterpret_cast<void**>(&srcBase));

		cct::UByte* dstBase = nullptr;
		op.dst->GetMemory()->Map(region.bufferOffset,
								 bufferRowPitch * region.imageExtent.height * region.imageExtent.depth,
								 reinterpret_cast<void**>(&dstBase));

		VkDeviceSize rowSize = region.imageExtent.width * pixelSize;
		for (UInt32 z = 0; z < region.imageExtent.depth; ++z)
		{
			for (UInt32 y = 0; y < region.imageExtent.height; ++y)
			{
				VkDeviceSize srcOffset = ((region.imageOffset.z + z) * op.src->GetExtent().height +
										  (region.imageOffset.y + y)) *
											 imageRowPitch +
										 region.imageOffset.x * pixelSize;
				VkDeviceSize dstOffset = (z * bufferRowLength * region.imageExtent.height + y * bufferRowLength) *
										 pixelSize;

				std::memcpy(dstBase + dstOffset, srcBase + srcOffset, rowSize);
			}
		}

		op.dst->GetMemory()->Unmap();
		op.src->GetMemory()->Unmap();
	}

	return VK_SUCCESS;
}

VkResult CpuContext::ClearColorImage(vkd::Image::OpClearColorImage op)
{
	VKD_AUTO_PROFILER_SCOPE();

	CCT_ASSERT(op.image && op.image->GetMemory(), "Invalid pointer");

	for (auto& range : op.ranges)
	{
		VkDeviceSize pixelSize = vkuFormatElementSize(op.image->GetFormat());
		VkDeviceSize imageSize = op.image->GetExtent().width * op.image->GetExtent().height *
								 op.image->GetExtent().depth * pixelSize;

		cct::UByte* data = nullptr;
		op.image->GetMemory()->Map(0, imageSize, reinterpret_cast<void**>(&data));

		UInt32* data32 = reinterpret_cast<UInt32*>(data);
		UInt32 clearValue = (static_cast<UInt32>(op.clearColor.uint32[3]) << 24) |
							(static_cast<UInt32>(op.clearColor.uint32[2]) << 16) |
							(static_cast<UInt32>(op.clearColor.uint32[1]) << 8) |
							static_cast<UInt32>(op.clearColor.uint32[0]);

		size_t pixelCount = imageSize / pixelSize;
		for (size_t i = 0; i < pixelCount; ++i)
			data32[i] = clearValue;

		op.image->GetMemory()->Unmap();
	}

	return VK_SUCCESS;
}
} // namespace vkd::software
