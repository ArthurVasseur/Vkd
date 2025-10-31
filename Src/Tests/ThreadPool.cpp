/**
 * @file Test/ThreadPool.cpp
 * @brief Unit tests for ThreadPool
 * @date 2025-10-31
 */

#include <VkdUtils/ThreadPool/ThreadPool.hpp>
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <vector>
#include <string>
#include <chrono>
#include <thread>

using namespace vkd;
using namespace std::chrono_literals;

TEST_CASE("ThreadPool - Basic Initialization", "[threadpool][init]")
{
	SECTION("Default construction")
	{
		ThreadPool pool;
		REQUIRE(pool.GetWorkerCount() > 0);
	}

	SECTION("Explicit thread count")
	{
		ThreadPool pool(4);
		REQUIRE(pool.GetWorkerCount() == 4);
	}

	SECTION("Single thread pool")
	{
		ThreadPool pool(1);
		REQUIRE(pool.GetWorkerCount() == 1);
	}
}

TEST_CASE("ThreadPool - AddTask", "[threadpool][addtask]")
{
	ThreadPool pool(4);

	SECTION("Single task execution")
	{
		std::atomic<bool> executed{ false };

		pool.AddTask([&executed]()
			{
				executed.store(true, std::memory_order_relaxed);
			});

		REQUIRE(pool.WaitFor(1000ms));
		REQUIRE(executed.load());
	}

	SECTION("Multiple tasks execution")
	{
		std::atomic<int> counter{ 0 };
		constexpr int numTasks = 100;

		for (int i = 0; i < numTasks; ++i)
		{
			pool.AddTask([&counter]()
				{
					counter.fetch_add(1, std::memory_order_relaxed);
				});
		}

		REQUIRE(pool.WaitFor(5000ms));
		REQUIRE(counter.load() == numTasks);
	}

	SECTION("Task with shared state")
	{
		std::atomic<int> sum{ 0 };
		constexpr int numTasks = 50;

		for (int i = 1; i <= numTasks; ++i)
		{
			pool.AddTask([&sum, i]()
				{
					sum.fetch_add(i, std::memory_order_relaxed);
				});
		}

		REQUIRE(pool.WaitFor(5000ms));
		REQUIRE(sum.load() == (numTasks * (numTasks + 1)) / 2);
	}
}

TEST_CASE("ThreadPool - Submit", "[threadpool][submit]")
{
	ThreadPool pool(4);

	SECTION("Submit returning int")
	{
		auto future = pool.Submit([]()
			{
				return 42;
			});

		REQUIRE(pool.WaitFor(1000ms));
		REQUIRE(future.get() == 42);
	}

	SECTION("Submit returning string")
	{
		auto future = pool.Submit([]()
			{
				return std::string("Hello, ThreadPool!");
			});

		REQUIRE(pool.WaitFor(1000ms));
		REQUIRE(future.get() == "Hello, ThreadPool!");
	}

	SECTION("Submit with computation")
	{
		auto future = pool.Submit([]()
			{
				int sum = 0;
				for (int i = 1; i <= 100; ++i)
					sum += i;
				return sum;
			});

		REQUIRE(pool.WaitFor(1000ms));
		REQUIRE(future.get() == 5050);
	}

	SECTION("Multiple Submit calls")
	{
		std::vector<std::future<int>> futures;
		constexpr int numTasks = 20;

		for (int i = 0; i < numTasks; ++i)
		{
			futures.push_back(pool.Submit([i]()
				{
					return i * i;
				}));
		}

		REQUIRE(pool.WaitFor(5000ms));

		for (int i = 0; i < numTasks; ++i)
		{
			REQUIRE(futures[i].get() == i * i);
		}
	}
}

