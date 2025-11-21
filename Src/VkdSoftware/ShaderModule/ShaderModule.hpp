/**
 * @file ShaderModule.hpp
 * @brief Software shader module implementation
 * @date 2025-11-21
 */

#pragma once

#include "Vkd/ShaderModule/ShaderModule.hpp"

namespace vkd::software
{
	class ShaderModule : public vkd::ShaderModule
	{
	public:
		ShaderModule() = default;
		~ShaderModule() override = default;

		VkResult Create(vkd::Device& owner, const VkShaderModuleCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks) override;
	};
} // namespace vkd::software
