/**
 * @file Executor.cpp
 * @brief Implementation of the shader execution engine
 * @date 2026-08-24
 */

#include "See/Executor/Executor.hpp"

#include <Concerto/Core/Logger/Logger.hpp>

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

		const ShaderHandle handle = m_nextHandle++;
		m_shaders.emplace(handle, RegisteredShader{stage, spirvCode, entryPoint});
		return handle;
	}

	void Executor::UnregisterShader(ShaderHandle handle)
	{
		m_shaders.erase(handle);
	}
} // namespace see
