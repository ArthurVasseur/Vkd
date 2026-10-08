/**
 * @file HostEvent.cpp
 * @brief Implementation of the CPU event
 * @date 2026-10-07
 */

#include "Vkd/Synchronization/Event/HostEvent.hpp"

namespace vkd
{
	HostEvent::HostEvent(const VkAllocationCallbacks& allocationCallbacks) :
		Event(allocationCallbacks),
		m_signaled(false)
	{
	}

	VkResult HostEvent::GetStatus()
	{
		VKD_AUTO_PROFILER_SCOPE();

		std::lock_guard lock(m_mutex);
		return m_signaled ? VK_EVENT_SET : VK_EVENT_RESET;
	}

	VkResult HostEvent::Set()
	{
		VKD_AUTO_PROFILER_SCOPE();

		std::lock_guard lock(m_mutex);
		m_signaled = true;
		return VK_SUCCESS;
	}

	VkResult HostEvent::Reset()
	{
		VKD_AUTO_PROFILER_SCOPE();

		std::lock_guard lock(m_mutex);
		m_signaled = false;
		return VK_SUCCESS;
	}
} // namespace vkd
