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

		if (info.codeSize == 0 || !info.pCode || (info.codeSize % sizeof(UInt32)) != 0)
		{
			m_createResult = VK_ERROR_INITIALIZATION_FAILED;
			return m_createResult;
		}

		const UInt32 codeWordCount = static_cast<UInt32>(info.codeSize / sizeof(UInt32));
		m_code.resize(codeWordCount);

		const UInt32* pCodeWords = static_cast<const UInt32*>(info.pCode);
		for (UInt32 i = 0; i < codeWordCount; ++i)
			m_code[i] = pCodeWords[i];

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}
} // namespace vkd
