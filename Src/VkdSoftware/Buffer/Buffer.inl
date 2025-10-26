#pragma once

#include "VkdSoftware/Buffer/Buffer.hpp"

namespace vkd::software
{
	inline Buffer::Buffer() :
		m_memory(VK_NULL_HANDLE),
		m_memoryOffset(0),
		m_bound(false)
	{
	}
}
