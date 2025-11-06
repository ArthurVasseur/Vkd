/**
 * @file Image.inl
 * @brief Inline implementations for Image
 * @date 2025-11-05
 */

#pragma once

#include "Vkd/Image/Image.hpp"
#include "Vkd/Device/Device.hpp"

namespace vkd
{
	inline Image::Image() :
		ObjectBase(ObjectType),
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
		m_memoryOffset(0)
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

		SetAllocationCallbacks(allocationCallbacks);

		return VK_SUCCESS;
	}

	inline void Image::BindImageMemory(DeviceMemory& deviceMemory, VkDeviceSize memoryOffset)
	{
		m_memory = &deviceMemory;
		m_memoryOffset = memoryOffset;
	}

	inline void Image::GetMemoryRequirements(VkMemoryRequirements& memoryRequirements) const
	{
		VkDeviceSize pixelSize = vkuFormatElementSize(m_format);
		VkDeviceSize imageSize = static_cast<VkDeviceSize>(m_extent.width) * m_extent.height * m_extent.depth * pixelSize;

		memoryRequirements.size = imageSize;
		memoryRequirements.alignment = 256;
		memoryRequirements.memoryTypeBits = 0xFFFFFFFF;
	}

	inline Device* Image::GetOwner() const
	{
		return m_owner;
	}

	inline VkImageType Image::GetImageType() const
	{
		return m_imageType;
	}

	inline VkFormat Image::GetFormat() const
	{
		return m_format;
	}

	inline VkExtent3D Image::GetExtent() const
	{
		return m_extent;
	}

	inline UInt32 Image::GetMipLevels() const
	{
		return m_mipLevels;
	}

	inline UInt32 Image::GetArrayLayers() const
	{
		return m_arrayLayers;
	}

	inline VkSampleCountFlagBits Image::GetSamples() const
	{
		return m_samples;
	}

	inline VkImageTiling Image::GetTiling() const
	{
		return m_tiling;
	}

	inline VkImageUsageFlags Image::GetUsage() const
	{
		return m_usage;
	}

	inline DeviceMemory* Image::GetMemory() const
	{
		return m_memory;
	}

	inline VkDeviceSize Image::GetMemoryOffset() const
	{
		return m_memoryOffset;
	}

	inline bool Image::IsBound() const
	{
		return m_memory != nullptr;
	}
}
