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

TEST_CASE("see::exec::Run - OpFunctionCall passes arguments and returns a value", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%float_fn = OpTypeFunction %float %float\n"
		"%_ptr_Private_float = OpTypePointer Private %float\n"
		"%in = OpVariable %_ptr_Private_float Private\n"
		"%out = OpVariable %_ptr_Private_float Private\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%v = OpLoad %float %in\n"
		"%r = OpFunctionCall %float %double %v\n"
		"OpStore %out %r\n"
		"OpReturn\n"
		"OpFunctionEnd\n"
		"%double = OpFunction %float None %float_fn\n"
		"%param = OpFunctionParameter %float\n"
		"%label = OpLabel\n"
		"%sum = OpFAdd %float %param %param\n"
		"OpReturnValue %sum\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 inId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	see::exec::Value input;
	input.SetFloat(0, 3.0f);

	std::unordered_map<cct::UInt32, see::exec::Value> inputs{{inId, input}};
	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
	REQUIRE(result.has_value());

	auto outIt = result->find(outId);
	REQUIRE(outIt != result->end());
	CHECK(outIt->second.GetFloat() == 6.0f);
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

TEST_CASE("see::exec::Run - OpTypeArray indexed dynamically via OpAccessChain", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%int = OpTypeInt 32 1\n"
		"%uint = OpTypeInt 32 0\n"
		"%uint_4 = OpConstant %uint 4\n"
		"%arr4float = OpTypeArray %float %uint_4\n"
		"%_ptr_Private_arr4float = OpTypePointer Private %arr4float\n"
		"%_ptr_Private_float = OpTypePointer Private %float\n"
		"%_ptr_Private_int = OpTypePointer Private %int\n"
		"%arr = OpVariable %_ptr_Private_arr4float Private\n"
		"%idx = OpVariable %_ptr_Private_int Private\n"
		"%out = OpVariable %_ptr_Private_float Private\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%i = OpLoad %int %idx\n"
		"%p = OpAccessChain %_ptr_Private_float %arr %i\n"
		"%v = OpLoad %float %p\n"
		"OpStore %out %v\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 arrId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 idxId = module.m_globalInstructions[1].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[2].m_id;

	see::exec::Value arr;
	arr.m_rows = 4;
	arr.SetFloat(0, 10.0f);
	arr.SetFloat(1, 20.0f);
	arr.SetFloat(2, 30.0f);
	arr.SetFloat(3, 40.0f);

	see::exec::Value idx;
	idx.m_scalar = see::ir::ScalarKind::Int32;
	idx.SetInt32(0, 2);

	std::unordered_map<cct::UInt32, see::exec::Value> inputs{{arrId, arr}, {idxId, idx}};
	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
	REQUIRE(result.has_value());

	CHECK(result->find(outId)->second.GetFloat() == 30.0f);
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

TEST_CASE("see::exec::Run - external buffer struct member offset from OpMemberDecorate", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"OpDecorate %Block BufferBlock\n"
		"OpMemberDecorate %Block 0 Offset 0\n"
		"OpMemberDecorate %Block 1 Offset 16\n"
		"%float = OpTypeFloat 32\n"
		"%v3float = OpTypeVector %float 3\n"
		"%Block = OpTypeStruct %v3float %float\n"
		"%_ptr_Uniform_Block = OpTypePointer Uniform %Block\n"
		"%_ptr_Uniform_float = OpTypePointer Uniform %float\n"
		"%_ptr_Output_float = OpTypePointer Output %float\n"
		"%block = OpVariable %_ptr_Uniform_Block Uniform\n"
		"%out = OpVariable %_ptr_Output_float Output\n"
		"%int = OpTypeInt 32 1\n"
		"%int_1 = OpConstant %int 1\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%p = OpAccessChain %_ptr_Uniform_float %block %int_1\n"
		"%v = OpLoad %float %p\n"
		"OpStore %out %v\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 blockId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	std::array<cct::UInt32, 5> blockWords{};
	const float decoy = 99.0f;
	const float real = 22.0f;
	std::memcpy(&blockWords[3], &decoy, sizeof(float));
	std::memcpy(&blockWords[4], &real, sizeof(float));

	std::unordered_map<cct::UInt32, see::exec::ExternalBuffer> externalBuffers{{blockId, see::exec::ExternalBuffer{blockWords.data(), static_cast<cct::UInt32>(blockWords.size())}}};

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {}, externalBuffers);
	REQUIRE(result.has_value());

	CHECK(result->find(outId)->second.GetFloat() == 22.0f);
}

TEST_CASE("see::exec::Run - nearest-neighbor image sampling through an external image", "[see][exec]")
{
	// Mirrors the real pattern (verified via spirv-dis against NZSL's sampler2D[f32].Sample()
	// codegen): a combined image/sampler loaded from a UniformConstant variable, then sampled
	// with OpImageSampleImplicitLod.
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"OpDecorate %tex DescriptorSet 0\n"
		"OpDecorate %tex Binding 0\n"
		"%float = OpTypeFloat 32\n"
		"%v2float = OpTypeVector %float 2\n"
		"%v4float = OpTypeVector %float 4\n"
		"%image = OpTypeImage %float 2D 0 0 0 1 Unknown\n"
		"%sampledImage = OpTypeSampledImage %image\n"
		"%_ptr_UniformConstant_sampledImage = OpTypePointer UniformConstant %sampledImage\n"
		"%_ptr_Private_v2float = OpTypePointer Private %v2float\n"
		"%_ptr_Private_v4float = OpTypePointer Private %v4float\n"
		"%tex = OpVariable %_ptr_UniformConstant_sampledImage UniformConstant\n"
		"%uv = OpVariable %_ptr_Private_v2float Private\n"
		"%outColor = OpVariable %_ptr_Private_v4float Private\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%img = OpLoad %sampledImage %tex\n"
		"%coord = OpLoad %v2float %uv\n"
		"%sampled = OpImageSampleImplicitLod %v4float %img %coord\n"
		"OpStore %outColor %sampled\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 texId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 uvId = module.m_globalInstructions[1].m_id;
	const cct::UInt32 outColorId = module.m_globalInstructions[2].m_id;

	// A 2x2 RGBA8 texture, a distinct color per texel so nearest-neighbor picking the wrong one is obvious.
	std::array<cct::UInt8, 2 * 2 * 4> texels = {
		255, 0, 0, 255, 0, 255, 0, 255,
		0, 0, 255, 255, 255, 255, 0, 255};

	see::exec::Value uv;
	uv.m_rows = 2;
	uv.SetFloat(0, 0.9f); // right column
	uv.SetFloat(1, 0.9f); // bottom row -> bottom-right texel (255, 255, 0, 255)

	std::unordered_map<cct::UInt32, see::exec::ExternalImage> externalImages{{texId, see::exec::ExternalImage{texels.data(), 2, 2}}};

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {{uvId, uv}}, {}, externalImages);
	REQUIRE(result.has_value());

	const see::exec::Value& color = result->find(outColorId)->second;
	CHECK(color.GetFloat(0) == 1.0f);
	CHECK(color.GetFloat(1) == 1.0f);
	CHECK(color.GetFloat(2) == 0.0f);
	CHECK(color.GetFloat(3) == 1.0f);
}

