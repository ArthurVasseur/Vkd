/**
 * @file ImageView.hpp
 * @brief Vulkan image view abstraction
 * @date 2025-11-18
 *
 * Represents an image view object describing how to access an image.
 */

#pragma once

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;

	class ImageView : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_IMAGE_VIEW;
		VKD_NON_DISPATCHABLE_HANDLE(ImageView);

		ImageView();
		~ImageView() override = default;

		virtual VkResult Create(Device& owner, const VkImageViewCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline VkImage GetImage() const;
		[[nodiscard]] inline VkImageViewType GetViewType() const;
		[[nodiscard]] inline VkFormat GetFormat() const;
		[[nodiscard]] inline const VkComponentMapping& GetComponents() const;
		[[nodiscard]] inline const VkImageSubresourceRange& GetSubresourceRange() const;

	protected:
		Device* m_owner;
		VkImage m_image;
		VkImageViewType m_viewType;
		VkFormat m_format;
		VkComponentMapping m_components;
		VkImageSubresourceRange m_subresourceRange;
	};
} // namespace vkd

#include "ImageView.inl"
