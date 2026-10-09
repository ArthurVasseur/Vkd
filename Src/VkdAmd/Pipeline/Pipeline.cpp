#include "VkdAmd/Pipeline/Pipeline.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>

#include <Concerto/Core/Logger/Logger.hpp>

#include "Vkd/RenderPass/RenderPass.hpp"
#include "Vkd/ShaderModule/ShaderModule.hpp"

#include "AmdCompiler/Compiler.hpp"
#include "VkdAmd/Device/Device.hpp"
#include "VkdAmd/PrivateData.hpp"

namespace vkd::amd
{
	namespace
	{
		// VKD_AMD_DUMP_SPIRV=<directory>: modules without precompiled GFX11 code are written there as
		// <hash>.spv, the input of a wddm-dump capture of AMDVLK running the same SPIR-V.
		void DumpUnknownSpirv(std::span<const uint32_t> spirv, uint64_t hash)
		{
			char dir[512] = {};
			size_t length = 0;
			if (spirv.empty() || getenv_s(&length, dir, sizeof(dir), "VKD_AMD_DUMP_SPIRV") != 0 || length == 0)
				return;
			char path[600];
			std::snprintf(path, sizeof(path), "%s/%016llX.spv", dir, static_cast<unsigned long long>(hash));
			FILE* file = nullptr;
			if (fopen_s(&file, path, "wb") != 0 || !file)
				return;
			std::fwrite(spirv.data(), sizeof(uint32_t), spirv.size(), file);
			std::fclose(file);
			cct::Logger::Info("AmdPipeline: SPIR-V written to {}", path);
		}

		bool EnvironmentFlag(const char* name)
		{
			char value[8] = {};
			size_t length = 0;
			return getenv_s(&length, value, sizeof(value), name) == 0 && length > 1 && value[0] == '1';
		}

		VkDescriptorType ToVkDescriptorType(amdcomp::DescriptorKind kind)
		{
			switch (kind)
			{
				case amdcomp::DescriptorKind::StorageBuffer:
					return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				case amdcomp::DescriptorKind::SampledImage:
					return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				case amdcomp::DescriptorKind::StorageImage:
					return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				case amdcomp::DescriptorKind::Sampler:
					return VK_DESCRIPTOR_TYPE_SAMPLER;
				case amdcomp::DescriptorKind::CombinedImageSampler:
					return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				default:
					return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			}
		}

		std::vector<amdcomp::SpecializationConstant> Specialization(const VkSpecializationInfo* info)
		{
			std::vector<amdcomp::SpecializationConstant> specialization;
			if (!info)
				return specialization;
			for (uint32_t i = 0; i < info->mapEntryCount; ++i)
			{
				const VkSpecializationMapEntry& entry = info->pMapEntries[i];
				uint64_t value = 0;
				const size_t size = std::min<size_t>(entry.size, sizeof(value));
				if (entry.offset + size <= info->dataSize)
					std::memcpy(&value, static_cast<const uint8_t*>(info->pData) + entry.offset, size);
				specialization.push_back({.id = entry.constantID, .value = value, .size = static_cast<uint32_t>(size)});
			}
			return specialization;
		}

		constexpr uint32_t kSpiShaderPgmChksumPs = 0x2C06;
		constexpr uint32_t kSpiShaderPgmRsrc1Ps = 0x2C0A;
		constexpr uint32_t kSpiShaderPgmRsrc2Ps = 0x2C0B;
		constexpr uint32_t kSpiShaderUserDataPs0 = 0x2C0C;
		constexpr uint32_t kSpiShaderUserDataPsLast = 0x2C2B;
		constexpr uint32_t kSpiShaderPgmChksumGs = 0x2C80;
		constexpr uint32_t kSpiShaderPgmRsrc1Gs = 0x2C8A;
		constexpr uint32_t kSpiShaderPgmRsrc2Gs = 0x2C8B;
		constexpr uint32_t kSpiPsInputCntl0 = 0xA191;
		constexpr uint32_t kSpiPsInputCntl31 = 0xA1B0;
		constexpr uint32_t kSpiVsOutConfig = 0xA1B1;
		constexpr uint32_t kSpiPsInputEna = 0xA1B3;
		constexpr uint32_t kSpiPsInputAddr = 0xA1B4;
		constexpr uint32_t kSpiPsInControl = 0xA1B6;
		constexpr uint32_t kSpiShaderColFormat = 0xA1C5;
		constexpr uint32_t kSpiPsInControlNumInterpMask = 0x3F;
		constexpr RegPatch kSetTablePatches[] = {RegPatch::SetTable0, RegPatch::SetTable1, RegPatch::SetTable2, RegPatch::SetTable3};

