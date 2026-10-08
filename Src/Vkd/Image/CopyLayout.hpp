/**
 * @file CopyLayout.hpp
 * @brief Byte ranges touched by image copies/clears, shared by the software and AMD drivers
 * @date 2026-10-07
 *
 * Vkd images are linear whatever their tiling (Image::GetSubresourceLayout): layer-major, each
 * layer holding its whole mip chain, each mip tightly packed. Every helper here reduces a
 * Vulkan region to a list of contiguous rows, given as byte offsets relative to the start of the
 * bound VkDeviceMemory, so a driver only has to move bytes (memcpy on the CPU, CP DMA on AMD) and
 * both drivers agree byte for byte.
 */

#pragma once

#include <algorithm>
#include <cstdint>

#include "Vkd/Buffer/Buffer.hpp"
#include "Vkd/Image/Image.hpp"

#include <vulkan/utility/vk_format_utils.h>
#include <vulkan/vulkan.h>

namespace vkd
{
	struct SubresourceFootprint
	{
		VkDeviceSize offset; // from the start of the bound memory
		VkDeviceSize rowPitch; // bytes per row
		VkDeviceSize depthPitch; // bytes per z slice
		VkExtent3D extent; // mip extent
	};

	inline SubresourceFootprint GetSubresourceFootprint(const Image& image, uint32_t mipLevel, uint32_t arrayLayer)
	{
		VkSubresourceLayout layout{};
		image.GetSubresourceLayout(VkImageSubresource{VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, arrayLayer}, layout);
		const VkExtent3D extent = image.GetExtent();
		return SubresourceFootprint{
			image.GetMemoryOffset() + layout.offset,
			layout.rowPitch,
			layout.depthPitch,
			VkExtent3D{std::max(extent.width >> mipLevel, 1u), std::max(extent.height >> mipLevel, 1u),
					   std::max(extent.depth >> mipLevel, 1u)}};
	}

	inline uint32_t ResolveLayerCount(const Image& image, uint32_t baseArrayLayer, uint32_t layerCount)
	{
		if (layerCount != VK_REMAINING_ARRAY_LAYERS)
			return layerCount;
		return baseArrayLayer < image.GetArrayLayers() ? image.GetArrayLayers() - baseArrayLayer : 0;
	}

	inline uint32_t ResolveLevelCount(const Image& image, uint32_t baseMipLevel, uint32_t levelCount)
	{
		if (levelCount != VK_REMAINING_MIP_LEVELS)
			return levelCount;
		return baseMipLevel < image.GetMipLevels() ? image.GetMipLevels() - baseMipLevel : 0;
	}

	// Bytes per texel of one aspect. Copies of a color or single-aspect depth format move whole
	// elements; a single aspect of a combined depth/stencil format is not addressable as rows in the
	// packed storage, so it reports 0 (unsupported).
	inline uint32_t CopyTexelSize(VkFormat format, VkImageAspectFlags aspect)
	{
		if (vkuFormatIsDepthAndStencil(format))
			return 0;
		(void)aspect;
		return vkuFormatElementSize(format);
	}

	// Texel blocks covering `texels` texels along one dimension of `blockSize`-texel blocks.
	inline uint32_t BlockCount(uint32_t texels, uint32_t blockSize)
	{
		return (texels + blockSize - 1) / blockSize;
	}

	// Byte offset of element (x, y, z) of one array layer / mip of an image: a texel, or a texel
	// block for a compressed format (x, y, z then count blocks).
	inline VkDeviceSize ImageTexelOffset(const SubresourceFootprint& fp, uint32_t x, uint32_t y, uint32_t z, uint32_t texelSize)
	{
		return fp.offset + VkDeviceSize{z} * fp.depthPitch + VkDeviceSize{y} * fp.rowPitch + VkDeviceSize{x} * texelSize;
	}

	// vkCmdCopyBufferToImage / vkCmdCopyImageToBuffer: calls row(bufferOffset, imageOffset, bytes) for
	// every row of the region. Buffer addressing follows the spec: rows of bufferRowLength texels,
	// slices of bufferImageHeight rows, array layers after the region's depth slices.
	// Returns false for a region the linear model can't express (unsupported aspect/format).
	template<typename RowFn>
	bool ForEachBufferImageRow(const Buffer& buffer, const Image& image, const VkBufferImageCopy& region, RowFn&& row)
	{
		const uint32_t texelSize = CopyTexelSize(image.GetFormat(), region.imageSubresource.aspectMask);
		if (texelSize == 0)
			return false;

		// Everything in texel blocks: the region and the buffer's row length / image height are in
		// texels, a compressed format's rows are rows of blocks.
		const VkImageSubresourceLayers& sub = region.imageSubresource;
		const uint32_t layerCount = ResolveLayerCount(image, sub.baseArrayLayer, sub.layerCount);
		const VkExtent3D block = vkuFormatTexelBlockExtent(image.GetFormat());
		const uint32_t width = BlockCount(region.imageExtent.width, block.width);
		const uint32_t height = BlockCount(region.imageExtent.height, block.height);
		const uint32_t depth = BlockCount(region.imageExtent.depth, block.depth);
		const VkDeviceSize rowLength = BlockCount(region.bufferRowLength ? region.bufferRowLength : region.imageExtent.width, block.width);
		const VkDeviceSize imageHeight = BlockCount(region.bufferImageHeight ? region.bufferImageHeight : region.imageExtent.height, block.height);
		const uint32_t x0 = static_cast<uint32_t>(region.imageOffset.x) / block.width;
		const uint32_t y0 = static_cast<uint32_t>(region.imageOffset.y) / block.height;
		const uint32_t z0 = static_cast<uint32_t>(region.imageOffset.z) / block.depth;
		const VkDeviceSize rowBytes = VkDeviceSize{width} * texelSize;
		const VkDeviceSize bufferBase = buffer.GetMemoryOffset() + region.bufferOffset;

		for (uint32_t layer = 0; layer < layerCount; ++layer)
		{
			const SubresourceFootprint fp = GetSubresourceFootprint(image, sub.mipLevel, sub.baseArrayLayer + layer);
			for (uint32_t z = 0; z < depth; ++z)
			{
				const VkDeviceSize slice = VkDeviceSize{layer} * depth + z;
				for (uint32_t y = 0; y < height; ++y)
				{
					const VkDeviceSize bufferOffset = bufferBase + (slice * imageHeight + y) * rowLength * texelSize;
					const VkDeviceSize imageOffset = ImageTexelOffset(fp, x0, y0 + y, z0 + z, texelSize);
					row(bufferOffset, imageOffset, rowBytes);
				}
			}
		}
		return true;
	}

