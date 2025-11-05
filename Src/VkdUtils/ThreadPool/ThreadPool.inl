//
// Created by arthur on 28/10/2025.
//

namespace vkd
{

	template<typename F>
		requires std::invocable<std::decay_t<F>>&& std::is_void_v<std::invoke_result_t<std::decay_t<F>>>
	void ThreadPool::AddTask(F&& f)
	{
		if (m_stopRequested.load(std::memory_order_acquire))
			return;

		m_tasksInFlight.fetch_add(1, std::memory_order_acq_rel);

		auto wrapped = [func = std::forward<F>(f), this]() mutable
			{
				try
				{
					func();
				}
				catch (const std::exception& e)
				{
					std::cerr << "[ThreadPool] Task threw exception: " << e.what() << '\n';
				}
				catch (...)
				{
					std::cerr << "[ThreadPool] Task threw unknown exception" << '\n';
				}
			};

		{
			std::lock_guard lock(m_queueMutex);

			if (m_stopRequested.load(std::memory_order_acquire))
			{
				m_tasksInFlight.fetch_sub(1, std::memory_order_acq_rel);
				return;
			}

			m_taskQueue.emplace_back(std::move(wrapped));
		}

		m_queueCv.notify_one();
	}

	template<typename F>
		requires std::invocable<std::decay_t<F>>
	auto ThreadPool::Submit(F&& f) -> std::future<std::invoke_result_t<std::decay_t<F>>>
	{
		using ReturnType = std::invoke_result_t<std::decay_t<F>>;

		auto promise = std::make_shared<std::promise<ReturnType>>();
		auto func = std::make_shared<std::decay_t<F>>(std::forward<F>(f));
		std::future<ReturnType> result = promise->get_future();

		if (m_stopRequested.load(std::memory_order_acquire))
		{
			promise->set_exception(std::make_exception_ptr(std::runtime_error("ThreadPool is shutting down")));
			return result;
		}

		m_tasksInFlight.fetch_add(1, std::memory_order_acq_rel);

		auto wrapped_task = [promise, func]()
			{
				try
				{
					if constexpr (std::is_void_v<ReturnType>)
					{
						(*func)();
						promise->set_value();
					}
					else
					{
						promise->set_value((*func)());
					}
				}
				catch (...)
				{
					try
					{
						promise->set_exception(std::current_exception());
					}
					catch (...) {}
				}
			};

		{
			std::lock_guard lock(m_queueMutex);

			if (m_stopRequested.load(std::memory_order_acquire))
			{
				m_tasksInFlight.fetch_sub(1, std::memory_order_acq_rel);

				try
				{
					promise->set_exception(std::make_exception_ptr(std::runtime_error("ThreadPool is shutting down")));
				}
				catch (...) {}

				return result;
			}

			m_taskQueue.emplace_back(std::move(wrapped_task));
		}

		m_queueCv.notify_one();

		return result;
	}

} // namespace vkd