		struct CompiledComputeShader
		{
			std::optional<amdcomp::ComputeShader> shader;
			std::string error;
		};

		std::shared_ptr<const CompiledComputeShader> CompileCached(std::span<const uint32_t> spirv, const char* entryPoint,
																   std::span<const amdcomp::SpecializationConstant> specialization)
		{
			std::string key(reinterpret_cast<const char*>(spirv.data()), spirv.size_bytes());
			key.append(entryPoint ? entryPoint : "main");
			key.push_back('\0');
			for (const amdcomp::SpecializationConstant& constant : specialization)
			{
				key.append(reinterpret_cast<const char*>(&constant.id), sizeof(constant.id));
				key.append(reinterpret_cast<const char*>(&constant.value), sizeof(constant.value));
			}

			static std::mutex mutex;
			static std::unordered_map<std::string, std::shared_ptr<const CompiledComputeShader>> cache;
			{
				std::scoped_lock lock(mutex);
				if (auto found = cache.find(key); found != cache.end())
					return found->second;
			}

			auto compiled = std::make_shared<CompiledComputeShader>();
			compiled->shader = amdcomp::CompileComputeShader(
				spirv, amdcomp::CompileOptions{.entryPoint = entryPoint ? entryPoint : "main", .specialization = specialization}, compiled->error);
			std::scoped_lock lock(mutex);
			return cache.emplace(std::move(key), std::move(compiled)).first->second;
		}
	} // namespace

	VkResult Pipeline::CreateComputePipeline(vkd::Device& owner, const VkComputePipelineCreateInfo& info,
											 const VkAllocationCallbacks& allocationCallbacks)
	{
		const VkResult result = vkd::Pipeline::CreateComputePipeline(owner, info, allocationCallbacks);
		if (result != VK_SUCCESS)
			return result;

		VKD_FROM_HANDLE(vkd::ShaderModule, module, info.stage.module);
		if (!module)
			return VK_SUCCESS;
		const auto& spirv = module->GetCode();
		const std::span<const uint32_t> code(spirv.data(), spirv.size());
		if (!EnvironmentFlag("VKD_AMD_COMPILE_ALL"))
			m_computeShader = FindPrecompiledComputeShader(code);
		if (!m_computeShader)
			m_computeShader = CompileComputeShader(info.stage, code);
		if (!m_computeShader)
		{
			DumpUnknownSpirv(code, HashSpirv(code));
			return VK_SUCCESS;
		}

		return UploadCode(owner, m_computeShader->code);
	}

