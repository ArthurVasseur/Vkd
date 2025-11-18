/**
 * @file ImageView.inl
 * @brief Inline implementations for ImageView
 * @date 2025-11-18
 */

#pragma once

#include "Vkd/Device/Device.hpp"
#include "Vkd/ImageView/ImageView.hpp"

namespace vkd
{
	inline ImageView::ImageView() :
		ObjectBase(ObjectType),
		m_owner(nullptr),
		m_image(VK_NULL_HANDLE),
		m_viewType(VK_IMAGE_VIEW_TYPE_2D),
		m_format(VK_FORMAT_UNDEFINED),
		m_components{},
		m_subresourceRange{}
	{
	}

	inline VkResult ImageView::Create(Device& owner, const VkImageViewCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;
		m_image = info.image;
		m_viewType = info.viewType;
		m_format = info.format;
		m_components = info.components;
		m_subresourceRange = info.subresourceRange;

		SetAllocationCallbacks(allocationCallbacks);

		return VK_SUCCESS;
	}

	inline Device* ImageView::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline VkImage ImageView::GetImage() const
	{
		AssertValid();
		return m_image;
	}

	inline VkImageViewType ImageView::GetViewType() const
	{
		AssertValid();
		return m_viewType;
	}

	inline VkFormat ImageView::GetFormat() const
	{
		AssertValid();
		return m_format;
	}

	inline const VkComponentMapping& ImageView::GetComponents() const
	{
		AssertValid();
		return m_components;
	}

	inline const VkImageSubresourceRange& ImageView::GetSubresourceRange() const
	{
		AssertValid();
		return m_subresourceRange;
	}
} // namespace vkd