TEST_CASE("see::exec::Run - OpLogicalEqual on a bool3 reduced by OpAll", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%bool = OpTypeBool\n"
		"%v3bool = OpTypeVector %bool 3\n"
		"%_ptr_Private_v3bool = OpTypePointer Private %v3bool\n"
		"%_ptr_Private_bool = OpTypePointer Private %bool\n"
		"%lhs = OpVariable %_ptr_Private_v3bool Private\n"
		"%rhs = OpVariable %_ptr_Private_v3bool Private\n"
		"%out = OpVariable %_ptr_Private_bool Private\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%a = OpLoad %v3bool %lhs\n"
		"%b = OpLoad %v3bool %rhs\n"
		"%eq = OpLogicalEqual %v3bool %a %b\n"
		"%allEq = OpAll %bool %eq\n"
		"OpStore %out %allEq\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 lhsId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 rhsId = module.m_globalInstructions[1].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[2].m_id;

	auto makeBool3 = [](bool x, bool y, bool z)
	{
		see::exec::Value value;
		value.m_scalar = see::ir::ScalarKind::Bool;
		value.m_rows = 3;
		value.SetBool(0, x);
		value.SetBool(1, y);
		value.SetBool(2, z);
		return value;
	};

	SECTION("all components equal")
	{
		std::unordered_map<cct::UInt32, see::exec::Value> inputs{{lhsId, makeBool3(true, false, true)}, {rhsId, makeBool3(true, false, true)}};
		std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
		REQUIRE(result.has_value());
		CHECK(result->find(outId)->second.GetBool());
	}

	SECTION("one component differs")
	{
		std::unordered_map<cct::UInt32, see::exec::Value> inputs{{lhsId, makeBool3(true, false, true)}, {rhsId, makeBool3(true, true, true)}};
		std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
		REQUIRE(result.has_value());
		CHECK_FALSE(result->find(outId)->second.GetBool());
	}
}

TEST_CASE("see::exec::Run - OpLogicalAnd/OpLogicalOr/OpLogicalNot on scalar bools", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%bool = OpTypeBool\n"
		"%_ptr_Private_bool = OpTypePointer Private %bool\n"
		"%a = OpVariable %_ptr_Private_bool Private\n"
		"%b = OpVariable %_ptr_Private_bool Private\n"
		"%outAnd = OpVariable %_ptr_Private_bool Private\n"
		"%outOr = OpVariable %_ptr_Private_bool Private\n"
		"%outNotA = OpVariable %_ptr_Private_bool Private\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%va = OpLoad %bool %a\n"
		"%vb = OpLoad %bool %b\n"
		"%andResult = OpLogicalAnd %bool %va %vb\n"
		"%orResult = OpLogicalOr %bool %va %vb\n"
		"%notA = OpLogicalNot %bool %va\n"
		"OpStore %outAnd %andResult\n"
		"OpStore %outOr %orResult\n"
		"OpStore %outNotA %notA\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 aId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 bId = module.m_globalInstructions[1].m_id;
	const cct::UInt32 outAndId = module.m_globalInstructions[2].m_id;
	const cct::UInt32 outOrId = module.m_globalInstructions[3].m_id;
	const cct::UInt32 outNotAId = module.m_globalInstructions[4].m_id;

	see::exec::Value trueValue;
	trueValue.m_scalar = see::ir::ScalarKind::Bool;
	trueValue.SetBool(0, true);

	see::exec::Value falseValue;
	falseValue.m_scalar = see::ir::ScalarKind::Bool;
	falseValue.SetBool(0, false);

	std::unordered_map<cct::UInt32, see::exec::Value> inputs{{aId, trueValue}, {bId, falseValue}};
	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
	REQUIRE(result.has_value());

	CHECK_FALSE(result->find(outAndId)->second.GetBool());
	CHECK(result->find(outOrId)->second.GetBool());
	CHECK_FALSE(result->find(outNotAId)->second.GetBool());
}