	const PrecompiledComputeShader* Pipeline::CompileComputeShader(const VkPipelineShaderStageCreateInfo& stage, std::span<const uint32_t> spirv)
	{
		const std::vector<amdcomp::SpecializationConstant> specialization = Specialization(stage.pSpecializationInfo);

		const std::shared_ptr<const CompiledComputeShader> cached = CompileCached(spirv, stage.pName, specialization);
		const std::optional<amdcomp::ComputeShader>& compiled = cached->shader;
		if (!compiled)
		{
			cct::Logger::Warning("AmdPipeline: can't compile compute shader (SPIR-V hash 0x{:016X}), its dispatches are skipped: {}", HashSpirv(spirv),
								 cached->error);
			return nullptr;
		}
		if (EnvironmentFlag("VKD_AMD_DUMP_ISA"))
		{
			cct::Logger::Info("AmdPipeline: compute shader 0x{:016X}:\n{}", HashSpirv(spirv),
							  amdcomp::Disassemble(compiled->code, (compiled->dispatchInitiator & 0x8000u) == 0));
		}

		m_compiledCode = compiled->code;
		m_compiledDescriptors.clear();
		for (const amdcomp::Descriptor& descriptor : compiled->descriptors)
		{
			m_compiledDescriptors.push_back({descriptor.set, descriptor.binding, ToVkDescriptorType(descriptor.kind), descriptor.offset, descriptor.count});
		}
		m_compiledShader = PrecompiledComputeShader{
			.spirvHash = HashSpirv(spirv),
			.code = m_compiledCode,
			.pgmRsrc1 = compiled->pgmRsrc1,
			.pgmRsrc2 = compiled->pgmRsrc2,
			.pgmRsrc3 = compiled->pgmRsrc3,
			.numThreadX = compiled->numThreadX,
			.numThreadY = compiled->numThreadY,
			.numThreadZ = compiled->numThreadZ,
			.dispatchInitiator = compiled->dispatchInitiator,
			.pushConstantUserData = compiled->pushConstantUserData,
			.pushConstantDwords = compiled->pushConstantDwords,
			.descriptors = m_compiledDescriptors,
			.numWorkgroupsUserData = compiled->numWorkgroupsUserData,
		};
		for (uint32_t set = 0; set < kMaxPrecompiledSets && set < amdcomp::kMaxDescriptorSets; ++set)
			m_compiledShader.setTableUserData[set] = compiled->setTableUserData[set];
		return &m_compiledShader;
	}

