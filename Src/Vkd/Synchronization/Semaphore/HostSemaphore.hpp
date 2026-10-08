/**
 * @file HostSemaphore.hpp
 * @brief Binary semaphore signaled and waited on the CPU
 * @date 2026-10-07
 *
 * For queues that wait on and signal semaphores from the CPU around their work: the software
 * rasterizer's worker thread, the AMD queue (which waits for each submission to complete).
 */

#pragma once

#include <condition_variable>
#include <mutex>

#include "Vkd/Synchronization/Semaphore/Semaphore.hpp"

namespace vkd
{
	class HostSemaphore : public Semaphore
	{
	public:
		explicit HostSemaphore(const VkAllocationCallbacks& allocationCallbacks);
		~HostSemaphore() override = default;

		void Wait() override;
		void Signal() override;

	private:
		std::mutex m_mutex;
		std::condition_variable m_cv;
		bool m_signaled;
	};
} // namespace vkd