TEST_CASE("ThreadPool - Exception Handling", "[threadpool][exception]")
{
	ThreadPool pool(4);

	SECTION("Task throwing exception")
	{
		auto future = pool.Submit([]() -> int
			{
				throw std::runtime_error("Test exception");
			});

		REQUIRE(pool.WaitFor(1000ms));

		REQUIRE_THROWS_AS(future.get(), std::runtime_error);
	}

	SECTION("Multiple tasks with exceptions")
	{
		std::vector<std::future<int>> futures;

		for (int i = 0; i < 10; ++i)
		{
			futures.push_back(pool.Submit([i]() -> int
				{
					if (i % 2 == 0)
						throw std::runtime_error("Even number");
					return i;
				}));
		}

		REQUIRE(pool.WaitFor(5000ms));

		for (int i = 0; i < 10; ++i)
		{
			if (i % 2 == 0)
				REQUIRE_THROWS_AS(futures[i].get(), std::runtime_error);
			else
				REQUIRE(futures[i].get() == i);
		}
	}

	SECTION("Pool continues after exception")
	{
		auto future1 = pool.Submit([]() -> int
			{
				throw std::runtime_error("First exception");
			});

		pool.WaitFor(1000ms);
		REQUIRE_THROWS_AS(future1.get(), std::runtime_error);

		auto future2 = pool.Submit([]()
			{
				return 42;
			});

		REQUIRE(pool.WaitFor(1000ms));
		REQUIRE(future2.get() == 42);
	}
}

TEST_CASE("ThreadPool - Wait and WaitFor", "[threadpool][wait]")
{
	ThreadPool pool(4);

	SECTION("WaitFor with immediate completion")
	{
		std::atomic<int> counter{ 0 };

		for (int i = 0; i < 10; ++i)
		{
			pool.AddTask([&counter]()
				{
					counter.fetch_add(1, std::memory_order_relaxed);
				});
		}

		REQUIRE(pool.WaitFor(5000ms));
		REQUIRE(counter.load() == 10);
	}

	SECTION("WaitFor with timeout")
	{
		pool.AddTask([]()
			{
				std::this_thread::sleep_for(500ms);
			});

		REQUIRE_FALSE(pool.WaitFor(100ms));
		REQUIRE(pool.WaitFor(1000ms));
	}

	SECTION("Wait with deadline")
	{
		pool.AddTask([]()
			{
				std::this_thread::sleep_for(200ms);
			});

		auto deadline = std::chrono::steady_clock::now() + 100ms;
		REQUIRE_FALSE(pool.Wait(deadline));

		deadline = std::chrono::steady_clock::now() + 1000ms;
		REQUIRE(pool.Wait(deadline));
	}

	SECTION("Multiple Wait calls")
	{
		std::atomic<int> counter{ 0 };

		for (int i = 0; i < 50; ++i)
		{
			pool.AddTask([&counter]()
				{
					counter.fetch_add(1, std::memory_order_relaxed);
				});
		}

		REQUIRE(pool.WaitFor(5000ms));
		REQUIRE(counter.load() == 50);
		REQUIRE(pool.WaitFor(100ms));
	}
}

TEST_CASE("ThreadPool - RequestStop", "[threadpool][stop]")
{
	SECTION("RequestStop with empty queue")
	{
		ThreadPool pool(4);
		std::atomic<int> counter{ 0 };

		for (int i = 0; i < 10; ++i)
		{
			pool.AddTask([&counter]()
				{
					counter.fetch_add(1, std::memory_order_relaxed);
				});
		}

		pool.WaitFor(5000ms);
		pool.RequestStop();

		REQUIRE(counter.load() == 10);
	}

	SECTION("RequestStop is idempotent")
	{
		ThreadPool pool(4);

		pool.RequestStop();
		pool.RequestStop();
		pool.RequestStop();
	}

	SECTION("No new tasks after RequestStop")
	{
		ThreadPool pool(4);
		std::atomic<int> counter{ 0 };

		pool.RequestStop();

		pool.AddTask([&counter]()
			{
				counter.fetch_add(1, std::memory_order_relaxed);
			});

		std::this_thread::sleep_for(100ms);
		REQUIRE(counter.load() == 0);
	}

	SECTION("Submit after RequestStop fails gracefully")
	{
		ThreadPool pool(4);
		pool.RequestStop();

		auto future = pool.Submit([]()
			{
				return 42;
			});

		REQUIRE(future.valid());
	}
}