	VkResult Pipeline::CreateGraphicsPipeline(vkd::Device& owner, const VkGraphicsPipelineCreateInfo& info,
											  const VkAllocationCallbacks& allocationCallbacks)
	{
		const VkResult result = vkd::Pipeline::CreateGraphicsPipeline(owner, info, allocationCallbacks);
		if (result != VK_SUCCESS)
			return result;

		const vkd::GraphicsPipelineStates states = vkd::GetGraphicsPipelineStates(info);
		if (states.viewport)
		{
			if (states.viewport->viewportCount > 0 && states.viewport->pViewports)
				m_graphicsState.viewport = states.viewport->pViewports[0];
			if (states.viewport->scissorCount > 0 && states.viewport->pScissors)
				m_graphicsState.scissor = states.viewport->pScissors[0];
		}
		if (states.dynamic)
		{
			for (uint32_t i = 0; i < states.dynamic->dynamicStateCount; ++i)
			{
				const VkDynamicState state = states.dynamic->pDynamicStates[i];
				m_graphicsState.dynamicViewport |= state == VK_DYNAMIC_STATE_VIEWPORT;
				m_graphicsState.dynamicScissor |= state == VK_DYNAMIC_STATE_SCISSOR;
				m_graphicsState.dynamicBlendConstants |= state == VK_DYNAMIC_STATE_BLEND_CONSTANTS;
				m_graphicsState.dynamicStencilCompareMask |= state == VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
				m_graphicsState.dynamicStencilWriteMask |= state == VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
				m_graphicsState.dynamicStencilReference |= state == VK_DYNAMIC_STATE_STENCIL_REFERENCE;
				m_graphicsState.dynamicDepthBounds |= state == VK_DYNAMIC_STATE_DEPTH_BOUNDS;
				m_graphicsState.dynamicDepthBias |= state == VK_DYNAMIC_STATE_DEPTH_BIAS;
			}
		}
		std::span<const VkPipelineColorBlendAttachmentState> blendAttachments;
		if (states.colorBlend && states.colorBlend->pAttachments)
		{
			blendAttachments = {states.colorBlend->pAttachments, std::min(states.colorBlend->attachmentCount, kMaxColorTargets)};
			std::copy_n(states.colorBlend->blendConstants, 4, m_graphicsState.blendConstants);
		}
		m_graphicsState.blend = ComputeColorBlendRegs(blendAttachments);
		m_graphicsState.depthStencil = ComputeDepthStencilRegs(states.depthStencil);
		if (states.vertexInput)
		{
			for (uint32_t i = 0; i < states.vertexInput->vertexBindingDescriptionCount; ++i)
			{
				const VkVertexInputBindingDescription& binding = states.vertexInput->pVertexBindingDescriptions[i];
				if (binding.binding >= GraphicsPipelineState::kMaxVertexBindings)
					continue;
				m_graphicsState.vertexStrides[binding.binding] = binding.stride;
				m_graphicsState.vertexBindingCount = std::max(m_graphicsState.vertexBindingCount, binding.binding + 1);
			}
		}
		if (states.rasterization)
		{
			m_graphicsState.cullMode = states.rasterization->cullMode;
			m_graphicsState.frontFace = states.rasterization->frontFace;
			m_graphicsState.depthBiasEnable = states.rasterization->depthBiasEnable;
			m_graphicsState.depthBias = {states.rasterization->depthBiasConstantFactor, states.rasterization->depthBiasClamp,
										 states.rasterization->depthBiasSlopeFactor};
		}

		uint64_t vertexHash = 0;
		uint64_t fragmentHash = 0;
		std::span<const uint32_t> stageCode[2];
		const VkPipelineShaderStageCreateInfo* stages[2] = {};
		for (uint32_t i = 0; i < info.stageCount; ++i)
		{
			VKD_FROM_HANDLE(vkd::ShaderModule, module, info.pStages[i].module);
			if (!module)
				continue;
			const auto& spirv = module->GetCode();
			const std::span<const uint32_t> code(spirv.data(), spirv.size());
			if (info.pStages[i].stage == VK_SHADER_STAGE_VERTEX_BIT)
			{
				vertexHash = HashSpirv(code);
				stageCode[0] = code;
				stages[0] = &info.pStages[i];
			}
			else if (info.pStages[i].stage == VK_SHADER_STAGE_FRAGMENT_BIT)
			{
				fragmentHash = HashSpirv(code);
				stageCode[1] = code;
				stages[1] = &info.pStages[i];
			}
		}
		if (!EnvironmentFlag("VKD_AMD_COMPILE_ALL"))
		{
			for (const PrecompiledGraphicsPipeline& pipeline : kPrecompiledGraphicsPipelines)
			{
				if (pipeline.vertexSpirvHash == vertexHash && pipeline.fragmentSpirvHash == fragmentHash)
					m_graphics = &pipeline;
			}
		}
		if (!m_graphics && stages[0] && stages[1])
			m_graphics = CompileGraphicsPipeline(info, *stages[0], stageCode[0], *stages[1], stageCode[1]);
		if (m_graphics)
		{
			// CB_SHADER_MASK is the PS's: the captured one.
			uint32_t shaderMask = 0;
			for (const RegWrite& w : m_graphics->drawState)
				if (w.reg == kCbShaderMask)
					shaderMask = w.value;
			m_graphicsState.cbColorControl = CbColorControl(shaderMask, m_graphicsState.blend.cbTargetMask);
		}
		if (!m_graphics)
		{
			DumpUnknownSpirv(stageCode[0], vertexHash);
			DumpUnknownSpirv(stageCode[1], fragmentHash);
			return VK_SUCCESS;
		}
		return UploadCode(owner, m_graphics->esCode, m_graphics->psCode);
	}

