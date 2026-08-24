/**
 * @file SeeIr.cpp
 * @brief Unit tests for the SPIR-V -> IR conversion (see::ir::Convert)
 * @date 2026-08-24
 */

#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <See/Ir/Module.hpp>
#include <See/Spirv/Module.hpp>
#include <spirv-tools/libspirv.h>

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

	see::ir::Module ParseAndConvert(const char* source)
	{
		const std::vector<cct::UInt32> code = Assemble(source);
		REQUIRE_FALSE(code.empty());

		std::optional<see::spirv::Module> spirvModule = see::spirv::Parse(code);
		REQUIRE(spirvModule.has_value());

		std::optional<see::ir::Module> irModule = see::ir::Convert(*spirvModule);
		REQUIRE(irModule.has_value());

		return std::move(*irModule);
	}
} // namespace

TEST_CASE("see::ir::Convert - passthrough load/store", "[see][ir]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%_ptr_Private_float = OpTypePointer Private %float\n"
		"%in = OpVariable %_ptr_Private_float Private\n"
		"%out = OpVariable %_ptr_Private_float Private\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%v = OpLoad %float %in\n"
		"OpStore %out %v\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);

	// Two OpVariable globals (in, out), both Pointer-to-float.
	REQUIRE(module.m_globalInstructions.size() == 2);
	for (const see::ir::Instruction& global : module.m_globalInstructions)
	{
		CHECK(global.m_op == see::ir::Op::Variable);
		auto typeIt = module.m_types.find(global.m_type);
		REQUIRE(typeIt != module.m_types.end());
		CHECK(typeIt->second.m_kind == see::ir::TypeKind::Pointer);
	}
	const cct::UInt32 inId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	REQUIRE(module.m_functions.size() == 1);
	REQUIRE(module.m_functions[0].m_blocks.size() == 1);
	const see::ir::BasicBlock& block = module.m_functions[0].m_blocks[0];
	CHECK(block.m_terminator == see::ir::Terminator::Return);

	REQUIRE(block.m_instructions.size() == 2);

	const see::ir::Instruction& load = block.m_instructions[0];
	CHECK(load.m_op == see::ir::Op::Load);
	REQUIRE(load.m_args.size() == 1);
	CHECK(load.m_args[0] == inId);
	auto loadTypeIt = module.m_types.find(load.m_type);
	REQUIRE(loadTypeIt != module.m_types.end());
	CHECK(loadTypeIt->second.m_kind == see::ir::TypeKind::Scalar);
	CHECK(loadTypeIt->second.m_scalar == see::ir::ScalarKind::Float32);

	const see::ir::Instruction& store = block.m_instructions[1];
	CHECK(store.m_op == see::ir::Op::Store);
	CHECK(store.m_id == 0);
	REQUIRE(store.m_args.size() == 2);
	CHECK(store.m_args[0] == outId);
	CHECK(store.m_args[1] == load.m_id);
}

