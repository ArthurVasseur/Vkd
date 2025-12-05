/**
 * @file BufferView.cpp
 * @brief Implementation of Vulkan buffer view
 * @date 2025-12-05
 */

#include "Vkd/BufferView/BufferView.hpp"

#include "Vkd/Device/Device.hpp"

namespace vkd
{
	BufferView::BufferView() :
		ObjectBase(ObjectType),
		m_owner(nullptr),
		m_buffer(VK_NULL_HANDLE),
		m_format(VK_FORMAT_UNDEFINED),
		m_offset(0),
		m_range(0)
	{
	}

	VkResult BufferView::Create(Device& owner, const VkBufferViewCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;
		m_buffer = info.buffer;
		m_format = info.format;
		m_offset = info.offset;
		m_range = info.range;

		SetAllocationCallbacks(allocationCallbacks);

#ifdef VKD_DEBUG_CHECKS
		m_createResult = VK_SUCCESS;
#endif

		return VK_SUCCESS;
	}
} // namespace vkd
