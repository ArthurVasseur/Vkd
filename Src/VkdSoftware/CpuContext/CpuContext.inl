#pragma once

#include "VkdSoftware/CpuContext/CpuContext.hpp"

namespace vkd::software
{
	inline void CpuContext::Reset()
	{
		m_boundPipeline = nullptr;
		m_boundVertexBuffer = nullptr;
		m_vertexBufferOffset = 0;
	}
}