TEST_CASE("see::exec::Run - OpConstantComposite resolves a vector constant", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%v3float = OpTypeVector %float 3\n"
		"%_ptr_Private_v3float = OpTypePointer Private %v3float\n"
		"%out = OpVariable %_ptr_Private_v3float Private\n"
		"%c1 = OpConstant %float 1\n"
		"%c2 = OpConstant %float 2\n"
		"%c3 = OpConstant %float 3\n"
		"%vec = OpConstantComposite %v3float %c1 %c2 %c3\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"OpStore %out %vec\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 outId = module.m_globalInstructions[0].m_id;

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {});
	REQUIRE(result.has_value());

	const see::exec::Value& out = result->find(outId)->second;
	CHECK(out.GetFloat(0) == 1.0f);
	CHECK(out.GetFloat(1) == 2.0f);
	CHECK(out.GetFloat(2) == 3.0f);
}

TEST_CASE("see::exec::Run - OpINotEqual compares vector operands component-wise", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%uint = OpTypeInt 32 0\n"
		"%uint3 = OpTypeVector %uint 3\n"
		"%bool = OpTypeBool\n"
		"%bool3 = OpTypeVector %bool 3\n"
		"%_ptr_Private_uint3 = OpTypePointer Private %uint3\n"
		"%_ptr_Private_bool3 = OpTypePointer Private %bool3\n"
		"%a = OpVariable %_ptr_Private_uint3 Private\n"
		"%b = OpVariable %_ptr_Private_uint3 Private\n"
		"%out = OpVariable %_ptr_Private_bool3 Private\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%va = OpLoad %uint3 %a\n"
		"%vb = OpLoad %uint3 %b\n"
		"%r = OpINotEqual %bool3 %va %vb\n"
		"OpStore %out %r\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 aId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 bId = module.m_globalInstructions[1].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[2].m_id;

	see::exec::Value a;
	a.m_scalar = see::ir::ScalarKind::Int32;
	a.m_rows = 3;
	a.SetInt32(0, 5);
	a.SetInt32(1, 3);
	a.SetInt32(2, 3);

	see::exec::Value b;
	b.m_scalar = see::ir::ScalarKind::Int32;
	b.m_rows = 3;
	b.SetInt32(0, 5);
	b.SetInt32(1, 3);
	b.SetInt32(2, 7);

	std::unordered_map<cct::UInt32, see::exec::Value> inputs{{aId, a}, {bId, b}};
	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
	REQUIRE(result.has_value());

	const see::exec::Value& out = result->find(outId)->second;
	CHECK_FALSE(out.GetBool(0));
	CHECK_FALSE(out.GetBool(1));
	CHECK(out.GetBool(2));
}

TEST_CASE("see::exec::Run - OpAccessChain on a vector narrows to the scalar component, not the whole vector", "[see][exec]")
{
	// Regression test: a literal-index AccessChain into a vector used to leave the pointer's
	// pointee type as the vector type instead of the scalar component type, so the subsequent
	// Load over-read the vector's other words starting at the indexed offset. That went unnoticed
	// by a plain Load+Store round-trip (only word 0 of the over-read value is ever read back via
	// GetBool/GetFloat) but corrupted any CompositeConstruct built from several such components,
	// since the first (over-sized) argument alone would saturate the destination.
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%bool = OpTypeBool\n"
		"%bool3 = OpTypeVector %bool 3\n"
		"%_ptr_Private_bool3 = OpTypePointer Private %bool3\n"
		"%_ptr_Private_bool = OpTypePointer Private %bool\n"
		"%v = OpVariable %_ptr_Private_bool3 Private\n"
		"%out = OpVariable %_ptr_Private_bool3 Private\n"
		"%int = OpTypeInt 32 1\n"
		"%c0 = OpConstant %int 0\n"
		"%c1 = OpConstant %int 1\n"
		"%c2 = OpConstant %int 2\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%pz = OpAccessChain %_ptr_Private_bool %v %c2\n"
		"%z = OpLoad %bool %pz\n"
		"%px = OpAccessChain %_ptr_Private_bool %v %c0\n"
		"%x = OpLoad %bool %px\n"
		"%py = OpAccessChain %_ptr_Private_bool %v %c1\n"
		"%y = OpLoad %bool %py\n"
		"%permuted = OpCompositeConstruct %bool3 %z %x %y\n"
		"OpStore %out %permuted\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 vId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	see::exec::Value v;
	v.m_scalar = see::ir::ScalarKind::Bool;
	v.m_rows = 3;
	v.SetBool(0, true);
	v.SetBool(1, true);
	v.SetBool(2, false);

	std::unordered_map<cct::UInt32, see::exec::Value> inputs{{vId, v}};
	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
	REQUIRE(result.has_value());

	const see::exec::Value& out = result->find(outId)->second;
	CHECK_FALSE(out.GetBool(0)); // v.z
	CHECK(out.GetBool(1)); // v.x
	CHECK(out.GetBool(2)); // v.y
}

