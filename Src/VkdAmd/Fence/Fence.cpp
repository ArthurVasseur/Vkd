#include "VkdAmd/Fence/Fence.hpp"

#include <chrono>
#include <limits>

namespace vkd::amd
{
	Fence::Fence(const VkAllocationCallbacks& allocationCallbacks) :
		vkd::Fence(allocationCallbacks),
		m_signaled(false)
	{
	}

	VkResult Fence::Create(Device& owner, const VkFenceCreateInfo& createInfo)
	{
		cct::EnumFlags<VkFenceCreateFlagBits> flags(createInfo.flags);
		m_signaled = flags.Contains(VK_FENCE_CREATE_SIGNALED_BIT);
		return vkd::Fence::Create(owner, createInfo);
	}

	VkResult Fence::GetStatus()
	{
		std::lock_guard lock(m_mutex);
		return m_signaled ? VK_SUCCESS : VK_NOT_READY;
	}

	VkResult Fence::Wait(uint64_t timeout)
	{
		using Clock = std::chrono::steady_clock;
		std::unique_lock lock(m_mutex);

		if (m_signaled)
			return VK_SUCCESS;
		if (timeout == 0)
			return VK_TIMEOUT;

		if (timeout == std::numeric_limits<uint64_t>::max())
		{
			m_cv.wait(lock, [this]
					  { return m_signaled; });
			return VK_SUCCESS;
		}

		const auto deadline = Clock::now() + std::chrono::nanoseconds(timeout);
		if (!m_cv.wait_until(lock, deadline, [this]
							 { return m_signaled; }))
			return VK_TIMEOUT;
		return VK_SUCCESS;
	}

	VkResult Fence::Reset()
	{
		std::lock_guard lock(m_mutex);
		m_signaled = false;
		return VK_SUCCESS;
	}

	VkResult Fence::Signal()
	{
		{
			std::lock_guard lock(m_mutex);
			m_signaled = true;
		}
		m_cv.notify_all();
		return VK_SUCCESS;
	}
} // namespace vkd::amd
