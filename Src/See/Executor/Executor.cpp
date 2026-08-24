/**
 * @file Executor.cpp
 * @brief Implementation of the shader execution engine
 * @date 2026-08-24
 */

#include "See/Executor/Executor.hpp"

#include <algorithm>

#include <Concerto/Core/Logger/Logger.hpp>

#include "See/Spirv/Module.hpp"
#include <spirv-tools/libspirv.h>

namespace see
{
	Executor::Executor() :
		m_context(spvContextCreate(SPV_ENV_UNIVERSAL_1_5)),
		m_nextHandle(1)
	{
	}

	Executor::~Executor()
	{
		spvContextDestroy(m_context);
	}

	bool Executor::IsValidSpirv(const std::vector<cct::UInt32>& spirvCode) const
	{
		spv_diagnostic diagnostic = nullptr;
		spv_result_t result = spvValidateBinary(m_context, spirvCode.data(), spirvCode.size(), &diagnostic);
		if (result == SPV_SUCCESS)
			return true;

		if (diagnostic)
		{
			cct::Logger::Error("SPIR-V validation failed: {}", diagnostic->error);
			spvDiagnosticDestroy(diagnostic);
		}
		return false;
	}

	ShaderHandle Executor::RegisterShader(ShaderStage stage, const std::vector<cct::UInt32>& spirvCode, const std::string& entryPoint)
	{
		if (!IsValidSpirv(spirvCode))
			return InvalidShaderHandle;

		std::optional<spirv::Module> spirvModule = spirv::Parse(spirvCode);
		if (!spirvModule)
			return InvalidShaderHandle;

		auto entryPointIt = std::find_if(spirvModule->m_entryPoints.begin(), spirvModule->m_entryPoints.end(), [&entryPoint](const spirv::EntryPoint& candidate)
										 { return candidate.m_name == entryPoint; });
		if (entryPointIt == spirvModule->m_entryPoints.end())
			return InvalidShaderHandle;

		std::optional<ir::Module> irModule = ir::Convert(*spirvModule);
		if (!irModule)
			return InvalidShaderHandle;

		const ShaderHandle handle = m_nextHandle++;
		m_shaders.emplace(handle, RegisteredShader{.m_stage = stage, .m_code = spirvCode, .m_entryPoint = entryPoint, .m_irModule = std::move(*irModule), .m_entryFunctionId = entryPointIt->m_functionId});
		return handle;
	}

	void Executor::UnregisterShader(ShaderHandle handle)
	{
		m_shaders.erase(handle);
	}

	const ir::Module* Executor::GetIrModule(ShaderHandle handle) const
	{
		auto it = m_shaders.find(handle);
		if (it == m_shaders.end())
			return nullptr;

		return &it->second.m_irModule;
	}

	const ir::Function* Executor::GetEntryFunction(ShaderHandle handle) const
	{
		auto it = m_shaders.find(handle);
		if (it == m_shaders.end())
			return nullptr;

		for (const ir::Function& function : it->second.m_irModule.m_functions)
			if (function.m_id == it->second.m_entryFunctionId)
				return &function;

		return nullptr;
	}
} // namespace see
