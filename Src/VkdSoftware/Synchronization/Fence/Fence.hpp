/**
 * @file Fence.hpp
 * @brief Software renderer fence implementation
 * @date 2025-10-25
 *
 * CPU-based fence synchronization using atomic operations.
 */

#pragma once

#include <mutex>
#include <condition_variable>

#include "Vkd/Synchronization/Fence/Fence.hpp"

CCT_ENABLE_ENUM_FLAGS(VkFenceCreateFlagBits)

namespace vkd::software
{
	class Fence : public vkd::Fence
	{
	public:
		Fence();
		~Fence() override = default;

		VkResult Create(Device& owner, const VkFenceCreateInfo& createInfo) override;

		VkResult GetStatus() override;
		VkResult Wait(uint64_t timeout) override;
		VkResult Reset() override;
		VkResult Signal() override;

	private:
		std::mutex m_mutex;
		std::condition_variable m_cv;
		bool m_signaled;
	};
}
