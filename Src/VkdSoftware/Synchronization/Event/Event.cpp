/**
 * @file Event.cpp
 * @brief Implementation of software renderer event
 * @date 2026-08-30
 */

#include "VkdSoftware/Synchronization/Event/Event.hpp"

namespace vkd::software
{
	Event::Event(const VkAllocationCallbacks& allocationCallbacks) :
		vkd::Event(allocationCallbacks),
		m_signaled(false)
	{
	}

	VkResult Event::GetStatus()
	{
		VKD_AUTO_PROFILER_SCOPE();

		std::lock_guard lock(m_mutex);
		return m_signaled ? VK_EVENT_SET : VK_EVENT_RESET;
	}

	VkResult Event::Set()
	{
		VKD_AUTO_PROFILER_SCOPE();

		std::lock_guard lock(m_mutex);
		m_signaled = true;
		return VK_SUCCESS;
	}

	VkResult Event::Reset()
	{
		VKD_AUTO_PROFILER_SCOPE();

		std::lock_guard lock(m_mutex);
		m_signaled = false;
		return VK_SUCCESS;
	}
} // namespace vkd::software
