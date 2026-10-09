#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "AmdCompiler/Compiler.hpp"

namespace llvm
{
	class LLVMContext;
	class Module;
	class TargetMachine;
} // namespace llvm

namespace see::spirv
{
	struct Module;
}

namespace amdcomp
{
	inline constexpr uint32_t kPushConstantUserData = 1;
	inline constexpr uint32_t kMaxComputeUserSgprs = 16;

	struct ComputeLayout
	{
		std::array<uint32_t, 3> localSize = {1, 1, 1};
		uint32_t pushConstantDwords = 0;
		uint32_t userSgprs = 1;
	};

	std::unique_ptr<llvm::Module> LowerComputeShader(const see::spirv::Module& spirv, const CompileOptions& options, llvm::LLVMContext& context,
													 llvm::TargetMachine& tm, ComputeLayout& layout, std::string& error);
} // namespace amdcomp