TEST_CASE("see::exec::Run - OpFRem/OpFMod sign conventions on negative operands", "[see][exec]")
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
		"%outRem = OpVariable %_ptr_Private_float Private\n"
		"%outMod = OpVariable %_ptr_Private_float Private\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%va = OpLoad %float %a\n"
		"%vb = OpLoad %float %b\n"
		"%rem = OpFRem %float %va %vb\n"
		"%mod = OpFMod %float %va %vb\n"
		"OpStore %outRem %rem\n"
		"OpStore %outMod %mod\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 aId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 bId = module.m_globalInstructions[1].m_id;
	const cct::UInt32 outRemId = module.m_globalInstructions[2].m_id;
	const cct::UInt32 outModId = module.m_globalInstructions[3].m_id;

	see::exec::Value a;
	a.SetFloat(0, -5.5f);
	see::exec::Value b;
	b.SetFloat(0, 2.0f);

	std::unordered_map<cct::UInt32, see::exec::Value> inputs{{aId, a}, {bId, b}};
	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
	REQUIRE(result.has_value());

	CHECK(result->find(outRemId)->second.GetFloat() == -1.5f); // sign of the dividend
	CHECK(result->find(outModId)->second.GetFloat() == 0.5f); // sign of the divisor
}

TEST_CASE("see::exec::Run - OpSRem/OpSMod/OpUMod integer remainder semantics", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%int = OpTypeInt 32 1\n"
		"%uint = OpTypeInt 32 0\n"
		"%_ptr_Private_int = OpTypePointer Private %int\n"
		"%_ptr_Private_uint = OpTypePointer Private %uint\n"
		"%a = OpVariable %_ptr_Private_int Private\n"
		"%b = OpVariable %_ptr_Private_int Private\n"
		"%ua = OpVariable %_ptr_Private_uint Private\n"
		"%ub = OpVariable %_ptr_Private_uint Private\n"
		"%outRem = OpVariable %_ptr_Private_int Private\n"
		"%outMod = OpVariable %_ptr_Private_int Private\n"
		"%outUMod = OpVariable %_ptr_Private_uint Private\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%va = OpLoad %int %a\n"
		"%vb = OpLoad %int %b\n"
		"%vua = OpLoad %uint %ua\n"
		"%vub = OpLoad %uint %ub\n"
		"%rem = OpSRem %int %va %vb\n"
		"%mod = OpSMod %int %va %vb\n"
		"%umod = OpUMod %uint %vua %vub\n"
		"OpStore %outRem %rem\n"
		"OpStore %outMod %mod\n"
		"OpStore %outUMod %umod\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 aId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 bId = module.m_globalInstructions[1].m_id;
	const cct::UInt32 uaId = module.m_globalInstructions[2].m_id;
	const cct::UInt32 ubId = module.m_globalInstructions[3].m_id;
	const cct::UInt32 outRemId = module.m_globalInstructions[4].m_id;
	const cct::UInt32 outModId = module.m_globalInstructions[5].m_id;
	const cct::UInt32 outUModId = module.m_globalInstructions[6].m_id;

	see::exec::Value a;
	a.m_scalar = see::ir::ScalarKind::Int32;
	a.SetInt32(0, -7);
	see::exec::Value b;
	b.m_scalar = see::ir::ScalarKind::Int32;
	b.SetInt32(0, 3);

	// 0xFFFFFFFE % 5 as unsigned is 4294967294 % 5 = 4; a signed interpretation (-2 % 5) would give -2 or 3.
	see::exec::Value ua;
	ua.m_scalar = see::ir::ScalarKind::Int32;
	ua.SetInt32(0, static_cast<cct::Int32>(0xFFFFFFFEu));
	see::exec::Value ub;
	ub.m_scalar = see::ir::ScalarKind::Int32;
	ub.SetInt32(0, 5);

	std::unordered_map<cct::UInt32, see::exec::Value> inputs{{aId, a}, {bId, b}, {uaId, ua}, {ubId, ub}};
	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
	REQUIRE(result.has_value());

	CHECK(result->find(outRemId)->second.GetInt32() == -1); // sign of the dividend
	CHECK(result->find(outModId)->second.GetInt32() == 2); // sign of the divisor
	CHECK(result->find(outUModId)->second.GetInt32() == 4);
}

TEST_CASE("see::exec::Run - OpFNegate/OpSNegate and int<->float conversions", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%int = OpTypeInt 32 1\n"
		"%uint = OpTypeInt 32 0\n"
		"%_ptr_Private_float = OpTypePointer Private %float\n"
		"%_ptr_Private_int = OpTypePointer Private %int\n"
		"%_ptr_Private_uint = OpTypePointer Private %uint\n"
		"%f = OpVariable %_ptr_Private_float Private\n"
		"%i = OpVariable %_ptr_Private_int Private\n"
		"%outNegF = OpVariable %_ptr_Private_float Private\n"
		"%outNegI = OpVariable %_ptr_Private_int Private\n"
		"%outSToF = OpVariable %_ptr_Private_float Private\n"
		"%outUToF = OpVariable %_ptr_Private_float Private\n"
		"%outFToS = OpVariable %_ptr_Private_int Private\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%vf = OpLoad %float %f\n"
		"%vi = OpLoad %int %i\n"
		"%negf = OpFNegate %float %vf\n"
		"%negi = OpSNegate %int %vi\n"
		"%stof = OpConvertSToF %float %vi\n"
		"%utof = OpConvertUToF %float %vi\n"
		"%ftos = OpConvertFToS %int %vf\n"
		"OpStore %outNegF %negf\n"
		"OpStore %outNegI %negi\n"
		"OpStore %outSToF %stof\n"
		"OpStore %outUToF %utof\n"
		"OpStore %outFToS %ftos\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 fId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 iId = module.m_globalInstructions[1].m_id;
	const cct::UInt32 outNegFId = module.m_globalInstructions[2].m_id;
	const cct::UInt32 outNegIId = module.m_globalInstructions[3].m_id;
	const cct::UInt32 outSToFId = module.m_globalInstructions[4].m_id;
	const cct::UInt32 outUToFId = module.m_globalInstructions[5].m_id;
	const cct::UInt32 outFToSId = module.m_globalInstructions[6].m_id;

	see::exec::Value f;
	f.SetFloat(0, 2.75f);
	see::exec::Value i;
	i.m_scalar = see::ir::ScalarKind::Int32;
	i.SetInt32(0, -3);

	std::unordered_map<cct::UInt32, see::exec::Value> inputs{{fId, f}, {iId, i}};
	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
	REQUIRE(result.has_value());

	CHECK(result->find(outNegFId)->second.GetFloat() == -2.75f);
	CHECK(result->find(outNegIId)->second.GetInt32() == 3);
	CHECK(result->find(outSToFId)->second.GetFloat() == -3.0f);
	CHECK(result->find(outUToFId)->second.GetFloat() == 4294967296.0f); // 0xFFFFFFFD as unsigned, rounded to float
	CHECK(result->find(outFToSId)->second.GetInt32() == 2); // truncation toward zero
}

