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

TEST_CASE("descriptor-backed storage is reported as unsupported", "[amd-compiler][compute]")
{
	std::string error;
	const std::optional<ComputeShader> shader = CompileComputeShader(tests::kFillDescriptorSpirv, CompileOptions{}, error);
	CHECK_FALSE(shader.has_value());
	CHECK(error.find("StorageBuffer") != std::string::npos);
}

TEST_CASE("print the fill shader's registers", "[.][amd-compiler][dump]")
{
	std::string error;
	const std::optional<ComputeShader> shader = CompileComputeShader(tests::kFillBufferAddressSpirv, CompileOptions{}, error);
	REQUIRE(shader.has_value());
	WARN("RSRC1 " << std::hex << shader->pgmRsrc1 << " RSRC2 " << shader->pgmRsrc2 << " RSRC3 " << shader->pgmRsrc3 << " initiator " << shader->dispatchInitiator);
	WARN(Disassemble(shader->code, true));
}
