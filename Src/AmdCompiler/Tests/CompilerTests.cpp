#include <cstring>
#include <fstream>
#include <iterator>

#include "AmdCompiler/Compiler.hpp"
#include "AmdCompiler/Tests/TestShaders.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace amdcomp;

TEST_CASE("buffer-address fill shader compiles", "[amd-compiler][compute]")
{
	std::string error;
	std::string ir;
	const std::optional<ComputeShader> shader = CompileComputeShader(tests::kFillBufferAddressSpirv, CompileOptions{.llvmIr = &ir}, error);
	INFO(error);
	INFO(ir);
	REQUIRE(shader.has_value());
	UNSCOPED_INFO(Disassemble(shader->code, true));

	CHECK(shader->numThreadX == 64);
	CHECK(shader->numThreadY == 1);
	CHECK(shader->numThreadZ == 1);
	CHECK(shader->pushConstantUserData == 1);
	CHECK(shader->pushConstantDwords == 4);
	CHECK(((shader->pgmRsrc2 >> 1) & 0x1F) == 5);
	CHECK((shader->pgmRsrc2 & 0x780) == 0x780);
	CHECK((shader->dispatchInitiator & 0x8000) == 0);
	CHECK(shader->code.size() > 8);

	const std::string assembly = Disassemble(shader->code, true);
	CHECK(assembly.find("global_store_b32") != std::string::npos);
	CHECK(assembly.find("s_endpgm") != std::string::npos);
}

TEST_CASE("storage buffer fill shader compiles", "[amd-compiler][compute]")
{
	std::string error;
	std::string ir;
	const std::optional<ComputeShader> shader = CompileComputeShader(tests::kFillDescriptorSpirv, CompileOptions{.llvmIr = &ir}, error);
	INFO(error);
	INFO(ir);
	REQUIRE(shader.has_value());
	const std::string assembly = Disassemble(shader->code, true);
	INFO(assembly);

	CHECK(shader->pushConstantDwords == 2);
	CHECK(shader->setTableUserData[0] == 3);
	CHECK(((shader->pgmRsrc2 >> 1) & 0x1F) == 4);
	REQUIRE(shader->descriptors.size() == 1);
	CHECK(shader->descriptors[0].set == 0);
	CHECK(shader->descriptors[0].binding == 0);
	CHECK(shader->descriptors[0].kind == DescriptorKind::StorageBuffer);
	CHECK(shader->descriptors[0].offset == 0);
	CHECK(shader->descriptors[0].count == 1);
	CHECK(assembly.find("s_getpc_b64") != std::string::npos);
	CHECK(assembly.find("buffer_store_b32") != std::string::npos);
}

TEST_CASE("print the compiled test shaders", "[.][amd-compiler][dump]")
{
	for (const std::span<const uint32_t> spirv : {std::span<const uint32_t>(tests::kFillBufferAddressSpirv), std::span<const uint32_t>(tests::kFillDescriptorSpirv)})
	{
		std::string error;
		const std::optional<ComputeShader> shader = CompileComputeShader(spirv, CompileOptions{}, error);
		REQUIRE(shader.has_value());
		WARN("RSRC1 " << std::hex << shader->pgmRsrc1 << " RSRC2 " << shader->pgmRsrc2 << " RSRC3 " << shader->pgmRsrc3 << " initiator " << shader->dispatchInitiator);
		WARN(Disassemble(shader->code, true));
	}
}

TEST_CASE("compile the SPIR-V file in AMDCOMP_SPIRV", "[.][amd-compiler][file]")
{
	char path[512] = {};
	size_t length = 0;
	REQUIRE((getenv_s(&length, path, sizeof(path), "AMDCOMP_SPIRV") == 0 && length > 1));
	std::ifstream file(path, std::ios::binary);
	REQUIRE(file);
	const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	std::vector<uint32_t> spirv(bytes.size() / 4);
	std::memcpy(spirv.data(), bytes.data(), spirv.size() * 4);

	std::string error;
	std::string ir;
	std::string optimized;
	const std::optional<ComputeShader> shader = CompileComputeShader(spirv, CompileOptions{.llvmIr = &ir, .optimizedLlvmIr = &optimized}, error);
	WARN(ir);
	WARN(optimized);
	WARN(error);
	if (shader)
		WARN(Disassemble(shader->code, true));
}

TEST_CASE("disassemble the code file in AMDCOMP_CODE", "[.][amd-compiler][disassemble]")
{
	char path[512] = {};
	size_t length = 0;
	REQUIRE((getenv_s(&length, path, sizeof(path), "AMDCOMP_CODE") == 0 && length > 1));
	std::ifstream file(path, std::ios::binary);
	REQUIRE(file);
	const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	std::vector<uint32_t> code(bytes.size() / 4);
	std::memcpy(code.data(), bytes.data(), code.size() * 4);
	WARN(Disassemble(code, true));
}

