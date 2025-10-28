//
// Created by arthur on 27/10/2025.
//

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