	// vkCmdCopyImage: calls row(srcOffset, dstOffset, bytes) for every row. A 3D image's z slices pair
	// with the other image's array layers (VK_KHR_maintenance1), so a region is walked as
	// max(depth, layerCount) 2D slices on both sides.
	template<typename RowFn>
	bool ForEachImageCopyRow(const Image& src, const Image& dst, const VkImageCopy& region, RowFn&& row)
	{
		const uint32_t texelSize = CopyTexelSize(src.GetFormat(), region.srcSubresource.aspectMask);
		if (texelSize == 0 || texelSize != CopyTexelSize(dst.GetFormat(), region.dstSubresource.aspectMask))
			return false;

		// In texel blocks. The extent is in source texels, also when copying between a compressed
		// and an uncompressed format of the same block size; each side's offsets are in its own texels.
		const uint32_t srcLayers = ResolveLayerCount(src, region.srcSubresource.baseArrayLayer, region.srcSubresource.layerCount);
		const VkExtent3D srcBlock = vkuFormatTexelBlockExtent(src.GetFormat());
		const VkExtent3D dstBlock = vkuFormatTexelBlockExtent(dst.GetFormat());
		const uint32_t width = BlockCount(region.extent.width, srcBlock.width);
		const uint32_t height = BlockCount(region.extent.height, srcBlock.height);
		const uint32_t sliceCount = std::max(BlockCount(region.extent.depth, srcBlock.depth), srcLayers);
		const bool src3d = src.GetImageType() == VK_IMAGE_TYPE_3D;
		const bool dst3d = dst.GetImageType() == VK_IMAGE_TYPE_3D;
		const uint32_t srcX = static_cast<uint32_t>(region.srcOffset.x) / srcBlock.width;
		const uint32_t srcY = static_cast<uint32_t>(region.srcOffset.y) / srcBlock.height;
		const uint32_t dstX = static_cast<uint32_t>(region.dstOffset.x) / dstBlock.width;
		const uint32_t dstY = static_cast<uint32_t>(region.dstOffset.y) / dstBlock.height;
		const VkDeviceSize rowBytes = VkDeviceSize{width} * texelSize;

		for (uint32_t s = 0; s < sliceCount; ++s)
		{
			const SubresourceFootprint srcFp = GetSubresourceFootprint(src, region.srcSubresource.mipLevel,
																	   region.srcSubresource.baseArrayLayer + (src3d ? 0 : s));
			const SubresourceFootprint dstFp = GetSubresourceFootprint(dst, region.dstSubresource.mipLevel,
																	   region.dstSubresource.baseArrayLayer + (dst3d ? 0 : s));
			const uint32_t srcZ = src3d ? static_cast<uint32_t>(region.srcOffset.z) / srcBlock.depth + s : 0;
			const uint32_t dstZ = dst3d ? static_cast<uint32_t>(region.dstOffset.z) / dstBlock.depth + s : 0;
			for (uint32_t y = 0; y < height; ++y)
			{
				const VkDeviceSize srcOffset = ImageTexelOffset(srcFp, srcX, srcY + y, srcZ, texelSize);
				const VkDeviceSize dstOffset = ImageTexelOffset(dstFp, dstX, dstY + y, dstZ, texelSize);
				row(srcOffset, dstOffset, rowBytes);
			}
		}
		return true;
	}

	// vkCmdClearColorImage: each (layer, mip) subresource is one contiguous byte range.
	template<typename RangeFn>
	void ForEachSubresourceRange(const Image& image, const VkImageSubresourceRange& range, RangeFn&& fn)
	{
		const uint32_t levelCount = ResolveLevelCount(image, range.baseMipLevel, range.levelCount);
		const uint32_t layerCount = ResolveLayerCount(image, range.baseArrayLayer, range.layerCount);
		for (uint32_t layer = 0; layer < layerCount; ++layer)
		{
			for (uint32_t level = 0; level < levelCount; ++level)
			{
				const SubresourceFootprint fp = GetSubresourceFootprint(image, range.baseMipLevel + level, range.baseArrayLayer + layer);
				fn(fp, fp.depthPitch * fp.extent.depth);
			}
		}
	}
} // namespace vkd
