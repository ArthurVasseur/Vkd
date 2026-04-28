/**
 * @file CpuContext.cpp
 * @brief Implementation of CPU rendering context
 * @date 2025-10-27
 */

#include "VkdSoftware/CpuContext/CpuContext.hpp"

#include "Vkd/DeviceMemory/DeviceMemory.hpp"
#include "Vkd/ImageView/ImageView.hpp"

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
												  op.dst->GetExtent().height +
											  (region.dstOffset.y + y)) *
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
			(void)range;
			VkResult result = ClearImageWithColor(op.image, op.clearColor);
			if (result != VK_SUCCESS)
				return result;
		}

		return VK_SUCCESS;
	}

	VkResult CpuContext::BeginRenderPass(vkd::OpBeginRenderPass op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.RenderPassObject && op.FramebufferObject, "Invalid render pass / framebuffer");

		m_currentRenderPass = op.RenderPassObject;
		m_currentFramebuffer = op.FramebufferObject;
		m_renderArea = op.RenderArea;

		const auto& attachments = op.RenderPassObject->GetAttachments();
		const auto& views = op.FramebufferObject->GetAttachments();

		const std::size_t count = std::min(attachments.size(), views.size());
		for (std::size_t i = 0; i < count; ++i)
		{
			if (attachments[i].loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR)
				continue;

			if (i >= op.ClearValues.size())
			{
				cct::Logger::Warning("BeginRenderPass: missing clear value for attachment {}", i);
				continue;
			}

			VKD_FROM_HANDLE(vkd::ImageView, viewObj, views[i]);
			VKD_FROM_HANDLE(vkd::Image, imageObj, viewObj->GetImage());

			VkResult result = ClearImageWithColor(imageObj, op.ClearValues[i].color);
			if (result != VK_SUCCESS)
				return result;
		}

		return VK_SUCCESS;
	}

	VkResult CpuContext::EndRenderPass(vkd::OpEndRenderPass op)
	{
		VKD_AUTO_PROFILER_SCOPE();
		(void)op;

		// storeOp == STORE is implicit since we wrote directly to image memory.
		// storeOp == DONT_CARE could be optimized later; for now we keep the data.
		m_currentRenderPass = nullptr;
		m_currentFramebuffer = nullptr;
		m_renderArea = {};

		return VK_SUCCESS;
	}

	namespace
	{
		// Float in [0,1] -> 8-bit unorm.
		UInt8 FloatToUNorm8(float f)
		{
			if (f <= 0.0f)
				return 0;
			if (f >= 1.0f)
				return 255;
			return static_cast<UInt8>(f * 255.0f + 0.5f);
		}

		// Build the 4-byte RGBA pattern for the given format.
		// Returns true if the format is supported by this fast path.
		bool BuildRgba8ClearPattern(VkFormat format, const VkClearColorValue& color, UInt8 outPattern[4])
		{
			switch (format)
			{
				case VK_FORMAT_R8G8B8A8_UNORM:
				case VK_FORMAT_R8G8B8A8_SRGB:
					outPattern[0] = FloatToUNorm8(color.float32[0]);
					outPattern[1] = FloatToUNorm8(color.float32[1]);
					outPattern[2] = FloatToUNorm8(color.float32[2]);
					outPattern[3] = FloatToUNorm8(color.float32[3]);
					return true;
				case VK_FORMAT_B8G8R8A8_UNORM:
				case VK_FORMAT_B8G8R8A8_SRGB:
					outPattern[0] = FloatToUNorm8(color.float32[2]);
					outPattern[1] = FloatToUNorm8(color.float32[1]);
					outPattern[2] = FloatToUNorm8(color.float32[0]);
					outPattern[3] = FloatToUNorm8(color.float32[3]);
					return true;
				case VK_FORMAT_R8G8B8A8_UINT:
				case VK_FORMAT_R8G8B8A8_SINT:
					outPattern[0] = static_cast<UInt8>(color.uint32[0] & 0xFFu);
					outPattern[1] = static_cast<UInt8>(color.uint32[1] & 0xFFu);
					outPattern[2] = static_cast<UInt8>(color.uint32[2] & 0xFFu);
					outPattern[3] = static_cast<UInt8>(color.uint32[3] & 0xFFu);
					return true;
				default:
					return false;
			}
		}
	} // namespace

	VkResult CpuContext::ClearImageWithColor(vkd::Image* image, const VkClearColorValue& color)
	{
		CCT_ASSERT(image && image->GetMemory(), "Invalid image / memory");

		const VkFormat format = image->GetFormat();
		const VkDeviceSize pixelSize = vkuFormatElementSize(format);
		if (pixelSize == 0)
			return VK_ERROR_FORMAT_NOT_SUPPORTED;

		const VkExtent3D extent = image->GetExtent();
		const VkDeviceSize imageSize = static_cast<VkDeviceSize>(extent.width) * extent.height * extent.depth * pixelSize;

		cct::UByte* data = nullptr;
		VkResult mapResult = image->GetMemory()->Map(0, imageSize, reinterpret_cast<void**>(&data));
		if (mapResult != VK_SUCCESS || data == nullptr)
			return mapResult != VK_SUCCESS ? mapResult : VK_ERROR_MEMORY_MAP_FAILED;

		UInt8 pattern[4] = {0, 0, 0, 0};
		const bool supported = pixelSize == 4 && BuildRgba8ClearPattern(format, color, pattern);

		if (supported)
		{
			UInt32 clearWord;
			std::memcpy(&clearWord, pattern, sizeof(clearWord));
			UInt32* data32 = reinterpret_cast<UInt32*>(data);
			const std::size_t wordCount = static_cast<std::size_t>(imageSize / sizeof(UInt32));
			for (std::size_t i = 0; i < wordCount; ++i)
				data32[i] = clearWord;
		}
		else
		{
			cct::Logger::Warning("ClearImageWithColor: unsupported format {}, zeroing memory", static_cast<int>(format));
			std::memset(data, 0, static_cast<std::size_t>(imageSize));
		}

		image->GetMemory()->Unmap();
		return VK_SUCCESS;
	}
} // namespace vkd::software
