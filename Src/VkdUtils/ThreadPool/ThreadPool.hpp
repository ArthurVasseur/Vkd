//
// Created by arthur on 28/10/2025.
//

#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <vector>

namespace vkd
{

	class ThreadPool
	{
	public:
		explicit ThreadPool(unsigned int numThreads = 0);
		~ThreadPool() noexcept;

		ThreadPool(const ThreadPool&) = delete;
		ThreadPool& operator=(const ThreadPool&) = delete;
		ThreadPool(ThreadPool&&) = delete;
		ThreadPool& operator=(ThreadPool&&) = delete;

		/**
		 * @brief Adds a task to the pool without returning a result.
		 *
		 * @tparam F Callable type that can be invoked with no arguments.
		 * @param f Callable to execute.
		 *
		 * @note This method is thread-safe. Tasks are not accepted after RequestStop().
		 */
		template<typename F>
			requires std::invocable<std::decay_t<F>> && std::is_void_v<std::invoke_result_t<std::decay_t<F>>>
		void AddTask(F&& f);

		/**
		 * @brief Submits a task and returns a future for its result.
		 *
		 * @tparam F Callable type.
		 * @return std::future with the result of the callable.
		 *
		 * @note This method is thread-safe. Tasks are not accepted after RequestStop().
		 */
		template<typename F>
			requires std::invocable<std::decay_t<F>>
		auto Submit(F&& f) -> std::future<std::invoke_result_t<std::decay_t<F>>>;

		/**
		 * @brief Waits until all in-flight tasks complete or deadline is reached.
		 *
		 * @param deadline Absolute time point to wait until.
		 * @return true if all tasks completed before deadline, false otherwise.
		 */
		bool Wait(std::chrono::steady_clock::time_point deadline);

		/**
		 * @brief Waits until all in-flight tasks complete or timeout expires.
		 *
		 * @param timeout Duration to wait.
		 * @return true if all tasks completed before timeout, false otherwise.
		 */
		bool WaitFor(std::chrono::milliseconds timeout);

		/**
		 * @brief Requests graceful shutdown. No new tasks will be accepted.
		 *
		 * Workers will finish current tasks and then exit.
		 * This method is idempotent and thread-safe.
		 */
		void RequestStop() noexcept;

		size_t GetWorkerCount() const noexcept;

	private:
		void WorkerLoop(std::stop_token stopToken, unsigned int workerIndex);

		void TaskCompleted() noexcept;

		// Thread management
		std::vector<std::jthread> m_workers;

		// Task queue and synchronization
		std::deque<std::function<void()>> m_taskQueue;
		std::mutex m_queueMutex;
		std::condition_variable_any m_queueCv;

		// Wait synchronization
		std::atomic<size_t> m_tasksInFlight{0};
		std::condition_variable m_waitCv;
		std::mutex m_waitMutex;

		// State
		std::atomic<bool> m_stopRequested{false};
	};

} // namespace vkd

#include "ThreadPool.inl"
