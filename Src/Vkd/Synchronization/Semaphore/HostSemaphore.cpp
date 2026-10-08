/**
 * @file HostSemaphore.cpp
 * @brief Implementation of the CPU binary semaphore
 * @date 2026-10-07
 */

#include "Vkd/Synchronization/Semaphore/HostSemaphore.hpp"

namespace vkd
{
	HostSemaphore::HostSemaphore(const VkAllocationCallbacks& allocationCallbacks) :
		Semaphore(allocationCallbacks),
		m_signaled(false)
	{
	}

	void HostSemaphore::Wait()
	{
		VKD_AUTO_PROFILER_SCOPE();

		std::unique_lock lock(m_mutex);
		m_cv.wait(lock, [this]
				  { return m_signaled; });
		m_signaled = false;
	}

	void HostSemaphore::Signal()
	{
		VKD_AUTO_PROFILER_SCOPE();

		{
			std::lock_guard lock(m_mutex);
			m_signaled = true;
		}
		m_cv.notify_all();
	}
} // namespace vkd
