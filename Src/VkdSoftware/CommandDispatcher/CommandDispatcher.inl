//
// Created by arthur on 27/10/2025.
//

#pragma once

#include "VkdSoftware/CommandDispatcher/CommandDispatcher.hpp"

namespace vkd::software
{
	inline CommandDispatcher::CommandDispatcher(CpuContext& ctx) :
		m_context(&ctx)
	{
	}
}
