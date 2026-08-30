/**
 * @file Semaphore.cpp
 * @brief Implementation of software renderer semaphore
 * @date 2026-08-30
 */

#include "VkdSoftware/Synchronization/Semaphore/Semaphore.hpp"

namespace vkd::software
{
	Semaphore::Semaphore(const VkAllocationCallbacks& allocationCallbacks) :
		vkd::Semaphore(allocationCallbacks),
		m_signaled(false)
	{
	}

	void Semaphore::Wait()
	{
		VKD_AUTO_PROFILER_SCOPE();

		std::unique_lock lock(m_mutex);
		m_cv.wait(lock, [this]
				  { return m_signaled; });
		m_signaled = false;
	}

	void Semaphore::Signal()
	{
		VKD_AUTO_PROFILER_SCOPE();

		{
			std::lock_guard lock(m_mutex);
			m_signaled = true;
		}
		m_cv.notify_all();
	}
} // namespace vkd::software
