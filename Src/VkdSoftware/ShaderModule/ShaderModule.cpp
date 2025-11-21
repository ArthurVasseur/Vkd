/**
 * @file ShaderModule.cpp
 * @brief Implementation of software shader module
 * @date 2025-11-21
 */

#include "VkdSoftware/ShaderModule/ShaderModule.hpp"

#include "VkdSoftware/Device/Device.hpp"

namespace vkd::software
{
	VkResult ShaderModule::Create(vkd::Device& owner, const VkShaderModuleCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		return vkd::ShaderModule::Create(owner, info, allocationCallbacks);
	}
} // namespace vkd::software
