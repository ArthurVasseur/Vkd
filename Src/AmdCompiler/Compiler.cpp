#include "AmdCompiler/Compiler.hpp"

#include "AmdCompiler/Backend.hpp"
#include "AmdCompiler/Lowering.hpp"
#include "See/Spirv/Module.hpp"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>

namespace amdcomp
{
	namespace
	{
		constexpr uint32_t kRsrc2ScratchEn = 1u << 0;
		constexpr uint32_t kRsrc2UserSgprShift = 1;
		constexpr uint32_t kRsrc2UserSgprMask = 0x1Fu << kRsrc2UserSgprShift;
		constexpr uint32_t kRsrc2TgidXyzTgSizeEn = 0xFu << 7;
		constexpr uint32_t kRsrc2TidigCompCntXyz = 2u << 11;
		constexpr uint32_t kRsrc3ImageOp = 1u << 31;
		constexpr uint32_t kDispatchInitiator = 0x00002045;
	} // namespace

	std::optional<ComputeShader> CompileComputeShader(std::span<const uint32_t> spirv, const CompileOptions& options, std::string& error)
	{
		const std::optional<see::spirv::Module> module = see::spirv::Parse(spirv);
		if (!module)
		{
			error = "invalid SPIR-V";
			return std::nullopt;
		}

		std::unique_ptr<llvm::TargetMachine> tm = backend::CreateTargetMachine(true, error);
		if (!tm)
			return std::nullopt;

		llvm::LLVMContext context;
		ComputeLayout layout;
		std::unique_ptr<llvm::Module> ir = LowerComputeShader(*module, options, context, *tm, layout, error);
		if (!ir)
			return std::nullopt;
		if (options.llvmIr)
		{
			llvm::raw_string_ostream os(*options.llvmIr);
			ir->print(os, nullptr);
		}

		std::optional<backend::Output> output = backend::Generate(*ir, *tm, error);
		if (!output)
			return std::nullopt;
		const uint32_t rsrc2 = output->registers[backend::kComputePgmRsrc2];
		if (output->scratchSize > 0 || (rsrc2 & kRsrc2ScratchEn))
		{
			error = "the code needs scratch memory, which Vkd doesn't set up yet";
			return std::nullopt;
		}

		ComputeShader shader;
		shader.code = std::move(output->code);
		shader.pgmRsrc1 = output->registers[backend::kComputePgmRsrc1];
		shader.pgmRsrc2 = (rsrc2 & ~kRsrc2UserSgprMask) | (layout.userSgprs << kRsrc2UserSgprShift) | kRsrc2TgidXyzTgSizeEn | kRsrc2TidigCompCntXyz;
		shader.pgmRsrc3 = kRsrc3ImageOp;
		shader.numThreadX = layout.localSize[0];
		shader.numThreadY = layout.localSize[1];
		shader.numThreadZ = layout.localSize[2];
		shader.dispatchInitiator = kDispatchInitiator | output->registers[backend::kComputeDispatchInitiator];
		shader.pushConstantUserData = layout.pushConstantDwords > 0 ? kPushConstantUserData : 0;
		shader.pushConstantDwords = layout.pushConstantDwords;
		for (uint32_t set = 0; set < kMaxDescriptorSets; ++set)
			shader.setTableUserData[set] = layout.setTableUserData[set];
		shader.descriptors = std::move(layout.descriptors);
		shader.numWorkgroupsUserData = layout.numWorkgroupsUserData;
		return shader;
	}
} // namespace amdcomp
