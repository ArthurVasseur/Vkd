/**
 * @file Event.hpp
 * @brief Software renderer event
 * @date 2026-08-30
 */

#pragma once

#include <mutex>

#include "Vkd/Synchronization/Event/Event.hpp"

namespace vkd::software
{
	class Event : public vkd::Event
	{
	public:
		explicit Event(const VkAllocationCallbacks& allocationCallbacks);
		~Event() override = default;

		VkResult GetStatus() override;
		VkResult Set() override;
		VkResult Reset() override;

	private:
		std::mutex m_mutex;
		bool m_signaled;
	};
} // namespace vkd::software