TEST_CASE("see::exec::Run - GLSL.std.450 FAbs/FClamp/FMix and vector Normalize", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"%glsl = OpExtInstImport \"GLSL.std.450\"\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%float3 = OpTypeVector %float 3\n"
		"%_ptr_Private_float = OpTypePointer Private %float\n"
		"%_ptr_Private_float3 = OpTypePointer Private %float3\n"
		"%x = OpVariable %_ptr_Private_float Private\n"
		"%v = OpVariable %_ptr_Private_float3 Private\n"
		"%outAbs = OpVariable %_ptr_Private_float Private\n"
		"%outClamp = OpVariable %_ptr_Private_float Private\n"
		"%outMix = OpVariable %_ptr_Private_float Private\n"
		"%outNorm = OpVariable %_ptr_Private_float3 Private\n"
		"%zero = OpConstant %float 0\n"
		"%one = OpConstant %float 1\n"
		"%ten = OpConstant %float 10\n"
		"%quarter = OpConstant %float 0.25\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%vx = OpLoad %float %x\n"
		"%vv = OpLoad %float3 %v\n"
		"%abs = OpExtInst %float %glsl FAbs %vx\n"
		"%clamp = OpExtInst %float %glsl FClamp %vx %zero %one\n"
		"%mix = OpExtInst %float %glsl FMix %zero %ten %quarter\n"
		"%norm = OpExtInst %float3 %glsl Normalize %vv\n"
		"OpStore %outAbs %abs\n"
		"OpStore %outClamp %clamp\n"
		"OpStore %outMix %mix\n"
		"OpStore %outNorm %norm\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 xId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 vId = module.m_globalInstructions[1].m_id;
	const cct::UInt32 outAbsId = module.m_globalInstructions[2].m_id;
	const cct::UInt32 outClampId = module.m_globalInstructions[3].m_id;
	const cct::UInt32 outMixId = module.m_globalInstructions[4].m_id;
	const cct::UInt32 outNormId = module.m_globalInstructions[5].m_id;

	see::exec::Value x;
	x.SetFloat(0, -2.5f);

	see::exec::Value v;
	v.m_rows = 3;
	v.SetFloat(0, 3.0f);
	v.SetFloat(1, 0.0f);
	v.SetFloat(2, 4.0f);

	std::unordered_map<cct::UInt32, see::exec::Value> inputs{{xId, x}, {vId, v}};
	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
	REQUIRE(result.has_value());

	CHECK(result->find(outAbsId)->second.GetFloat() == 2.5f);
	CHECK(result->find(outClampId)->second.GetFloat() == 0.0f);
	CHECK(result->find(outMixId)->second.GetFloat() == 2.5f);
	const see::exec::Value& norm = result->find(outNormId)->second;
	CHECK(norm.GetFloat(0) == 0.6f);
	CHECK(norm.GetFloat(1) == 0.0f);
	CHECK(norm.GetFloat(2) == 0.8f);
}

TEST_CASE("see::exec::Run - OpVectorTimesScalar and OpFUnordNotEqual", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%float3 = OpTypeVector %float 3\n"
		"%bool = OpTypeBool\n"
		"%_ptr_Private_float3 = OpTypePointer Private %float3\n"
		"%_ptr_Private_float = OpTypePointer Private %float\n"
		"%_ptr_Private_bool = OpTypePointer Private %bool\n"
		"%v = OpVariable %_ptr_Private_float3 Private\n"
		"%s = OpVariable %_ptr_Private_float Private\n"
		"%outScaled = OpVariable %_ptr_Private_float3 Private\n"
		"%outCompare = OpVariable %_ptr_Private_bool Private\n"
		"%two = OpConstant %float 2\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%vv = OpLoad %float3 %v\n"
		"%vs = OpLoad %float %s\n"
		"%scaled = OpVectorTimesScalar %float3 %vv %vs\n"
		"%cmp = OpFUnordNotEqual %bool %vs %two\n"
		"OpStore %outScaled %scaled\n"
		"OpStore %outCompare %cmp\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 vId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 sId = module.m_globalInstructions[1].m_id;
	const cct::UInt32 outScaledId = module.m_globalInstructions[2].m_id;
	const cct::UInt32 outCompareId = module.m_globalInstructions[3].m_id;

	see::exec::Value v;
	v.m_rows = 3;
	v.SetFloat(0, 1.0f);
	v.SetFloat(1, -2.0f);
	v.SetFloat(2, 0.5f);

	see::exec::Value s;
	s.SetFloat(0, 3.0f);

	std::unordered_map<cct::UInt32, see::exec::Value> inputs{{vId, v}, {sId, s}};
	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
	REQUIRE(result.has_value());

	const see::exec::Value& scaled = result->find(outScaledId)->second;
	CHECK(scaled.GetFloat(0) == 3.0f);
	CHECK(scaled.GetFloat(1) == -6.0f);
	CHECK(scaled.GetFloat(2) == 1.5f);
	CHECK(result->find(outCompareId)->second.GetBool()); // 3.0 != 2.0
}

