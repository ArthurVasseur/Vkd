/**
 * @file See.cpp
 * @brief Unit tests for the shader execution engine skeleton
 * @date 2026-08-24
 */

#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <See/Executor/Executor.hpp>
#include <spirv-tools/libspirv.h>

namespace
{
	std::vector<cct::UInt32> AssembleMinimalComputeShader()
	{
		static constexpr const char* Source =
			"OpCapability Shader\n"
			"OpMemoryModel Logical GLSL450\n"
			"OpEntryPoint GLCompute %main \"main\"\n"
			"OpExecutionMode %main LocalSize 1 1 1\n"
			"%void = OpTypeVoid\n"
			"%voidfn = OpTypeFunction %void\n"
			"%main = OpFunction %void None %voidfn\n"
			"%entry = OpLabel\n"
			"OpReturn\n"
			"OpFunctionEnd\n";

		spv_context context = spvContextCreate(SPV_ENV_UNIVERSAL_1_5);
		spv_binary binary = nullptr;
		spv_diagnostic diagnostic = nullptr;
		spv_result_t result = spvTextToBinary(context, Source, std::strlen(Source), &binary, &diagnostic);

		std::vector<cct::UInt32> code;
		if (result == SPV_SUCCESS)
			code.assign(binary->code, binary->code + binary->wordCount);

		spvDiagnosticDestroy(diagnostic);
		spvBinaryDestroy(binary);
		spvContextDestroy(context);
		return code;
	}
} // namespace

TEST_CASE("See::Executor - rejects invalid SPIR-V", "[see][executor]")
{
	see::Executor executor;

	SECTION("Empty binary is invalid")
	{
		REQUIRE_FALSE(executor.IsValidSpirv({}));
		REQUIRE(executor.RegisterShader(see::ShaderStage::Vertex, {}, "main") == see::InvalidShaderHandle);
	}

	SECTION("Garbage magic number is invalid")
	{
		const std::vector<cct::UInt32> garbage = {0xDEADBEEF, 0, 0, 0, 0};
		REQUIRE_FALSE(executor.IsValidSpirv(garbage));
	}
}

TEST_CASE("See::Executor - accepts and registers a valid SPIR-V module", "[see][executor]")
{
	const std::vector<cct::UInt32> code = AssembleMinimalComputeShader();
	REQUIRE_FALSE(code.empty());

	see::Executor executor;
	REQUIRE(executor.IsValidSpirv(code));

	const see::ShaderHandle handle = executor.RegisterShader(see::ShaderStage::Compute, code, "main");
	REQUIRE(handle != see::InvalidShaderHandle);

	executor.UnregisterShader(handle);
}

TEST_CASE("See::Executor - exposes the parsed IR for a registered shader", "[see][executor]")
{
	const std::vector<cct::UInt32> code = AssembleMinimalComputeShader();
	REQUIRE_FALSE(code.empty());

	see::Executor executor;
	const see::ShaderHandle handle = executor.RegisterShader(see::ShaderStage::Compute, code, "main");
	REQUIRE(handle != see::InvalidShaderHandle);

	const see::ir::Module* module = executor.GetIrModule(handle);
	REQUIRE(module != nullptr);

	const see::ir::Function* entryFunction = executor.GetEntryFunction(handle);
	REQUIRE(entryFunction != nullptr);
	REQUIRE_FALSE(entryFunction->m_blocks.empty());
	CHECK(entryFunction->m_blocks[0].m_terminator == see::ir::Terminator::Return);
}

TEST_CASE("See::Executor - rejects an entry point name that doesn't exist in the module", "[see][executor]")
{
	const std::vector<cct::UInt32> code = AssembleMinimalComputeShader();
	REQUIRE_FALSE(code.empty());

	see::Executor executor;
	REQUIRE(executor.RegisterShader(see::ShaderStage::Compute, code, "doesNotExist") == see::InvalidShaderHandle);
}

TEST_CASE("See::Executor - GetIrModule/GetEntryFunction return null for an unknown handle", "[see][executor]")
{
	see::Executor executor;
	CHECK(executor.GetIrModule(see::InvalidShaderHandle) == nullptr);
	CHECK(executor.GetEntryFunction(see::InvalidShaderHandle) == nullptr);
}