TEST_CASE("see::ir::Convert - scalar arithmetic wires operands through SSA ids", "[see][ir]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%_ptr_Private_float = OpTypePointer Private %float\n"
		"%a = OpVariable %_ptr_Private_float Private\n"
		"%b = OpVariable %_ptr_Private_float Private\n"
		"%out = OpVariable %_ptr_Private_float Private\n"
		"%two = OpConstant %float 2\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%va = OpLoad %float %a\n"
		"%vb = OpLoad %float %b\n"
		"%sum = OpFAdd %float %va %vb\n"
		"%prod = OpFMul %float %sum %two\n"
		"OpStore %out %prod\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);

	REQUIRE(module.m_globalInstructions.size() == 3);
	const cct::UInt32 outId = module.m_globalInstructions[2].m_id;

	REQUIRE(module.m_constants.size() == 1);
	const cct::UInt32 twoId = module.m_constants.begin()->first;
	const see::ir::Constant& twoConstant = module.m_constants.begin()->second;
	auto floatTypeIt = module.m_types.find(twoConstant.m_type);
	REQUIRE(floatTypeIt != module.m_types.end());
	CHECK(floatTypeIt->second.m_scalar == see::ir::ScalarKind::Float32);
	float twoValue;
	std::memcpy(&twoValue, &twoConstant.m_bits, sizeof(float));
	CHECK(twoValue == 2.0f);

	REQUIRE(module.m_functions.size() == 1);
	const see::ir::BasicBlock& block = module.m_functions[0].m_blocks[0];
	REQUIRE(block.m_instructions.size() == 5);

	const see::ir::Instruction& loadA = block.m_instructions[0];
	const see::ir::Instruction& loadB = block.m_instructions[1];
	const see::ir::Instruction& sum = block.m_instructions[2];
	const see::ir::Instruction& prod = block.m_instructions[3];
	const see::ir::Instruction& store = block.m_instructions[4];

	CHECK(loadA.m_op == see::ir::Op::Load);
	CHECK(loadB.m_op == see::ir::Op::Load);

	CHECK(sum.m_op == see::ir::Op::Add);
	REQUIRE(sum.m_args.size() == 2);
	CHECK(sum.m_args[0] == loadA.m_id);
	CHECK(sum.m_args[1] == loadB.m_id);

	CHECK(prod.m_op == see::ir::Op::Mul);
	REQUIRE(prod.m_args.size() == 2);
	CHECK(prod.m_args[0] == sum.m_id);
	CHECK(prod.m_args[1] == twoId);

	CHECK(store.m_op == see::ir::Op::Store);
	REQUIRE(store.m_args.size() == 2);
	CHECK(store.m_args[0] == outId);
	CHECK(store.m_args[1] == prod.m_id);
}

TEST_CASE("see::ir::Convert - vector/matrix types and select/compare ops", "[see][ir]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%v4float = OpTypeVector %float 4\n"
		"%mat4v4float = OpTypeMatrix %v4float 4\n"
		"%bool = OpTypeBool\n"
		"%_ptr_Private_v4float = OpTypePointer Private %v4float\n"
		"%_ptr_Private_mat4v4float = OpTypePointer Private %mat4v4float\n"
		"%m = OpVariable %_ptr_Private_mat4v4float Private\n"
		"%v = OpVariable %_ptr_Private_v4float Private\n"
		"%out = OpVariable %_ptr_Private_v4float Private\n"
		"%true = OpConstantTrue %bool\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%vm = OpLoad %mat4v4float %m\n"
		"%vv = OpLoad %v4float %v\n"
		"%mv = OpMatrixTimesVector %v4float %vm %vv\n"
		"%sel = OpSelect %v4float %true %mv %vv\n"
		"OpStore %out %sel\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);

	auto vecTypeIt = std::find_if(module.m_types.begin(), module.m_types.end(), [](const auto& pair)
								  { return pair.second.m_kind == see::ir::TypeKind::Vector; });
	REQUIRE(vecTypeIt != module.m_types.end());
	CHECK(vecTypeIt->second.m_componentCount == 4);
	CHECK(vecTypeIt->second.m_scalar == see::ir::ScalarKind::Float32);

	auto matTypeIt = std::find_if(module.m_types.begin(), module.m_types.end(), [](const auto& pair)
								  { return pair.second.m_kind == see::ir::TypeKind::Matrix; });
	REQUIRE(matTypeIt != module.m_types.end());
	CHECK(matTypeIt->second.m_componentCount == 4);
	CHECK(matTypeIt->second.m_pointee == vecTypeIt->first);

	REQUIRE(module.m_functions.size() == 1);
	const see::ir::BasicBlock& block = module.m_functions[0].m_blocks[0];
	REQUIRE(block.m_instructions.size() == 5);

	CHECK(block.m_instructions[2].m_op == see::ir::Op::MatrixTimesVector);
	CHECK(block.m_instructions[3].m_op == see::ir::Op::Select);
	REQUIRE(block.m_instructions[3].m_args.size() == 3);
}
