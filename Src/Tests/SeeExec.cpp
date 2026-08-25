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

TEST_CASE("see::exec::Run - struct-typed function locals (OpCopyMemory, member AccessChain, whole-struct Load/Extract)", "[see][exec]")
{
	// Mirrors the pattern real shader compilers emit for struct-typed entry-point I/O:
	// a Function-storage struct local copied from Input via OpCopyMemory, member access
	// via AccessChain, and the whole struct re-loaded then split with CompositeExtract.
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%int = OpTypeInt 32 1\n"
		"%float = OpTypeFloat 32\n"
		"%v3float = OpTypeVector %float 3\n"
		"%_ptr_Input_int = OpTypePointer Input %int\n"
		"%_ptr_Output_v3float = OpTypePointer Output %v3float\n"
		"%in_var = OpVariable %_ptr_Input_int Input\n"
		"%out_var = OpVariable %_ptr_Output_v3float Output\n"
		"%InStruct = OpTypeStruct %int\n"
		"%OutStruct = OpTypeStruct %v3float\n"
		"%_ptr_Function_InStruct = OpTypePointer Function %InStruct\n"
		"%_ptr_Function_OutStruct = OpTypePointer Function %OutStruct\n"
		"%_ptr_Function_int = OpTypePointer Function %int\n"
		"%_ptr_Function_v3float = OpTypePointer Function %v3float\n"
		"%int_0 = OpConstant %int 0\n"
		"%float_1 = OpConstant %float 1\n"
		"%float_2 = OpConstant %float 2\n"
		"%float_3 = OpConstant %float 3\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%localOut = OpVariable %_ptr_Function_OutStruct Function\n"
		"%localIn = OpVariable %_ptr_Function_InStruct Function\n"
		"%p1 = OpAccessChain %_ptr_Function_int %localIn %int_0\n"
		"OpCopyMemory %p1 %in_var\n"
		"%p2 = OpAccessChain %_ptr_Function_v3float %localOut %int_0\n"
		"%vec = OpCompositeConstruct %v3float %float_1 %float_2 %float_3\n"
		"OpStore %p2 %vec\n"
		"%whole = OpLoad %OutStruct %localOut\n"
		"%member = OpCompositeExtract %v3float %whole 0\n"
		"OpStore %out_var %member\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 inId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	see::exec::Value input;
	input.m_scalar = see::ir::ScalarKind::Int32;
	input.SetInt32(0, 42);

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {{inId, input}});
	REQUIRE(result.has_value());

	auto outIt = result->find(outId);
	REQUIRE(outIt != result->end());
	CHECK(outIt->second.GetFloat(0) == 1.0f);
	CHECK(outIt->second.GetFloat(1) == 2.0f);
	CHECK(outIt->second.GetFloat(2) == 3.0f);
}

TEST_CASE("see::exec::Run - uniform block bigger than 16 words via an external buffer", "[see][exec]")
{
	// A Value tops out at 16 words, but a uniform block backed by an external buffer (a mapped
	// VkBuffer, in the real caller) isn't - this struct is 17 words (1 float + 1 mat4) and is never
	// materialized whole, only member-by-member through AccessChain, so the cap doesn't apply to it.
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%v4float = OpTypeVector %float 4\n"
		"%mat4v4float = OpTypeMatrix %v4float 4\n"
		"%int = OpTypeInt 32 1\n"
		"%Ubo = OpTypeStruct %float %mat4v4float\n"
		"%_ptr_Uniform_Ubo = OpTypePointer Uniform %Ubo\n"
		"%_ptr_Uniform_float = OpTypePointer Uniform %float\n"
		"%_ptr_Uniform_mat4v4float = OpTypePointer Uniform %mat4v4float\n"
		"%_ptr_Output_float = OpTypePointer Output %float\n"
		"%_ptr_Output_mat4v4float = OpTypePointer Output %mat4v4float\n"
		"%ubo = OpVariable %_ptr_Uniform_Ubo Uniform\n"
		"%outScalar = OpVariable %_ptr_Output_float Output\n"
		"%outMatrix = OpVariable %_ptr_Output_mat4v4float Output\n"
		"%int_0 = OpConstant %int 0\n"
		"%int_1 = OpConstant %int 1\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%p0 = OpAccessChain %_ptr_Uniform_float %ubo %int_0\n"
		"%v0 = OpLoad %float %p0\n"
		"OpStore %outScalar %v0\n"
		"%p1 = OpAccessChain %_ptr_Uniform_mat4v4float %ubo %int_1\n"
		"%v1 = OpLoad %mat4v4float %p1\n"
		"OpStore %outMatrix %v1\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 uboId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outScalarId = module.m_globalInstructions[1].m_id;
	const cct::UInt32 outMatrixId = module.m_globalInstructions[2].m_id;

	std::array<cct::UInt32, 17> uboWords{};
	const float scalarValue = 7.0f;
	std::memcpy(&uboWords[0], &scalarValue, sizeof(float));
	for (cct::UInt32 i = 0; i < 4; ++i)
	{
		const float diagonal = static_cast<float>(i + 1);
		std::memcpy(&uboWords[1 + i * 4 + i], &diagonal, sizeof(float));
	}

	std::unordered_map<cct::UInt32, see::exec::ExternalBuffer> externalBuffers{{uboId, see::exec::ExternalBuffer{uboWords.data(), static_cast<cct::UInt32>(uboWords.size())}}};

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {}, externalBuffers);
	REQUIRE(result.has_value());

	CHECK(result->find(outScalarId)->second.GetFloat() == 7.0f);

	const see::exec::Value& matrix = result->find(outMatrixId)->second;
	for (cct::UInt32 i = 0; i < 4; ++i)
		CHECK(matrix.GetFloat(i * 4 + i) == static_cast<float>(i + 1));
}
