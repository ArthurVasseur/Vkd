/**
 * @file ShaderModule.cpp
 * @brief Implementation of ShaderModule
 * @date 2025-11-21
 */

#include "Vkd/ShaderModule/ShaderModule.hpp"

namespace vkd
{
	VkResult ShaderModule::Create(Device& owner, const VkShaderModuleCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;
		SetAllocationCallbacks(allocationCallbacks);

		if (info.codeSize == 0 || !info.pCode)
			return VK_ERROR_INITIALIZATION_FAILED;

		cct::UInt32 codeWordCount = info.codeSize / sizeof(cct::UInt32);
		m_code.resize(codeWordCount);

		const cct::UInt32* pCodeWords = static_cast<const cct::UInt32*>(info.pCode);
		for (cct::UInt32 i = 0; i < codeWordCount; ++i)
			m_code[i] = pCodeWords[i];

		return VK_SUCCESS;
	}
} // namespace vkd
