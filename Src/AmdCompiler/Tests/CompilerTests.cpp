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