TEST_CASE("ThreadPool - Destruction", "[threadpool][destruction]")
{
	SECTION("Destruction with empty queue")
	{
		std::atomic<int> counter{ 0 };

		{
			ThreadPool pool(4);

			for (int i = 0; i < 10; ++i)
			{
				pool.AddTask([&counter]()
					{
						counter.fetch_add(1, std::memory_order_relaxed);
					});
			}

			pool.WaitFor(5000ms);
		}

		REQUIRE(counter.load() == 10);
	}

	SECTION("Destruction with pending tasks")
	{
		std::atomic<int> counter{ 0 };

		{
			ThreadPool pool(4);

			for (int i = 0; i < 100; ++i)
			{
				pool.AddTask([&counter]()
					{
						std::this_thread::sleep_for(10ms);
						counter.fetch_add(1, std::memory_order_relaxed);
					});
			}
		}

		REQUIRE(counter.load() >= 0);
	}
}

TEST_CASE("ThreadPool - Concurrent Operations", "[threadpool][concurrent]")
{
	ThreadPool pool(8);

	SECTION("Concurrent AddTask from multiple threads")
	{
		std::atomic<int> counter{ 0 };
		constexpr int numThreads = 10;
		constexpr int tasksPerThread = 100;

		std::vector<std::thread> threads;
		for (int t = 0; t < numThreads; ++t)
		{
			threads.emplace_back([&pool, &counter]()
				{
					for (int i = 0; i < tasksPerThread; ++i)
					{
						pool.AddTask([&counter]()
							{
								counter.fetch_add(1, std::memory_order_relaxed);
							});
					}
				});
		}

		for (auto& thread : threads)
			thread.join();

		REQUIRE(pool.WaitFor(10000ms));
		REQUIRE(counter.load() == numThreads * tasksPerThread);
	}

	SECTION("Concurrent Submit from multiple threads")
	{
		constexpr int numThreads = 10;
		constexpr int tasksPerThread = 50;

		std::vector<std::thread> threads;
		std::vector<std::vector<std::future<int>>> allFutures(numThreads);

		for (int t = 0; t < numThreads; ++t)
		{
			threads.emplace_back([&pool, &allFutures, t]()
				{
					for (int i = 0; i < tasksPerThread; ++i)
					{
						allFutures[t].push_back(pool.Submit([i]()
							{
								return i;
							}));
					}
				});
		}

		for (auto& thread : threads)
			thread.join();

		REQUIRE(pool.WaitFor(10000ms));

		for (int t = 0; t < numThreads; ++t)
		{
			for (int i = 0; i < tasksPerThread; ++i)
			{
				REQUIRE(allFutures[t][i].get() == i);
			}
		}
	}

	SECTION("Concurrent Wait from multiple threads")
	{
		std::atomic<int> counter{ 0 };

		for (int i = 0; i < 100; ++i)
		{
			pool.AddTask([&counter]()
				{
					std::this_thread::sleep_for(10ms);
					counter.fetch_add(1, std::memory_order_relaxed);
				});
		}

		std::vector<std::thread> threads;
		std::atomic<int> waitSuccessCount{ 0 };

		for (int t = 0; t < 5; ++t)
		{
			threads.emplace_back([&pool, &waitSuccessCount]()
				{
					if (pool.WaitFor(10000ms))
						waitSuccessCount.fetch_add(1, std::memory_order_relaxed);
				});
		}

		for (auto& thread : threads)
			thread.join();

		REQUIRE(counter.load() == 100);
		REQUIRE(waitSuccessCount.load() == 5);
	}
}

