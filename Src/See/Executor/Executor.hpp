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

		[[nodiscard]] ShaderHandle RegisterShader(ShaderStage stage, const std::vector<cct::UInt32>& spirvCode, const std::string& entryPoint);
		void UnregisterShader(ShaderHandle handle);

		[[nodiscard]] bool IsValidSpirv(const std::vector<cct::UInt32>& spirvCode) const;

	private:
		struct RegisteredShader
		{
			ShaderStage Stage;
			std::vector<cct::UInt32> Code;
			std::string EntryPoint;
		};

		spv_context_t* m_context;
		std::unordered_map<ShaderHandle, RegisteredShader> m_shaders;
		ShaderHandle m_nextHandle;
	};
} // namespace see
