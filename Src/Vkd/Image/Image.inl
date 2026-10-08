/**
 * @file Image.inl
 * @brief Inline implementations for Image
 * @date 2025-11-05
 */

#pragma once

#include <algorithm>

#include "Vkd/Device/Device.hpp"
#include "Vkd/Image/Image.hpp"

namespace vkd
{
	inline Image::Image(const VkAllocationCallbacks& allocationCallbacks) :
		ObjectBase(ObjectType, allocationCallbacks),
		m_owner(nullptr),
		m_imageType(VK_IMAGE_TYPE_2D),
		m_format(VK_FORMAT_UNDEFINED),
		m_extent{0, 0, 0},
		m_mipLevels(1),
		m_arrayLayers(1),
		m_samples(VK_SAMPLE_COUNT_1_BIT),
		m_tiling(VK_IMAGE_TILING_OPTIMAL),
		m_usage(0),
		m_memory(nullptr),
		m_memoryOffset(0),
		m_rowPitchAlignment(1)
	{
	}

	inline VkResult Image::Create(Device& owner, const VkImageCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;
		m_imageType = info.imageType;
		m_format = info.format;
		m_extent = info.extent;
		m_mipLevels = info.mipLevels;
		m_arrayLayers = info.arrayLayers;
		m_samples = info.samples;
		m_tiling = info.tiling;
		m_usage = info.usage;
		m_rowPitchAlignment = std::max<VkDeviceSize>(owner.GetImageRowPitchAlignment(), 1);

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline void Image::BindImageMemory(DeviceMemory& deviceMemory, VkDeviceSize memoryOffset)
	{
		m_memory = &deviceMemory;
		m_memoryOffset = memoryOffset;
	}

	// Rows, columns and slices count texel blocks: one texel for an uncompressed format, e.g. 4x4
	// texels for BC (the spec's rowPitch of a compressed linear image is between rows of blocks).
	inline VkDeviceSize Image::LevelRowPitch(UInt32 level) const
	{
		const VkExtent3D block = vkuFormatTexelBlockExtent(m_format);
		const VkDeviceSize blocksWide = (std::max<VkDeviceSize>(m_extent.width >> level, 1) + block.width - 1) / block.width;
		const VkDeviceSize rowBytes = blocksWide * vkuFormatElementSize(m_format);
		return (rowBytes + m_rowPitchAlignment - 1) / m_rowPitchAlignment * m_rowPitchAlignment;
	}

	inline VkDeviceSize Image::LevelRows(UInt32 level) const
	{
		const VkExtent3D block = vkuFormatTexelBlockExtent(m_format);
		return (std::max<VkDeviceSize>(m_extent.height >> level, 1) + block.height - 1) / block.height;
	}

	inline VkDeviceSize Image::LevelSlices(UInt32 level) const
	{
		const VkExtent3D block = vkuFormatTexelBlockExtent(m_format);
		return (std::max<VkDeviceSize>(m_extent.depth >> level, 1) + block.depth - 1) / block.depth;
	}

	inline void Image::GetMemoryRequirements(VkMemoryRequirements& memoryRequirements) const
	{
		VkDeviceSize mipChainSize = 0;
		for (UInt32 level = 0; level < m_mipLevels; ++level)
			mipChainSize += LevelRowPitch(level) * LevelRows(level) * LevelSlices(level);

		memoryRequirements.size = mipChainSize * m_arrayLayers * static_cast<VkDeviceSize>(m_samples);
		memoryRequirements.alignment = 256;
		memoryRequirements.memoryTypeBits = 0x1;
	}

	inline void Image::GetSubresourceLayout(const VkImageSubresource& subresource, VkSubresourceLayout& layout) const
	{
		VkDeviceSize mipChainSize = 0;
		VkDeviceSize levelOffset = 0;
		VkDeviceSize levelSize = 0;
		VkDeviceSize levelRowPitch = 0;
		VkDeviceSize levelDepthPitch = 0;

		for (UInt32 level = 0; level < m_mipLevels; ++level)
		{
			const VkDeviceSize mipHeight = LevelRows(level);
			const VkDeviceSize mipDepth = LevelSlices(level);
			const VkDeviceSize rowPitch = LevelRowPitch(level);
			const VkDeviceSize thisLevelSize = rowPitch * mipHeight * mipDepth;

			if (level == subresource.mipLevel)
			{
				levelOffset = mipChainSize;
				levelSize = thisLevelSize;
				levelRowPitch = rowPitch;
				levelDepthPitch = rowPitch * mipHeight;
			}

			mipChainSize += thisLevelSize;
		}

		layout.offset = (subresource.arrayLayer * mipChainSize) + levelOffset;
		layout.size = levelSize;
		layout.rowPitch = levelRowPitch;
		layout.arrayPitch = mipChainSize;
		layout.depthPitch = levelDepthPitch;
	}

	inline Device* Image::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline VkImageType Image::GetImageType() const
	{
		AssertValid();
		return m_imageType;
	}

	inline VkFormat Image::GetFormat() const
	{
		AssertValid();
		return m_format;
	}

	inline VkExtent3D Image::GetExtent() const
	{
		AssertValid();
		return m_extent;
	}

	inline UInt32 Image::GetMipLevels() const
	{
		AssertValid();
		return m_mipLevels;
	}

	inline UInt32 Image::GetArrayLayers() const
	{
		AssertValid();
		return m_arrayLayers;
	}

	inline VkSampleCountFlagBits Image::GetSamples() const
	{
		AssertValid();
		return m_samples;
	}

	inline VkImageTiling Image::GetTiling() const
	{
		AssertValid();
		return m_tiling;
	}

	inline VkImageUsageFlags Image::GetUsage() const
	{
		AssertValid();
		return m_usage;
	}

	inline DeviceMemory* Image::GetMemory() const
	{
		AssertValid();
		return m_memory;
	}

	inline VkDeviceSize Image::GetMemoryOffset() const
	{
		AssertValid();
		return m_memoryOffset;
	}

	inline bool Image::IsBound() const
	{
		AssertValid();
		return m_memory != nullptr;
	}
} // namespace vkd
