/**
 * @file Executor.hpp
 * @brief Shader Execution Engine, SPIR-V shader executor
 * @date 2026-08-24
 */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <Concerto/Core/Types/Types.hpp>

#include "See/Ir/Module.hpp"

struct spv_context_t;

namespace see
{
	enum class ShaderStage : cct::UInt32
	{
		Vertex,
		Fragment,
		Compute
	};

	using ShaderHandle = cct::UInt64;
	inline constexpr ShaderHandle InvalidShaderHandle = 0;

	class Executor
	{
	public:
		Executor();
		~Executor();

		Executor(const Executor&) = delete;
		Executor(Executor&&) = delete;
		Executor& operator=(const Executor&) = delete;
		Executor& operator=(Executor&&) = delete;

		// Validates, parses and converts to IR. Returns InvalidShaderHandle if any of that fails,
		// or if no OpEntryPoint in the module matches `entryPoint`.
		[[nodiscard]] ShaderHandle RegisterShader(ShaderStage stage, const std::vector<cct::UInt32>& spirvCode, const std::string& entryPoint);
		void UnregisterShader(ShaderHandle handle);

		[[nodiscard]] bool IsValidSpirv(const std::vector<cct::UInt32>& spirvCode) const;

		[[nodiscard]] const ir::Module* GetIrModule(ShaderHandle handle) const;
		[[nodiscard]] const ir::Function* GetEntryFunction(ShaderHandle handle) const;

	private:
		struct RegisteredShader
		{
			ShaderStage m_stage;
			std::vector<cct::UInt32> m_code;
			std::string m_entryPoint;
			ir::Module m_irModule;
			cct::UInt32 m_entryFunctionId;
		};

		spv_context_t* m_context;
		std::unordered_map<ShaderHandle, RegisteredShader> m_shaders;
		ShaderHandle m_nextHandle;
	};
} // namespace see
