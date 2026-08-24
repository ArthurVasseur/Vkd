/**
 * @file SeeSpirv.cpp
 * @brief Unit tests for the SPIR-V frontend parser (see::spirv::Parse)
 * @date 2026-08-24
 */

#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <See/Spirv/Module.hpp>
#include <spirv-tools/libspirv.h>
#include <spirv/unified1/spirv.h>

namespace
{
	std::vector<cct::UInt32> Assemble(const char* source)
	{
		spv_context context = spvContextCreate(SPV_ENV_UNIVERSAL_1_5);
		spv_binary binary = nullptr;
		spv_diagnostic diagnostic = nullptr;
		spv_result_t result = spvTextToBinary(context, source, std::strlen(source), &binary, &diagnostic);

		std::vector<cct::UInt32> code;
		if (result == SPV_SUCCESS)
			code.assign(binary->code, binary->code + binary->wordCount);

		spvDiagnosticDestroy(diagnostic);
		spvBinaryDestroy(binary);
		spvContextDestroy(context);
		return code;
	}
} // namespace

TEST_CASE("see::spirv::Parse - rejects invalid binaries", "[see][spirv]")
{
	REQUIRE_FALSE(see::spirv::Parse({}).has_value());
	REQUIRE_FALSE(see::spirv::Parse({0xDEADBEEF, 0, 0, 0, 0}).has_value());
}

TEST_CASE("see::spirv::Parse - builds CFG, entry points and decorations for a diamond branch", "[see][spirv]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\" %gid\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"OpDecorate %gid BuiltIn GlobalInvocationId\n"
		"%uint = OpTypeInt 32 0\n"
		"%v3uint = OpTypeVector %uint 3\n"
		"%_ptr_Input_v3uint = OpTypePointer Input %v3uint\n"
		"%gid = OpVariable %_ptr_Input_v3uint Input\n"
		"%void = OpTypeVoid\n"
		"%bool = OpTypeBool\n"
		"%true = OpConstantTrue %bool\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"OpSelectionMerge %merge None\n"
		"OpBranchConditional %true %then %else\n"
		"%then = OpLabel\n"
		"OpBranch %merge\n"
		"%else = OpLabel\n"
		"OpBranch %merge\n"
		"%merge = OpLabel\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const std::vector<cct::UInt32> code = Assemble(Source);
	REQUIRE_FALSE(code.empty());

	std::optional<see::spirv::Module> module = see::spirv::Parse(code);
	REQUIRE(module.has_value());

	REQUIRE(module->m_entryPoints.size() == 1);
	CHECK(module->m_entryPoints[0].m_executionModel == SpvExecutionModelGLCompute);
	CHECK(module->m_entryPoints[0].m_name == "main");
	REQUIRE(module->m_entryPoints[0].m_interfaceIds.size() == 1);

	const cct::UInt32 gidId = module->m_entryPoints[0].m_interfaceIds[0];
	auto decoIt = module->m_decorations.find(gidId);
	REQUIRE(decoIt != module->m_decorations.end());
	REQUIRE(decoIt->second.size() == 1);
	CHECK(decoIt->second[0].m_kind == SpvDecorationBuiltIn);
	REQUIRE(decoIt->second[0].m_literals.size() == 1);
	CHECK(decoIt->second[0].m_literals[0] == SpvBuiltInGlobalInvocationId);

	REQUIRE(module->m_functions.size() == 1);
	const see::spirv::Function& function = module->m_functions[0];
	REQUIRE(function.m_blocks.size() == 4);

	auto trueConstantIt = std::find_if(module->m_globalInstructions.begin(), module->m_globalInstructions.end(), [](const see::spirv::Instruction& instruction)
									   { return instruction.m_opcode == SpvOpConstantTrue; });
	REQUIRE(trueConstantIt != module->m_globalInstructions.end());

	CHECK(function.m_blocks[0].m_terminator == see::spirv::Terminator::BranchConditional);
	CHECK(function.m_blocks[0].m_terminatorOperand == trueConstantIt->m_resultId);
	REQUIRE(function.m_blocks[0].m_branchTargets.size() == 2);
	CHECK(function.m_blocks[0].m_branchTargets[0] == function.m_blocks[1].m_label);
	CHECK(function.m_blocks[0].m_branchTargets[1] == function.m_blocks[2].m_label);

	CHECK(function.m_blocks[1].m_terminator == see::spirv::Terminator::Branch);
	REQUIRE(function.m_blocks[1].m_branchTargets.size() == 1);
	CHECK(function.m_blocks[1].m_branchTargets[0] == function.m_blocks[3].m_label);

	CHECK(function.m_blocks[2].m_terminator == see::spirv::Terminator::Branch);
	REQUIRE(function.m_blocks[2].m_branchTargets.size() == 1);
	CHECK(function.m_blocks[2].m_branchTargets[0] == function.m_blocks[3].m_label);

	CHECK(function.m_blocks[3].m_terminator == see::spirv::Terminator::Return);
	CHECK(function.m_blocks[3].m_branchTargets.empty());
}
