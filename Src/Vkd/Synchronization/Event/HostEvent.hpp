/**
 * @file HostEvent.hpp
 * @brief Event whose state lives on the CPU
 * @date 2026-10-07
 *
 * vkSetEvent/vkResetEvent/vkGetEventStatus act on a flag; vkCmdSetEvent/vkCmdResetEvent are applied
 * by the queue when the command buffer executes (software) or completes (AMD).
 */

#pragma once

#include <mutex>

#include "Vkd/Synchronization/Event/Event.hpp"

namespace vkd
{
	class HostEvent : public Event
	{
	public:
		explicit HostEvent(const VkAllocationCallbacks& allocationCallbacks);
		~HostEvent() override = default;

		VkResult GetStatus() override;
		VkResult Set() override;
		VkResult Reset() override;

	private:
		std::mutex m_mutex;
		bool m_signaled;
	};
} // namespace vkd