TEST_CASE("vkd-test-app's pipeline compiles", "[amd-compiler][graphics]")
{
	std::string error;
	const std::optional<GraphicsShaders> shaders = CompileGraphicsShaders(tests::kTestAppVertSpirv, tests::kTestAppFragSpirv, GraphicsOptions{}, error);
	INFO(error);
	REQUIRE(shaders.has_value());
	CHECK(shaders->vsOutConfig == 2);
	CHECK(shaders->numInterpolants == 2);
	REQUIRE(shaders->psInputCntl.size() == 2);
	CHECK(shaders->psInputCntl[0] == 0);
	CHECK(shaders->psInputCntl[1] == 1);
	CHECK(shaders->colorFormat == 4);
	CHECK(shaders->shaderMask == 0xF);
	CHECK(shaders->fragmentSetTableUserData[0] == 1);
	REQUIRE(shaders->descriptors.size() == 2);
	CHECK(shaders->descriptors[0].kind == DescriptorKind::UniformBuffer);
	CHECK(shaders->descriptors[0].offset == 0);
	CHECK(shaders->descriptors[1].kind == DescriptorKind::CombinedImageSampler);
	CHECK(shaders->descriptors[1].offset == 16);
	CHECK(Disassemble(shaders->vertex.code, true).find("exp pos0") != std::string::npos);
	CHECK(Disassemble(shaders->fragment.code, true).find("image_sample") != std::string::npos);
}

TEST_CASE("vertex buffers compile", "[amd-compiler][graphics]")
{
	const VertexAttribute attributes[] = {
		{.location = 0, .binding = 0, .offset = 0, .format = 0x32, .channels = 2, .swizzle = {0, 1, kVertexSwizzleZero, kVertexSwizzleOne}},
		{.location = 1, .binding = 0, .offset = 8, .format = 0x3C, .channels = 3, .swizzle = {0, 1, 2, kVertexSwizzleOne}},
		{.location = 2, .binding = 1, .offset = 0, .format = 0x32, .channels = 2, .swizzle = {0, 1, kVertexSwizzleZero, kVertexSwizzleOne}, .perInstance = true},
	};
	std::string error;
	const std::optional<GraphicsShaders> shaders =
		CompileGraphicsShaders(tests::kVertexBuffersVertSpirv, tests::kPushConstantFragSpirv, GraphicsOptions{.vertexAttributes = attributes}, error);
	INFO(error);
	REQUIRE(shaders.has_value());
	CHECK(shaders->vertexBufferTableUserData == 1);
	CHECK(shaders->baseVertexUserData == 2);
	CHECK(shaders->baseInstanceUserData == 3);
	CHECK(shaders->instanceIndex);
	CHECK(((shaders->vertex.pgmRsrc2 >> 1) & 0x1F) == 4);
	const std::string code = Disassemble(shaders->vertex.code, true);
	CHECK(code.find("format:[BUF_FMT_32_32_32_FLOAT] idxen offset:8") != std::string::npos);
	CHECK(code.find("format:[BUF_FMT_32_32_FLOAT] idxen") != std::string::npos);
}

TEST_CASE("vertex shader descriptors, push constants and matrices compile", "[amd-compiler][graphics]")
{
	const VertexAttribute attributes[] = {
		{.location = 0, .binding = 0, .offset = 0, .format = 0x32, .channels = 2, .swizzle = {0, 1, kVertexSwizzleZero, kVertexSwizzleOne}},
		{.location = 1, .binding = 0, .offset = 8, .format = 0x32, .channels = 2, .swizzle = {0, 1, kVertexSwizzleZero, kVertexSwizzleOne}},
		{.location = 2, .binding = 0, .offset = 16, .format = 0x32, .channels = 2, .swizzle = {0, 1, kVertexSwizzleZero, kVertexSwizzleOne}},
	};
	std::string error;
	const std::optional<GraphicsShaders> shaders =
		CompileGraphicsShaders(tests::kMatrixVertSpirv, tests::kMatrixFragSpirv, GraphicsOptions{.vertexAttributes = attributes}, error);
	INFO(error);
	REQUIRE(shaders.has_value());
	CHECK(shaders->vertexBufferTableUserData == 1);
	CHECK(shaders->vertexSetTableUserData[0] == 4);
	CHECK(shaders->vertexPushConstantUserData == 5);
	CHECK(shaders->vertexPushConstantDwords == 1);
	CHECK(((shaders->vertex.pgmRsrc2 >> 1) & 0x1F) == 6);
	CHECK(shaders->vsOutConfig == 2);
	CHECK(shaders->numInterpolants == 2);
	REQUIRE(shaders->psInputCntl.size() == 2);
	CHECK(shaders->psInputCntl[0] == 0);
	CHECK(shaders->psInputCntl[1] == 1);
	REQUIRE(shaders->descriptors.size() == 1);
	CHECK(shaders->descriptors[0].kind == DescriptorKind::UniformBuffer);
}

TEST_CASE("print the compiled graphics test shaders", "[.][amd-compiler][dump-graphics]")
{
	const std::pair<std::span<const uint32_t>, std::span<const uint32_t>> pipelines[] = {
		{tests::kTestAppVertSpirv, tests::kTestAppFragSpirv},
		{tests::kVaryingsVertSpirv, tests::kVaryingsFragSpirv},
	};
	for (const auto& [vertex, fragment] : pipelines)
	{
		std::string error;
		const std::optional<GraphicsShaders> shaders = CompileGraphicsShaders(vertex, fragment, GraphicsOptions{}, error);
		INFO(error);
		REQUIRE(shaders.has_value());
		WARN("GS RSRC1 " << std::hex << shaders->vertex.pgmRsrc1 << " RSRC2 " << shaders->vertex.pgmRsrc2 << " PS RSRC1 " << shaders->fragment.pgmRsrc1
						 << " RSRC2 " << shaders->fragment.pgmRsrc2 << " ENA " << shaders->psInputEna << " ADDR " << shaders->psInputAddr << " VS_OUT "
						 << shaders->vsOutConfig);
		WARN(Disassemble(shaders->vertex.code, true));
		WARN(Disassemble(shaders->fragment.code, true));
	}
}
