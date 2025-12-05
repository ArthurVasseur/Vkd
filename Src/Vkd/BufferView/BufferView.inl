/**
 * @file BufferView.inl
 * @brief Inline implementations for BufferView
 * @date 2025-12-05
 */

#pragma once

namespace vkd
{
	Device* BufferView::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	VkBuffer BufferView::GetBuffer() const
	{
		AssertValid();
		return m_buffer;
	}

	VkFormat BufferView::GetFormat() const
	{
		AssertValid();
		return m_format;
	}

	VkDeviceSize BufferView::GetOffset() const
	{
		AssertValid();
		return m_offset;
	}

	VkDeviceSize BufferView::GetRange() const
	{
		AssertValid();
		return m_range;
	}
} // namespace vkd
