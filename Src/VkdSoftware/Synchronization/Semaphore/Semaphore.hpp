/**
 * @file Semaphore.hpp
 * @brief Software renderer binary semaphore
 * @date 2026-08-30
 */

#pragma once

#include <condition_variable>
#include <mutex>

#include "Vkd/Synchronization/Semaphore/Semaphore.hpp"

namespace vkd::software
{
	class Semaphore : public vkd::Semaphore
	{
	public:
		explicit Semaphore(const VkAllocationCallbacks& allocationCallbacks);
		~Semaphore() override = default;

		void Wait() override;
		void Signal() override;

	private:
		std::mutex m_mutex;
		std::condition_variable m_cv;
		bool m_signaled;
	};
} // namespace vkd::software