TEST_CASE("ThreadPool - Edge Cases", "[threadpool][edge]")
{
	SECTION("Tasks that add more tasks")
	{
		ThreadPool pool(4);
		std::atomic<int> counter{ 0 };

		pool.AddTask([&pool, &counter]()
			{
				counter.fetch_add(1, std::memory_order_relaxed);

				pool.AddTask([&counter]()
					{
						counter.fetch_add(1, std::memory_order_relaxed);
					});
			});

		std::this_thread::sleep_for(500ms);
		pool.WaitFor(5000ms);

		REQUIRE(counter.load() == 2);
	}

	SECTION("Large number of threads")
	{
		ThreadPool pool(100);
		std::atomic<int> counter{ 0 };

		for (int i = 0; i < 1000; ++i)
		{
			pool.AddTask([&counter]()
				{
					counter.fetch_add(1, std::memory_order_relaxed);
				});
		}

		REQUIRE(pool.WaitFor(10000ms));
		REQUIRE(counter.load() == 1000);
	}

	SECTION("Tasks with varying durations")
	{
		ThreadPool pool(4);
		std::atomic<int> counter{ 0 };

		for (int i = 0; i < 20; ++i)
		{
			pool.AddTask([&counter, i]()
				{
					if (i % 2 == 0)
						std::this_thread::sleep_for(10ms);
					else
						std::this_thread::sleep_for(50ms);
					counter.fetch_add(1, std::memory_order_relaxed);
				});
		}

		REQUIRE(pool.WaitFor(10000ms));
		REQUIRE(counter.load() == 20);
	}

	SECTION("Empty pool behavior")
	{
		ThreadPool pool(4);
		REQUIRE(pool.WaitFor(100ms));
	}

	SECTION("Immediate RequestStop after construction")
	{
		ThreadPool pool(4);
		pool.RequestStop();

		std::atomic<int> counter{ 0 };
		pool.AddTask([&counter]()
			{
				counter.fetch_add(1, std::memory_order_relaxed);
			});

		std::this_thread::sleep_for(100ms);
		REQUIRE(counter.load() == 0);
	}
}

TEST_CASE("ThreadPool - Stress Test", "[threadpool][stress]")
{
	ThreadPool pool(8);

	SECTION("High volume task processing")
	{
		std::atomic<int> counter{ 0 };
		constexpr int numTasks = 10000;

		for (int i = 0; i < numTasks; ++i)
		{
			pool.AddTask([&counter]()
				{
					counter.fetch_add(1, std::memory_order_relaxed);
				});
		}

		REQUIRE(pool.WaitFor(30000ms));
		REQUIRE(counter.load() == numTasks);
	}

	SECTION("Mixed AddTask and Submit")
	{
		std::atomic<int> addTaskCounter{ 0 };
		std::vector<std::future<int>> futures;
		constexpr int numOperations = 1000;

		for (int i = 0; i < numOperations; ++i)
		{
			if (i % 2 == 0)
			{
				pool.AddTask([&addTaskCounter]()
					{
						addTaskCounter.fetch_add(1, std::memory_order_relaxed);
					});
			}
			else
			{
				futures.push_back(pool.Submit([i]()
					{
						return i;
					}));
			}
		}

		REQUIRE(pool.WaitFor(30000ms));
		REQUIRE(addTaskCounter.load() == numOperations / 2);

		for (size_t i = 0; i < futures.size(); ++i)
		{
			REQUIRE(futures[i].get() == static_cast<int>(i * 2 + 1));
		}
	}
}

TEST_CASE("ThreadPool - Thread Safety", "[threadpool][thread-safety]")
{
	ThreadPool pool(4);

	SECTION("No data races with shared atomic")
	{
		std::atomic<int> counter{ 0 };
		constexpr int numIncrements = 10000;

		for (int i = 0; i < numIncrements; ++i)
		{
			pool.AddTask([&counter]()
				{
					counter.fetch_add(1, std::memory_order_relaxed);
				});
		}

		REQUIRE(pool.WaitFor(30000ms));
		REQUIRE(counter.load() == numIncrements);
	}

	SECTION("GetWorkerCount is thread-safe")
	{
		std::vector<std::thread> threads;

		for (int t = 0; t < 10; ++t)
		{
			threads.emplace_back([&pool]()
				{
					for (int i = 0; i < 100; ++i)
					{
						volatile size_t count = pool.GetWorkerCount();
						(void)count;
					}
				});
		}

		for (auto& thread : threads)
			thread.join();
	}
}