TEST_CASE("see::exec::Run - OpSelect with a vector condition selects component-wise", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%bool = OpTypeBool\n"
		"%bool3 = OpTypeVector %bool 3\n"
		"%uint = OpTypeInt 32 0\n"
		"%uint3 = OpTypeVector %uint 3\n"
		"%_ptr_Private_bool3 = OpTypePointer Private %bool3\n"
		"%_ptr_Private_uint3 = OpTypePointer Private %uint3\n"
		"%cond = OpVariable %_ptr_Private_bool3 Private\n"
		"%out = OpVariable %_ptr_Private_uint3 Private\n"
		"%zero = OpConstant %uint 0\n"
		"%one = OpConstant %uint 1\n"
		"%ones = OpConstantComposite %uint3 %one %one %one\n"
		"%zeros = OpConstantComposite %uint3 %zero %zero %zero\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%c = OpLoad %bool3 %cond\n"
		"%sel = OpSelect %uint3 %c %ones %zeros\n"
		"OpStore %out %sel\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 condId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	see::exec::Value cond;
	cond.m_scalar = see::ir::ScalarKind::Bool;
	cond.m_rows = 3;
	cond.SetBool(0, true);
	cond.SetBool(1, false);
	cond.SetBool(2, true);

	std::unordered_map<cct::UInt32, see::exec::Value> inputs{{condId, cond}};
	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
	REQUIRE(result.has_value());

	const see::exec::Value& out = result->find(outId)->second;
	CHECK(out.GetInt32(0) == 1);
	CHECK(out.GetInt32(1) == 0);
	CHECK(out.GetInt32(2) == 1);
}

TEST_CASE("see::exec::Run - OpAccessChain indexes a matrix by column then component", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%float3 = OpTypeVector %float 3\n"
		"%mat3 = OpTypeMatrix %float3 3\n"
		"%_ptr_Private_mat3 = OpTypePointer Private %mat3\n"
		"%_ptr_Private_float = OpTypePointer Private %float\n"
		"%m = OpVariable %_ptr_Private_mat3 Private\n"
		"%out = OpVariable %_ptr_Private_float Private\n"
		"%int = OpTypeInt 32 1\n"
		"%c2 = OpConstant %int 2\n"
		"%c0 = OpConstant %int 0\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%p = OpAccessChain %_ptr_Private_float %m %c2 %c0\n"
		"%v = OpLoad %float %p\n"
		"OpStore %out %v\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 mId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	see::exec::Value m;
	m.m_rows = 3;
	m.m_columns = 3;
	for (cct::UInt32 i = 0; i < 9; ++i)
		m.SetFloat(i, static_cast<float>(i));

	std::unordered_map<cct::UInt32, see::exec::Value> inputs{{mId, m}};
	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], inputs);
	REQUIRE(result.has_value());

	// Column 2, component 0 = word 2*3+0 = 6
	CHECK(result->find(outId)->second.GetFloat() == 6.0f);
}

TEST_CASE("see::exec::Run - OpVectorShuffle swizzle from a single vector", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%v4float = OpTypeVector %float 4\n"
		"%_ptr_Input_v4float = OpTypePointer Input %v4float\n"
		"%_ptr_Output_v4float = OpTypePointer Output %v4float\n"
		"%in_var = OpVariable %_ptr_Input_v4float Input\n"
		"%out_var = OpVariable %_ptr_Output_v4float Output\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%a = OpLoad %v4float %in_var\n"
		"%r = OpVectorShuffle %v4float %a %a 3 2 1 0\n"
		"OpStore %out_var %r\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 inId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	see::exec::Value input;
	input.m_rows = 4;
	input.SetFloat(0, 10.0f);
	input.SetFloat(1, 20.0f);
	input.SetFloat(2, 30.0f);
	input.SetFloat(3, 40.0f);

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {{inId, input}});
	REQUIRE(result.has_value());

	auto outIt = result->find(outId);
	REQUIRE(outIt != result->end());
	CHECK(outIt->second.m_rows == 4);
	CHECK(outIt->second.GetFloat(0) == 40.0f);
	CHECK(outIt->second.GetFloat(1) == 30.0f);
	CHECK(outIt->second.GetFloat(2) == 20.0f);
	CHECK(outIt->second.GetFloat(3) == 10.0f);
}

