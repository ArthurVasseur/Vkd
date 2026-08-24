/**
 * @file SeeExec.cpp
 * @brief Unit tests for the scalar IR interpreter (see::exec::Run)
 * @date 2026-08-24
 */

#include <cstring>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <See/Exec/Scalar.hpp>
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

TEST_CASE("see::exec::Run - passthrough load/store", "[see][exec]")
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
	const cct::UInt32 inId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	see::exec::Value input;
	input.SetFloat(0, 42.0f);

	std::unordered_map<cct::UInt32, see::exec::Value> inputs{{inId, input}};
	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
	REQUIRE(result.has_value());

	auto outIt = result->find(outId);
	REQUIRE(outIt != result->end());
	CHECK(outIt->second.GetFloat() == 42.0f);
}

TEST_CASE("see::exec::Run - scalar arithmetic", "[see][exec]")
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
	const cct::UInt32 aId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 bId = module.m_globalInstructions[1].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[2].m_id;

	see::exec::Value a;
	a.SetFloat(0, 3.0f);
	see::exec::Value b;
	b.SetFloat(0, 4.0f);

	std::unordered_map<cct::UInt32, see::exec::Value> inputs{{aId, a}, {bId, b}};
	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
	REQUIRE(result.has_value());

	auto outIt = result->find(outId);
	REQUIRE(outIt != result->end());
	CHECK(outIt->second.GetFloat() == 14.0f); // (3 + 4) * 2
}

TEST_CASE("see::exec::Run - conditional branch picks the right block", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%bool = OpTypeBool\n"
		"%_ptr_Private_float = OpTypePointer Private %float\n"
		"%_ptr_Private_bool = OpTypePointer Private %bool\n"
		"%cond = OpVariable %_ptr_Private_bool Private\n"
		"%out = OpVariable %_ptr_Private_float Private\n"
		"%one = OpConstant %float 1\n"
		"%two = OpConstant %float 2\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%c = OpLoad %bool %cond\n"
		"OpSelectionMerge %merge None\n"
		"OpBranchConditional %c %then %else\n"
		"%then = OpLabel\n"
		"OpStore %out %one\n"
		"OpBranch %merge\n"
		"%else = OpLabel\n"
		"OpStore %out %two\n"
		"OpBranch %merge\n"
		"%merge = OpLabel\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 condId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	SECTION("condition true takes the then branch")
	{
		see::exec::Value cond;
		cond.m_scalar = see::ir::ScalarKind::Bool;
		cond.SetBool(0, true);

		std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {{condId, cond}});
		REQUIRE(result.has_value());
		CHECK(result->find(outId)->second.GetFloat() == 1.0f);
	}

	SECTION("condition false takes the else branch")
	{
		see::exec::Value cond;
		cond.m_scalar = see::ir::ScalarKind::Bool;
		cond.SetBool(0, false);

		std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {{condId, cond}});
		REQUIRE(result.has_value());
		CHECK(result->find(outId)->second.GetFloat() == 2.0f);
	}
}

TEST_CASE("see::exec::Run - matrix times vector", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%v4float = OpTypeVector %float 4\n"
		"%mat4v4float = OpTypeMatrix %v4float 4\n"
		"%_ptr_Private_v4float = OpTypePointer Private %v4float\n"
		"%_ptr_Private_mat4v4float = OpTypePointer Private %mat4v4float\n"
		"%m = OpVariable %_ptr_Private_mat4v4float Private\n"
		"%v = OpVariable %_ptr_Private_v4float Private\n"
		"%out = OpVariable %_ptr_Private_v4float Private\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%vm = OpLoad %mat4v4float %m\n"
		"%vv = OpLoad %v4float %v\n"
		"%r = OpMatrixTimesVector %v4float %vm %vv\n"
		"OpStore %out %r\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 mId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 vId = module.m_globalInstructions[1].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[2].m_id;

	// 2 * identity.
	see::exec::Value matrix;
	matrix.m_rows = 4;
	matrix.m_columns = 4;
	for (cct::UInt32 i = 0; i < 4; ++i)
		matrix.SetFloat(i * 4 + i, 2.0f);

	see::exec::Value vector;
	vector.m_rows = 4;
	for (cct::UInt32 i = 0; i < 4; ++i)
		vector.SetFloat(i, static_cast<float>(i + 1)); // (1, 2, 3, 4)

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {{mId, matrix}, {vId, vector}});
	REQUIRE(result.has_value());

	const see::exec::Value& out = result->find(outId)->second;
	CHECK(out.GetFloat(0) == 2.0f);
	CHECK(out.GetFloat(1) == 4.0f);
	CHECK(out.GetFloat(2) == 6.0f);
	CHECK(out.GetFloat(3) == 8.0f);
}
