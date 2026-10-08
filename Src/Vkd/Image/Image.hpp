/**
 * @file Image.hpp
 * @brief Vulkan image object abstraction
 * @date 2025-11-05
 *
 * Represents a Vulkan image resource and handles memory binding.
 */

#pragma once

#include <vector>

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/utility/vk_format_utils.h>
#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;
	class DeviceMemory;
	class Buffer;

	class Image : public ObjectBase
	{
	public:
		struct OpCopy
		{
			Image* src;
			Image* dst;
			vkd::vector<VkImageCopy> regions;
		};

		struct OpClearColorImage
		{
			Image* image;
			VkImageLayout layout;
			VkClearColorValue clearColor;
			vkd::vector<VkImageSubresourceRange> ranges;
		};

		struct OpClearDepthStencilImage
		{
			Image* image;
			VkImageLayout layout;
			VkClearDepthStencilValue value;
			vkd::vector<VkImageSubresourceRange> ranges;
		};

		struct OpResolve
		{
			Image* src;
			Image* dst;
			vkd::vector<VkImageResolve> regions;
		};

		// vkCmdBlitImage: scaled copies, regions as given (offsets may be reversed: mirroring).
		struct OpBlit
		{
			Image* src;
			Image* dst;
			vkd::vector<VkImageBlit> regions;
			VkFilter filter;
		};

		using Op = Nz::TypeList<OpCopy, OpClearColorImage, OpClearDepthStencilImage, OpResolve, OpBlit>;

		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_IMAGE;
		VKD_NON_DISPATCHABLE_HANDLE(Image);

		explicit Image(const VkAllocationCallbacks& allocationCallbacks);
		~Image() override = default;

		virtual VkResult Create(Device& owner, const VkImageCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks);
		void BindImageMemory(DeviceMemory& deviceMemory, VkDeviceSize memoryOffset);
		virtual void GetMemoryRequirements(VkMemoryRequirements& memoryRequirements) const;
		void GetSubresourceLayout(const VkImageSubresource& subresource, VkSubresourceLayout& layout) const;

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline VkImageType GetImageType() const;
		[[nodiscard]] inline VkFormat GetFormat() const;
		[[nodiscard]] inline VkExtent3D GetExtent() const;
		[[nodiscard]] inline UInt32 GetMipLevels() const;
		[[nodiscard]] inline UInt32 GetArrayLayers() const;
		[[nodiscard]] inline VkSampleCountFlagBits GetSamples() const;
		[[nodiscard]] inline VkImageTiling GetTiling() const;
		[[nodiscard]] inline VkImageUsageFlags GetUsage() const;
		[[nodiscard]] inline DeviceMemory* GetMemory() const;
		[[nodiscard]] inline VkDeviceSize GetMemoryOffset() const;
		[[nodiscard]] inline bool IsBound() const;

	protected:
		// Row pitch of a mip level: its rows padded to the device's row alignment.
		[[nodiscard]] VkDeviceSize LevelRowPitch(UInt32 level) const;
		[[nodiscard]] VkDeviceSize LevelRows(UInt32 level) const;
		[[nodiscard]] VkDeviceSize LevelSlices(UInt32 level) const;

		Device* m_owner;
		VkImageType m_imageType;
		VkFormat m_format;
		VkExtent3D m_extent;
		UInt32 m_mipLevels;
		UInt32 m_arrayLayers;
		VkSampleCountFlagBits m_samples;
		VkImageTiling m_tiling;
		VkImageUsageFlags m_usage;
		DeviceMemory* m_memory;
		VkDeviceSize m_memoryOffset;
		VkDeviceSize m_rowPitchAlignment;
	};
} // namespace vkd

#include "Image.inl"