	const PrecompiledGraphicsPipeline* Pipeline::CompileGraphicsPipeline(const VkGraphicsPipelineCreateInfo& info, const VkPipelineShaderStageCreateInfo& vertexStage,
																		 std::span<const uint32_t> vertexSpirv, const VkPipelineShaderStageCreateInfo& fragmentStage,
																		 std::span<const uint32_t> fragmentSpirv)
	{
		const uint64_t vertexHash = HashSpirv(vertexSpirv);
		const uint64_t fragmentHash = HashSpirv(fragmentSpirv);
		VkFormat colorFormat = VK_FORMAT_UNDEFINED;
		VKD_FROM_HANDLE(vkd::RenderPass, renderPass, info.renderPass);
		if (renderPass && info.subpass < renderPass->GetSubpasses().size())
		{
			const VkSubpassDescription& subpass = renderPass->GetSubpasses()[info.subpass];
			if (subpass.colorAttachmentCount > 0 && subpass.pColorAttachments[0].attachment < renderPass->GetAttachments().size())
				colorFormat = renderPass->GetAttachments()[subpass.pColorAttachments[0].attachment].format;
		}
		const PrecompiledGraphicsPipeline* base = nullptr;
		for (const PrecompiledGraphicsPipeline& pipeline : kPrecompiledGraphicsPipelines)
		{
			if (pipeline.drawState.data() == kTestAppDrawState)
				base = &pipeline;
		}
		if (!base || colorFormat != base->colorFormat)
		{
			cct::Logger::Warning("AmdPipeline: can't compile the VS/FS pair (SPIR-V hashes 0x{:016X} / 0x{:016X}), its draws are skipped: compiled pipelines only "
								 "render into one R8G8B8A8_UNORM color attachment yet",
								 vertexHash, fragmentHash);
			return nullptr;
		}

		const std::vector<amdcomp::SpecializationConstant> vertexSpecialization = Specialization(vertexStage.pSpecializationInfo);
		const std::vector<amdcomp::SpecializationConstant> fragmentSpecialization = Specialization(fragmentStage.pSpecializationInfo);
		const amdcomp::ColorExportFormat colorExports[] = {amdcomp::ColorExportFormat::Float16};
		amdcomp::GraphicsOptions options{
			.vertex = {.entryPoint = vertexStage.pName, .specialization = vertexSpecialization},
			.fragment = {.entryPoint = fragmentStage.pName, .specialization = fragmentSpecialization},
			.colorExports = colorExports,
		};
		std::string error;
		std::optional<amdcomp::GraphicsShaders> compiled = amdcomp::CompileGraphicsShaders(vertexSpirv, fragmentSpirv, options, error);
		if (!compiled)
		{
			cct::Logger::Warning("AmdPipeline: can't compile the VS/FS pair (SPIR-V hashes 0x{:016X} / 0x{:016X}), its draws are skipped: {}", vertexHash,
								 fragmentHash, error);
			return nullptr;
		}
		if (EnvironmentFlag("VKD_AMD_DUMP_ISA"))
		{
			cct::Logger::Info("AmdPipeline: vertex shader 0x{:016X}:\n{}", vertexHash, amdcomp::Disassemble(compiled->vertex.code, true));
			cct::Logger::Info("AmdPipeline: fragment shader 0x{:016X}:\n{}", fragmentHash, amdcomp::Disassemble(compiled->fragment.code, true));
		}

		m_compiledDrawState.clear();
		for (const RegWrite& write : base->drawState)
		{
			RegWrite w = write;
			switch (w.reg)
			{
				case kSpiShaderPgmRsrc1Gs:
					w.value = compiled->vertex.pgmRsrc1;
					break;
				case kSpiShaderPgmRsrc2Gs:
					w.value = compiled->vertex.pgmRsrc2;
					break;
				case kSpiShaderPgmRsrc1Ps:
					w.value = compiled->fragment.pgmRsrc1;
					break;
				case kSpiShaderPgmRsrc2Ps:
					w.value = compiled->fragment.pgmRsrc2;
					break;
				case kSpiShaderPgmChksumGs:
				case kSpiShaderPgmChksumPs:
					w.value = 0;
					break;
				case kSpiVsOutConfig:
					w.value = compiled->vsOutConfig;
					break;
				case kSpiPsInputEna:
					w.value = compiled->psInputEna;
					break;
				case kSpiPsInputAddr:
					w.value = compiled->psInputAddr;
					break;
				case kSpiPsInControl:
					w.value = (w.value & ~kSpiPsInControlNumInterpMask) | compiled->numInterpolants;
					break;
				case kSpiShaderColFormat:
					w.value = compiled->colorFormat;
					break;
				case kCbShaderMask:
					w.value = compiled->shaderMask;
					break;
				default:
					break;
			}
			const bool psInputCntl = w.kind == RegKind::Context && w.reg >= kSpiPsInputCntl0 && w.reg <= kSpiPsInputCntl31;
			const bool psUserData = w.kind == RegKind::Sh && w.reg > kSpiShaderUserDataPs0 && w.reg <= kSpiShaderUserDataPsLast;
			if (!psInputCntl && !psUserData)
				m_compiledDrawState.push_back(w);
		}
		for (size_t i = 0; i < compiled->psInputCntl.size(); ++i)
			m_compiledDrawState.push_back({RegKind::Context, RegPatch::None, kSpiPsInputCntl0 + static_cast<uint32_t>(i), compiled->psInputCntl[i]});
		for (uint32_t set = 0; set < amdcomp::kMaxDescriptorSets; ++set)
		{
			if (compiled->fragmentSetTableUserData[set] == 0)
				continue;
			if (set >= std::size(kSetTablePatches))
			{
				cct::Logger::Warning("AmdPipeline: the fragment shader reads descriptor set {}, draws only bind sets 0-3 yet", set);
				return nullptr;
			}
			m_compiledDrawState.push_back({RegKind::Sh, kSetTablePatches[set], kSpiShaderUserDataPs0 + compiled->fragmentSetTableUserData[set], 0});
		}
		m_compiledPushConstants.clear();
		if (compiled->fragmentPushConstantDwords > 0)
		{
			const uint32_t firstReg = kSpiShaderUserDataPs0 + compiled->fragmentPushConstantUserData;
			m_compiledPushConstants.push_back({firstReg, compiled->fragmentPushConstantDwords, 0});
			for (uint32_t d = 0; d < compiled->fragmentPushConstantDwords; ++d)
				m_compiledDrawState.push_back({RegKind::Sh, RegPatch::PushConstant, firstReg + d, 0});
		}

		m_compiledDescriptors.clear();
		for (const amdcomp::Descriptor& descriptor : compiled->descriptors)
			m_compiledDescriptors.push_back({descriptor.set, descriptor.binding, ToVkDescriptorType(descriptor.kind), descriptor.offset, descriptor.count});
		m_compiledEsCode = std::move(compiled->vertex.code);
		m_compiledPsCode = std::move(compiled->fragment.code);
		m_compiledGraphics = PrecompiledGraphicsPipeline{
			.vertexSpirvHash = vertexHash,
			.fragmentSpirvHash = fragmentHash,
			.colorFormat = base->colorFormat,
			.esCode = m_compiledEsCode,
			.psCode = m_compiledPsCode,
			.drawState = m_compiledDrawState,
			.drawInitiator = base->drawInitiator,
			.descriptors = m_compiledDescriptors,
			.pushConstants = m_compiledPushConstants,
		};
		return &m_compiledGraphics;
	}

	// Shader code is read by the GPU only, uploaded once through the CPU mapping (AMDVLK puts its code
	// in 0x20002004 allocations too), in the shader VA window. Program addresses must be 256-byte
	// aligned: the allocation starts on a 64KB page and `second` starts on the next 256 bytes.
	VkResult Pipeline::UploadCode(vkd::Device& owner, std::span<const uint32_t> first, std::span<const uint32_t> second)
	{
		auto* amdDevice = static_cast<AmdDevice*>(&owner);
		m_psCodeOffset = (first.size_bytes() + 255) & ~uint64_t{255};
		const uint64_t codeBytes = m_psCodeOffset + second.size_bytes();
		if (!amdDevice->AllocateShaderWindowMemory(codeBytes, kSurfaceFlagsInitA, m_code) || m_code.GetGpuVa() == 0)
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;
		auto* cpu = static_cast<uint8_t*>(m_code.MapToHost());
		if (!cpu)
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;
		std::memcpy(cpu, first.data(), first.size_bytes());
		if (!second.empty())
			std::memcpy(cpu + m_psCodeOffset, second.data(), second.size_bytes());
		m_code.UnmapFromHost();
		return VK_SUCCESS;
	}
} // namespace vkd::amd
