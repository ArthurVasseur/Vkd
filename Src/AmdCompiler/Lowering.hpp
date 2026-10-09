#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
		std::array<uint32_t, kMaxDescriptorSets> setTableUserData = {};
		std::vector<Descriptor> descriptors;
		uint32_t numWorkgroupsUserData = 0;
	};

	std::unique_ptr<llvm::Module> LowerComputeShader(const see::spirv::Module& spirv, const CompileOptions& options, llvm::LLVMContext& context,
													 llvm::TargetMachine& tm, ComputeLayout& layout, std::string& error);

	inline constexpr uint32_t kVertexUserSgprs = 3;

	struct VertexLayout
	{
		bool instanceIndex = false;
		std::vector<uint32_t> outputLocations;
		std::vector<Descriptor> descriptors;
	};

	struct FragmentInput
	{
		uint32_t location;
		bool flat;
	};

	struct FragmentLayout
	{
		uint32_t userSgprs = 1;
		std::array<uint32_t, kMaxDescriptorSets> setTableUserData = {};
		uint32_t pushConstantUserData = 0;
		uint32_t pushConstantDwords = 0;
		std::vector<FragmentInput> inputs;
		uint32_t colorFormat = 0;
		uint32_t shaderMask = 0;
		std::vector<Descriptor> descriptors;
	};

	bool ScanDescriptors(const see::spirv::Module& spirv, const CompileOptions& options, llvm::LLVMContext& context, llvm::TargetMachine& tm,
						 std::vector<Descriptor>& descriptors, std::string& error);

	std::unique_ptr<llvm::Module> LowerVertexShader(const see::spirv::Module& spirv, const CompileOptions& options, std::span<const Descriptor> descriptorLayout,
													llvm::LLVMContext& context, llvm::TargetMachine& tm, VertexLayout& layout, std::string& error);

	std::unique_ptr<llvm::Module> LowerFragmentShader(const see::spirv::Module& spirv, const CompileOptions& options,
													  std::span<const Descriptor> descriptorLayout, std::span<const ColorExportFormat> colorExports,
													  llvm::LLVMContext& context, llvm::TargetMachine& tm, FragmentLayout& layout, std::string& error);
} // namespace amdcomp
