#include "AmdCompiler/Compiler.hpp"

#include <algorithm>
#include <array>
#include <format>

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
		constexpr uint32_t kRsrc1GsVgprCompCntOne = 1u << 29;
		constexpr uint32_t kRsrc2EsVgprCompCntInstance = 3u << 16;
		constexpr uint32_t kVsOutNoParameterExport = 1u << 7;
		constexpr uint32_t kPsInputFlatShade = 1u << 10;

		bool MergeDescriptors(std::vector<Descriptor>& merged, const std::vector<Descriptor>& stage, std::string& error)
		{
			for (const Descriptor& descriptor : stage)
			{
				auto existing = std::find_if(merged.begin(), merged.end(),
											 [&](const Descriptor& d)
											 { return d.set == descriptor.set && d.binding == descriptor.binding; });
				if (existing == merged.end())
					merged.push_back(descriptor);
				else if (existing->kind != descriptor.kind)
				{
					error = std::format("set {} binding {} has different descriptor types in the vertex and fragment shaders", descriptor.set, descriptor.binding);
					return false;
				}
				else
					existing->count = std::max(existing->count, descriptor.count);
			}
			return true;
		}

		uint32_t DescriptorBytes(DescriptorKind kind)
		{
			switch (kind)
			{
				case DescriptorKind::SampledImage:
				case DescriptorKind::StorageImage:
					return 32;
				case DescriptorKind::CombinedImageSampler:
					return 48;
				default:
					return 16;
			}
		}

		std::optional<backend::Output> GenerateStage(std::unique_ptr<llvm::Module> ir, llvm::TargetMachine& tm, const CompileOptions& options, std::string& error)
		{
			if (!ir)
				return std::nullopt;
			if (options.llvmIr)
			{
				llvm::raw_string_ostream os(*options.llvmIr);
				ir->print(os, nullptr);
			}
			std::optional<backend::Output> output = backend::Generate(*ir, tm, error, options.optimizedLlvmIr);
			if (output && output->scratchSize > 0)
			{
				error = "the code needs scratch memory, which Vkd doesn't set up yet";
				return std::nullopt;
			}
			return output;
		}
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

		std::optional<backend::Output> output = backend::Generate(*ir, *tm, error, options.optimizedLlvmIr);
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

	std::optional<GraphicsShaders> CompileGraphicsShaders(std::span<const uint32_t> vertexSpirv, std::span<const uint32_t> fragmentSpirv,
														  const GraphicsOptions& options, std::string& error)
	{
		const std::optional<see::spirv::Module> vertexModule = see::spirv::Parse(vertexSpirv);
		const std::optional<see::spirv::Module> fragmentModule = see::spirv::Parse(fragmentSpirv);
		if (!vertexModule || !fragmentModule)
		{
			error = "invalid SPIR-V";
			return std::nullopt;
		}
		std::unique_ptr<llvm::TargetMachine> tm = backend::CreateTargetMachine(true, error);
		if (!tm)
			return std::nullopt;

		std::vector<Descriptor> layout;
		{
			llvm::LLVMContext context;
			std::vector<Descriptor> vertexDescriptors;
			std::vector<Descriptor> fragmentDescriptors;
			if (!ScanDescriptors(*vertexModule, options.vertex, context, *tm, vertexDescriptors, error) ||
				!ScanDescriptors(*fragmentModule, options.fragment, context, *tm, fragmentDescriptors, error) ||
				!MergeDescriptors(layout, vertexDescriptors, error) || !MergeDescriptors(layout, fragmentDescriptors, error))
				return std::nullopt;
		}
		std::sort(layout.begin(), layout.end(), [](const Descriptor& a, const Descriptor& b)
				  { return a.set != b.set ? a.set < b.set : a.binding < b.binding; });
		std::array<uint32_t, kMaxDescriptorSets> tableBytes = {};
		for (Descriptor& descriptor : layout)
		{
			if (descriptor.set >= kMaxDescriptorSets)
			{
				error = std::format("descriptor set {} is beyond the {} sets Vkd supports", descriptor.set, kMaxDescriptorSets);
				return std::nullopt;
			}
			descriptor.offset = tableBytes[descriptor.set];
			tableBytes[descriptor.set] += descriptor.count * DescriptorBytes(descriptor.kind);
		}

		VertexLayout vertexLayout;
		std::optional<backend::Output> vertex;
		{
			llvm::LLVMContext context;
			vertex = GenerateStage(LowerVertexShader(*vertexModule, options.vertex, layout, context, *tm, vertexLayout, error), *tm, options.vertex, error);
		}
		if (!vertex)
			return std::nullopt;
		FragmentLayout fragmentLayout;
		std::optional<backend::Output> fragment;
		{
			llvm::LLVMContext context;
			fragment = GenerateStage(LowerFragmentShader(*fragmentModule, options.fragment, layout, options.colorExports, context, *tm, fragmentLayout, error),
									 *tm, options.fragment, error);
		}
		if (!fragment)
			return std::nullopt;

		GraphicsShaders shaders;
		shaders.vertex.code = std::move(vertex->code);
		shaders.vertex.pgmRsrc1 = vertex->registers[backend::kGsPgmRsrc1] | kRsrc1GsVgprCompCntOne;
		shaders.vertex.pgmRsrc2 = (vertex->registers[backend::kGsPgmRsrc2] & ~kRsrc2UserSgprMask) | (kVertexUserSgprs << kRsrc2UserSgprShift) |
								  (vertexLayout.instanceIndex ? kRsrc2EsVgprCompCntInstance : 0);
		shaders.instanceIndex = vertexLayout.instanceIndex;
		shaders.fragment.code = std::move(fragment->code);
		shaders.fragment.pgmRsrc1 = fragment->registers[backend::kPsPgmRsrc1];
		shaders.fragment.pgmRsrc2 = (fragment->registers[backend::kPsPgmRsrc2] & ~kRsrc2UserSgprMask) | (fragmentLayout.userSgprs << kRsrc2UserSgprShift);

		const size_t parameters = vertexLayout.outputLocations.size();
		shaders.vsOutConfig = parameters == 0 ? kVsOutNoParameterExport : static_cast<uint32_t>((parameters - 1) << 1);
		for (const FragmentInput& input : fragmentLayout.inputs)
		{
			auto parameter = std::find(vertexLayout.outputLocations.begin(), vertexLayout.outputLocations.end(), input.location);
			if (parameter == vertexLayout.outputLocations.end())
			{
				error = std::format("the fragment shader reads location {}, which the vertex shader doesn't write", input.location);
				return std::nullopt;
			}
			shaders.psInputCntl.push_back(static_cast<uint32_t>(parameter - vertexLayout.outputLocations.begin()) | (input.flat ? kPsInputFlatShade : 0));
		}
		shaders.numInterpolants = static_cast<uint32_t>(fragmentLayout.inputs.size());
		shaders.psInputEna = fragment->registers[backend::kSpiPsInputEna];
		shaders.psInputAddr = fragment->registers[backend::kSpiPsInputAddr];
		shaders.colorFormat = fragmentLayout.colorFormat;
		shaders.shaderMask = fragmentLayout.shaderMask;
		for (uint32_t set = 0; set < kMaxDescriptorSets; ++set)
			shaders.fragmentSetTableUserData[set] = fragmentLayout.setTableUserData[set];
		shaders.fragmentPushConstantUserData = fragmentLayout.pushConstantUserData;
		shaders.fragmentPushConstantDwords = fragmentLayout.pushConstantDwords;
		shaders.descriptors = std::move(layout);
		return shaders;
	}
} // namespace amdcomp
