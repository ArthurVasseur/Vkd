/**
 * @file CpuContext.inl
 * @brief Inline implementations for CPU rendering context
 * @date 2025-10-27
 */

#pragma once

#include "VkdSoftware/CpuContext/CpuContext.hpp"

namespace vkd::software
{
	inline void CpuContext::Reset()
	{
		m_boundPipeline = nullptr;
		m_boundVertexBuffers.clear();
		m_vertexBufferOffsets.clear();
	}
}