TEST_CASE("see::exec::Run - OpVectorShuffle across operands of different lengths", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%v2float = OpTypeVector %float 2\n"
		"%v4float = OpTypeVector %float 4\n"
		"%_ptr_Input_v4float = OpTypePointer Input %v4float\n"
		"%_ptr_Output_v4float = OpTypePointer Output %v4float\n"
		"%in_var = OpVariable %_ptr_Input_v4float Input\n"
		"%out_var = OpVariable %_ptr_Output_v4float Output\n"
		"%float_1 = OpConstant %float 1\n"
		"%float_2 = OpConstant %float 2\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%a = OpLoad %v4float %in_var\n"
		"%b = OpCompositeConstruct %v2float %float_1 %float_2\n"
		"%r = OpVectorShuffle %v4float %a %b 2 5 0 4294967295\n"
		"OpStore %out_var %r\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 inId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	see::exec::Value input;
	input.m_rows = 4;
	input.SetFloat(0, 10.0f);
	input.SetFloat(1, 20.0f);
	input.SetFloat(2, 30.0f);
	input.SetFloat(3, 40.0f);

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {{inId, input}});
	REQUIRE(result.has_value());

	auto outIt = result->find(outId);
	REQUIRE(outIt != result->end());
	CHECK(outIt->second.GetFloat(0) == 30.0f);
	CHECK(outIt->second.GetFloat(1) == 2.0f);
	CHECK(outIt->second.GetFloat(2) == 10.0f);
	CHECK(outIt->second.GetFloat(3) == 0.0f);
}

TEST_CASE("see::exec::Run - bitwise and shift opcodes", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%int = OpTypeInt 32 1\n"
		"%v4int = OpTypeVector %int 4\n"
		"%_ptr_Input_int = OpTypePointer Input %int\n"
		"%_ptr_Output_v4int = OpTypePointer Output %v4int\n"
		"%in_var = OpVariable %_ptr_Input_int Input\n"
		"%out_var = OpVariable %_ptr_Output_v4int Output\n"
		"%int_3 = OpConstant %int 3\n"
		"%int_5 = OpConstant %int 5\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%a = OpLoad %int %in_var\n"
		"%and = OpBitwiseAnd %int %a %int_5\n"
		"%xor = OpBitwiseXor %int %a %int_5\n"
		"%shl = OpShiftLeftLogical %int %int_3 %int_5\n"
		"%not = OpNot %int %a\n"
		"%r = OpCompositeConstruct %v4int %and %xor %shl %not\n"
		"OpStore %out_var %r\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 inId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	see::exec::Value input;
	input.m_scalar = see::ir::ScalarKind::Int32;
	input.SetInt32(0, 12);

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {{inId, input}});
	REQUIRE(result.has_value());

	auto outIt = result->find(outId);
	REQUIRE(outIt != result->end());
	CHECK(outIt->second.GetInt32(0) == (12 & 5));
	CHECK(outIt->second.GetInt32(1) == (12 ^ 5));
	CHECK(outIt->second.GetInt32(2) == (3 << 5));
	CHECK(outIt->second.GetInt32(3) == ~12);
}

TEST_CASE("see::exec::Run - unsigned division and comparison do not use signed semantics", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%uint = OpTypeInt 32 0\n"
		"%bool = OpTypeBool\n"
		"%v2uint = OpTypeVector %uint 2\n"
		"%_ptr_Input_uint = OpTypePointer Input %uint\n"
		"%_ptr_Output_v2uint = OpTypePointer Output %v2uint\n"
		"%in_var = OpVariable %_ptr_Input_uint Input\n"
		"%out_var = OpVariable %_ptr_Output_v2uint Output\n"
		"%uint_2 = OpConstant %uint 2\n"
		"%uint_1 = OpConstant %uint 1\n"
		"%uint_0 = OpConstant %uint 0\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%a = OpLoad %uint %in_var\n"
		"%div = OpUDiv %uint %a %uint_2\n"
		"%lt = OpULessThan %bool %a %uint_1\n"
		"%ltVal = OpSelect %uint %lt %uint_1 %uint_0\n"
		"%r = OpCompositeConstruct %v2uint %div %ltVal\n"
		"OpStore %out_var %r\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 inId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	see::exec::Value input;
	input.m_scalar = see::ir::ScalarKind::Int32;
	input.SetInt32(0, static_cast<cct::Int32>(0xFFFFFFFEu));

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {{inId, input}});
	REQUIRE(result.has_value());

	auto outIt = result->find(outId);
	REQUIRE(outIt != result->end());
	CHECK(static_cast<cct::UInt32>(outIt->second.GetInt32(0)) == 0x7FFFFFFFu);
	CHECK(outIt->second.GetInt32(1) == 0);
}

TEST_CASE("see::exec::Run - OpBitCount, OpBitReverse and bitfield extraction", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%int = OpTypeInt 32 1\n"
		"%v4int = OpTypeVector %int 4\n"
		"%_ptr_Input_int = OpTypePointer Input %int\n"
		"%_ptr_Output_v4int = OpTypePointer Output %v4int\n"
		"%in_var = OpVariable %_ptr_Input_int Input\n"
		"%out_var = OpVariable %_ptr_Output_v4int Output\n"
		"%int_4 = OpConstant %int 4\n"
		"%int_8 = OpConstant %int 8\n"
		"%int_255 = OpConstant %int 255\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%a = OpLoad %int %in_var\n"
		"%count = OpBitCount %int %a\n"
		"%rev = OpBitReverse %int %a\n"
		"%uext = OpBitFieldUExtract %int %a %int_4 %int_8\n"
		"%ins = OpBitFieldInsert %int %a %int_255 %int_4 %int_8\n"
		"%r = OpCompositeConstruct %v4int %count %rev %uext %ins\n"
		"OpStore %out_var %r\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 inId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	see::exec::Value input;
	input.m_scalar = see::ir::ScalarKind::Int32;
	input.SetInt32(0, static_cast<cct::Int32>(0x0000ABCDu));

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {{inId, input}});
	REQUIRE(result.has_value());

	auto outIt = result->find(outId);
	REQUIRE(outIt != result->end());
	CHECK(outIt->second.GetInt32(0) == 10);
	CHECK(static_cast<cct::UInt32>(outIt->second.GetInt32(1)) == 0xB3D50000u);
	CHECK(static_cast<cct::UInt32>(outIt->second.GetInt32(2)) == 0xBCu);
	CHECK(static_cast<cct::UInt32>(outIt->second.GetInt32(3)) == 0x0000AFFDu);
}

