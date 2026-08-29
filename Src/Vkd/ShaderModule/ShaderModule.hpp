/**
 * @file ShaderModule.hpp
 * @brief Vulkan shader module abstraction
 * @date 2025-11-21
 */

#pragma once

#include <Concerto/Core/Types/Types.hpp>

#include "Vkd/Memory/Stl.hpp"
#include "Vkd/ObjectBase/ObjectBase.hpp"

namespace vkd
{
	class Device;

	class ShaderModule : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_SHADER_MODULE;
		VKD_NON_DISPATCHABLE_HANDLE(ShaderModule);

		explicit ShaderModule(const VkAllocationCallbacks& allocationCallbacks);
		~ShaderModule() override = default;

		virtual VkResult Create(Device& owner, const VkShaderModuleCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline const vkd::vector<cct::UInt32>& GetCode() const;
		[[nodiscard]] inline cct::UInt32 GetCodeSize() const;

	protected:
		Device* m_owner;
		vkd::vector<cct::UInt32> m_code;
	};
} // namespace vkd

#include "Vkd/ShaderModule/ShaderModule.inl"
