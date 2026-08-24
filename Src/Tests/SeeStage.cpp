/**
 * @file SeeStage.cpp
 * @brief Unit tests for the vertex/fragment stage wrappers (see::exec::RunVertexStage/RunFragmentStage)
 * @date 2026-08-24
 */

#include <cstring>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <See/Exec/Stage.hpp>
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

	static constexpr const char* VertexSource =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint Vertex %main \"main\" %vidx %outPos %outColor\n"
		"OpDecorate %vidx BuiltIn VertexIndex\n"
		"OpDecorate %outPos BuiltIn Position\n"
		"OpDecorate %outColor Location 0\n"
		"%int = OpTypeInt 32 1\n"
		"%bool = OpTypeBool\n"
		"%float = OpTypeFloat 32\n"
		"%v4float = OpTypeVector %float 4\n"
		"%v3float = OpTypeVector %float 3\n"
		"%_ptr_Input_int = OpTypePointer Input %int\n"
		"%_ptr_Output_v4float = OpTypePointer Output %v4float\n"
		"%_ptr_Output_v3float = OpTypePointer Output %v3float\n"
		"%vidx = OpVariable %_ptr_Input_int Input\n"
		"%outPos = OpVariable %_ptr_Output_v4float Output\n"
		"%outColor = OpVariable %_ptr_Output_v3float Output\n"
		"%izero = OpConstant %int 0\n"
		"%c0 = OpConstant %float 0\n"
		"%c1 = OpConstant %float 1\n"
		"%cNeg1 = OpConstant %float -1\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%idx = OpLoad %int %vidx\n"
		"%isZero = OpIEqual %bool %idx %izero\n"
		"%posA = OpCompositeConstruct %v4float %c1 %c0 %c0 %c1\n"
		"%posB = OpCompositeConstruct %v4float %cNeg1 %c0 %c0 %c1\n"
		"%pos = OpSelect %v4float %isZero %posA %posB\n"
		"OpStore %outPos %pos\n"
		"%color = OpCompositeConstruct %v3float %c1 %c0 %c0\n"
		"OpStore %outColor %color\n"
		"OpReturn\n"
		"OpFunctionEnd\n";

	static constexpr const char* FragmentSource =
		"OpCapability Shader\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint Fragment %main \"main\" %inColor %outColor\n"
		"OpExecutionMode %main OriginUpperLeft\n"
		"OpDecorate %inColor Location 0\n"
		"OpDecorate %outColor Location 0\n"
		"%float = OpTypeFloat 32\n"
		"%v3float = OpTypeVector %float 3\n"
		"%v4float = OpTypeVector %float 4\n"
		"%_ptr_Input_v3float = OpTypePointer Input %v3float\n"
		"%_ptr_Output_v4float = OpTypePointer Output %v4float\n"
		"%inColor = OpVariable %_ptr_Input_v3float Input\n"
		"%outColor = OpVariable %_ptr_Output_v4float Output\n"
		"%one = OpConstant %float 1\n"
		"%void = OpTypeVoid\n"
		"%voidfn = OpTypeFunction %void\n"
		"%main = OpFunction %void None %voidfn\n"
		"%entry = OpLabel\n"
		"%c = OpLoad %v3float %inColor\n"
		"%r = OpCompositeExtract %float %c 0\n"
		"%g = OpCompositeExtract %float %c 1\n"
		"%b = OpCompositeExtract %float %c 2\n"
		"%rgba = OpCompositeConstruct %v4float %r %g %b %one\n"
		"OpStore %outColor %rgba\n"
		"OpReturn\n"
		"OpFunctionEnd\n";
} // namespace

TEST_CASE("see::exec::RunVertexStage - finds BuiltIn/Location variables via decorations", "[see][stage]")
{
	const see::ir::Module module = ParseAndConvert(VertexSource);
	REQUIRE(module.m_functions.size() == 1);

	SECTION("vertex 0 takes the positive-x branch")
	{
		std::optional<see::exec::VertexStageOutput> output = see::exec::RunVertexStage(module, module.m_functions[0], 0);
		REQUIRE(output.has_value());
		CHECK(output->m_position.GetFloat(0) == 1.0f);

		auto colorIt = output->m_locations.find(0);
		REQUIRE(colorIt != output->m_locations.end());
		CHECK(colorIt->second.GetFloat(0) == 1.0f);
	}

	SECTION("vertex 1 takes the negative-x branch")
	{
		std::optional<see::exec::VertexStageOutput> output = see::exec::RunVertexStage(module, module.m_functions[0], 1);
		REQUIRE(output.has_value());
		CHECK(output->m_position.GetFloat(0) == -1.0f);
	}
}

TEST_CASE("see::exec::RunFragmentStage - seeds Location inputs and reads Location outputs", "[see][stage]")
{
	const see::ir::Module module = ParseAndConvert(FragmentSource);
	REQUIRE(module.m_functions.size() == 1);

	see::exec::Value color;
	color.m_rows = 3;
	color.SetFloat(0, 0.2f);
	color.SetFloat(1, 0.4f);
	color.SetFloat(2, 0.6f);

	std::optional<std::unordered_map<cct::UInt32, see::exec::Value>> outputs = see::exec::RunFragmentStage(module, module.m_functions[0], {{0, color}});
	REQUIRE(outputs.has_value());

	auto colorIt = outputs->find(0);
	REQUIRE(colorIt != outputs->end());
	CHECK(colorIt->second.GetFloat(0) == 0.2f);
	CHECK(colorIt->second.GetFloat(1) == 0.4f);
	CHECK(colorIt->second.GetFloat(2) == 0.6f);
	CHECK(colorIt->second.GetFloat(3) == 1.0f);
}