TEST_CASE("see::exec::Run - OpBitFieldSExtract sign-extends the extracted field", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%int = OpTypeInt 32 1\n"
		"%_ptr_Input_int = OpTypePointer Input %int\n"
		"%_ptr_Output_int = OpTypePointer Output %int\n"
		"%in_var = OpVariable %_ptr_Input_int Input\n"
		"%out_var = OpVariable %_ptr_Output_int Output\n"
		"%int_4 = OpConstant %int 4\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%a = OpLoad %int %in_var\n"
		"%r = OpBitFieldSExtract %int %a %int_4 %int_4\n"
		"OpStore %out_var %r\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 inId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	see::exec::Value input;
	input.m_scalar = see::ir::ScalarKind::Int32;
	input.SetInt32(0, static_cast<cct::Int32>(0x000000F0u));

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {{inId, input}});
	REQUIRE(result.has_value());

	CHECK(result->find(outId)->second.GetInt32() == -1);
}

TEST_CASE("see::exec::Run - OpBitcast reinterprets the bits without converting", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%int = OpTypeInt 32 1\n"
		"%float = OpTypeFloat 32\n"
		"%_ptr_Input_float = OpTypePointer Input %float\n"
		"%_ptr_Output_int = OpTypePointer Output %int\n"
		"%in_var = OpVariable %_ptr_Input_float Input\n"
		"%out_var = OpVariable %_ptr_Output_int Output\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%a = OpLoad %float %in_var\n"
		"%r = OpBitcast %int %a\n"
		"OpStore %out_var %r\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 inId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	see::exec::Value input;
	input.SetFloat(0, 1.0f);

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {{inId, input}});
	REQUIRE(result.has_value());

	auto outIt = result->find(outId);
	REQUIRE(outIt != result->end());
	CHECK(outIt->second.m_scalar == see::ir::ScalarKind::Int32);
	CHECK(static_cast<cct::UInt32>(outIt->second.GetInt32()) == 0x3F800000u);
}

TEST_CASE("see::exec::Run - OpCompositeExtract pulls a whole column out of a matrix", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%v3float = OpTypeVector %float 3\n"
		"%mat3 = OpTypeMatrix %v3float 3\n"
		"%_ptr_Input_mat3 = OpTypePointer Input %mat3\n"
		"%_ptr_Output_v3float = OpTypePointer Output %v3float\n"
		"%in_var = OpVariable %_ptr_Input_mat3 Input\n"
		"%out_var = OpVariable %_ptr_Output_v3float Output\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%m = OpLoad %mat3 %in_var\n"
		"%col = OpCompositeExtract %v3float %m 2\n"
		"OpStore %out_var %col\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 inId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	see::exec::Value input;
	input.m_rows = 3;
	input.m_columns = 3;
	for (cct::UInt32 i = 0; i < 9; ++i)
		input.SetFloat(i, static_cast<float>(i));

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {{inId, input}});
	REQUIRE(result.has_value());

	auto outIt = result->find(outId);
	REQUIRE(outIt != result->end());
	CHECK(outIt->second.m_rows == 3);
	CHECK(outIt->second.m_columns == 1);
	CHECK(outIt->second.GetFloat(0) == 6.0f);
	CHECK(outIt->second.GetFloat(1) == 7.0f);
	CHECK(outIt->second.GetFloat(2) == 8.0f);
}

TEST_CASE("see::exec::Run - OpCompositeExtract walks a column then a component", "[see][exec]")
{
	static constexpr const char* Source =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\"\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"%float = OpTypeFloat 32\n"
		"%v3float = OpTypeVector %float 3\n"
		"%mat3 = OpTypeMatrix %v3float 3\n"
		"%_ptr_Input_mat3 = OpTypePointer Input %mat3\n"
		"%_ptr_Output_float = OpTypePointer Output %float\n"
		"%in_var = OpVariable %_ptr_Input_mat3 Input\n"
		"%out_var = OpVariable %_ptr_Output_float Output\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%m = OpLoad %mat3 %in_var\n"
		"%c = OpCompositeExtract %float %m 1 2\n"
		"OpStore %out_var %c\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	const see::ir::Module module = ParseAndConvert(Source);
	const cct::UInt32 inId = module.m_globalInstructions[0].m_id;
	const cct::UInt32 outId = module.m_globalInstructions[1].m_id;

	see::exec::Value input;
	input.m_rows = 3;
	input.m_columns = 3;
	for (cct::UInt32 i = 0; i < 9; ++i)
		input.SetFloat(i, static_cast<float>(i));

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> result = see::exec::Run(module, module.m_functions[0], {{inId, input}});
	REQUIRE(result.has_value());

	CHECK(result->find(outId)->second.GetFloat() == 5.0f);
}
