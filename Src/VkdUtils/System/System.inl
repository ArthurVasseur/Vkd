/**
 * @file System.inl
 * @brief Inline implementations for system information utilities
 * @date 2025-10-30
 */

#pragma once

#include "VkdUtils/System/System.hpp"

namespace vkd::utils
{
	inline System::System()
		: m_totalRamBytes(std::nullopt)
		, m_availableRamBytes(std::nullopt)
	{
	}

	inline void System::InvalidateCache()
	{
		m_totalRamBytes.reset();
		m_availableRamBytes.reset();
	}
}
