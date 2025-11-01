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
					std::cerr << "[ThreadPool] Task threw exception: " << e.what() << '\n';
				}
				catch (...)
				{
					std::cerr << "[ThreadPool] Task threw unknown exception" << '\n';
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
			return;

		for (auto& worker : m_workers)
		{
			worker.request_stop();
		}

		{
			std::lock_guard lock(m_queueMutex);
			m_queueCv.notify_all();
		}
	}

	size_t ThreadPool::GetWorkerCount() const noexcept
	{
		return m_workers.size();
	}

} // namespace vkd

/*
 * TEST SCENARIOS (commented out):
 *
 * 1. Basic functionality:
 *    - Create ThreadPool with default threads
 *    - Add 1000 light tasks (e.g., incrementing a shared atomic counter)
 *    - WaitFor with large timeout (e.g., 5s)
 *    - Verify all tasks completed (counter == 1000)
 *
 * 2. Submit with return values:
 *    - Submit tasks returning int, double, std::string
 *    - Verify futures complete with correct values
 *
 * 3. Submit with exceptions:
 *    - Submit tasks that throw exceptions
 *    - Verify futures throw when .get() is called
 *    - Verify pool continues to function after exceptions
 *
 * 4. Timeout behavior:
 *    - Add tasks with intentional delays (e.g., sleep)
 *    - WaitFor with short timeout
 *    - Verify returns false (timeout)
 *    - WaitFor again with longer timeout
 *    - Verify returns true (completed)
 *
 * 5. RequestStop during operation:
 *    - Start adding tasks in one thread
 *    - Call RequestStop from another thread
 *    - Verify no crashes, no deadlocks
 *    - Verify queued tasks complete before shutdown
 *
 * 6. Destruction with empty queue:
 *    - Create pool, add and complete tasks
 *    - Destroy pool
 *    - Verify clean shutdown, no leaks
 *
 * 7. Destruction with non-empty queue:
 *    - Create pool, add many tasks with delays
 *    - Destroy pool immediately
 *    - Verify graceful shutdown (may process some tasks)
 *    - Verify no crashes, no hangs
 *
 * 8. Concurrent AddTask/Submit:
 *    - Multiple threads adding tasks simultaneously
 *    - Verify thread-safety, no data races
 *
 * 9. Multiple Wait calls:
 *    - Multiple threads calling Wait/WaitFor simultaneously
 *    - Verify all return correct results
 *
 * 10. Edge cases:
 *     - Pool with 1 thread
 *     - Pool with 100+ threads
 *     - Tasks that add more tasks to the pool
 *     - Immediate RequestStop after construction
 */

 // Demo main (compile with -DTHREADPOOL_DEMO)
#ifdef THREADPOOL_DEMO

#include <iostream>
#include <atomic>

int main()
{
	using namespace vkd;

	std::cout << "ThreadPool Demo\n";
	std::cout << "===============\n\n";

	// Create pool
	ThreadPool pool;
	std::cout << "Created pool with " << pool.GetWorkerCount() << " workers\n";

	// Test 1: Simple tasks
	{
		std::atomic<int> counter{ 0 };
		constexpr int num_tasks = 100;

		for (int i = 0; i < num_tasks; ++i)
		{
			pool.AddTask([&counter]()
				{
					counter.fetch_add(1, std::memory_order_relaxed);
				});
		}

		bool completed = pool.WaitFor(std::chrono::milliseconds{ 1000 });
		std::cout << "Test 1 - Simple tasks: "
			<< (completed && counter.load() == num_tasks ? "PASS" : "FAIL")
			<< " (counter=" << counter.load() << ")\n";
	}

	// Test 2: Submit with return value
	{
		auto future = pool.Submit([]()
			{
				return 42;
			});

		bool completed = pool.WaitFor(std::chrono::milliseconds{ 1000 });
		int result = future.get();

		std::cout << "Test 2 - Submit with return: "
			<< (completed && result == 42 ? "PASS" : "FAIL")
			<< " (result=" << result << ")\n";
	}

	// Test 3: Submit with exception
	{
		auto future = pool.Submit([]() -> int
			{
				throw std::runtime_error("test exception");
			});

		pool.WaitFor(std::chrono::milliseconds{ 1000 });

		bool caught = false;
		try
		{
			future.get();
		}
		catch (const std::runtime_error& e)
		{
			caught = true;
		}

		std::cout << "Test 3 - Exception handling: "
			<< (caught ? "PASS" : "FAIL") << "\n";
	}

	std::cout << "\nAll tests completed. Pool will shut down gracefully.\n";

	return 0;
}

#endif // THREADPOOL_DEMO
