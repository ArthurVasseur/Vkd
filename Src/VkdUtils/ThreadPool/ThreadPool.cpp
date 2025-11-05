//
// Created by arthur on 28/10/2025.
//

#include "VkdUtils/ThreadPool/ThreadPool.hpp"
#include "VkdUtils/System/System.hpp"

#include <algorithm>
#include <iostream>

namespace vkd
{
	ThreadPool::ThreadPool(unsigned int numThreads)
	{
		if (numThreads == 0)
		{
			numThreads = std::thread::hardware_concurrency();
			if (numThreads == 0)
			{
				numThreads = 1;
			}
		}

		m_workers.reserve(numThreads);

		for (unsigned int i = 0; i < numThreads; ++i)
		{
			m_workers.emplace_back([this, i](std::stop_token st)
				{
					WorkerLoop(st, i + 1);
				});
		}
	}

	ThreadPool::~ThreadPool() noexcept
	{
		RequestStop();
	}

	void ThreadPool::WorkerLoop(std::stop_token stopToken, unsigned int workerIndex)
	{
		System::SetThreadName("ThreadPool Worker#" + std::to_string(workerIndex));

		while (true)
		{
			std::function<void()> task;
			{
				std::unique_lock lock(m_queueMutex);

				m_queueCv.wait(lock, stopToken, [this]()
					{
						return !m_taskQueue.empty() || m_stopRequested.load(std::memory_order_acquire);
					});

				if (stopToken.stop_requested() && m_taskQueue.empty())
					break;

				if (m_taskQueue.empty())
				{
					if (m_stopRequested.load(std::memory_order_acquire))
						break;
					continue;
				}

				task = std::move(m_taskQueue.front());
				m_taskQueue.pop_front();
			}

			if (task)
			{
				try
				{
					task();
				}
				catch (const std::exception& e)
				{
					std::cerr << "[ThreadPool::Worker#" << workerIndex << "] Exception caught: " << e.what() << '\n';
				}
				catch (...)
				{
					std::cerr << "[ThreadPool::Worker#" << workerIndex << "] Unknown exception caught\n";
				}
				TaskCompleted();
			}
		}
	}

	void ThreadPool::TaskCompleted() noexcept
	{
		size_t prev = m_tasksInFlight.fetch_sub(1, std::memory_order_acq_rel);

		if (prev == 1)
		{
			std::lock_guard lock(m_waitMutex);
			m_waitCv.notify_all();
		}
	}

	bool ThreadPool::Wait(std::chrono::steady_clock::time_point deadline)
	{
		std::unique_lock lock(m_waitMutex);

		return m_waitCv.wait_until(lock, deadline, [this]()
			{
				return m_tasksInFlight.load(std::memory_order_acquire) == 0;
			});
	}

	bool ThreadPool::WaitFor(std::chrono::milliseconds timeout)
	{
		auto deadline = std::chrono::steady_clock::now() + timeout;
		return Wait(deadline);
	}

	void ThreadPool::RequestStop() noexcept
	{
		bool expected = false;
		if (!m_stopRequested.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
		{
			// Stop already requested, wait for workers to finish
			for (auto& worker : m_workers)
			{
				if (worker.joinable())
					worker.join();
			}
			return;
		}

		for (auto& worker : m_workers)
		{
			worker.request_stop();
		}

		{
			std::lock_guard lock(m_queueMutex);
			m_queueCv.notify_all();
		}

		// Wait for all workers to finish
		for (auto& worker : m_workers)
		{
			if (worker.joinable())
				worker.join();
		}
	}

	size_t ThreadPool::GetWorkerCount() const noexcept
	{
		return m_workers.size();
	}

} // namespace vkd
