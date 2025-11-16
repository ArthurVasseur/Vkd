/**
 * @file CommandDispatcher.inl
 * @brief Inline implementations for command dispatcher
 * @date 2025-10-27
 */

#pragma once

#include "VkdSoftware/CommandDispatcher/CommandDispatcher.hpp"

namespace vkd::software
{
	inline CommandDispatcher::CommandDispatcher(CpuContext& ctx) :
		m_context(&ctx)
	{
	}
} // namespace vkd::software
