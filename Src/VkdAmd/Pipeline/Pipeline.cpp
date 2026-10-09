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
		std::vector<amdcomp::SpecializationConstant> specialization;
		if (const VkSpecializationInfo* info = stage.pSpecializationInfo)
		{
			for (uint32_t i = 0; i < info->mapEntryCount; ++i)
			{
				const VkSpecializationMapEntry& entry = info->pMapEntries[i];
				uint64_t value = 0;
				const size_t size = std::min<size_t>(entry.size, sizeof(value));
				if (entry.offset + size <= info->dataSize)
					std::memcpy(&value, static_cast<const uint8_t*>(info->pData) + entry.offset, size);
				specialization.push_back({.id = entry.constantID, .value = value, .size = static_cast<uint32_t>(size)});
			}
		}

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
			const VkDescriptorType type =
				descriptor.kind == amdcomp::DescriptorKind::StorageBuffer ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			m_compiledDescriptors.push_back({descriptor.set, descriptor.binding, type, descriptor.offset, descriptor.count});
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
			}
			else if (info.pStages[i].stage == VK_SHADER_STAGE_FRAGMENT_BIT)
			{
				fragmentHash = HashSpirv(code);
				stageCode[1] = code;
			}
		}
		for (const PrecompiledGraphicsPipeline& pipeline : kPrecompiledGraphicsPipelines)
		{
			if (pipeline.vertexSpirvHash == vertexHash && pipeline.fragmentSpirvHash == fragmentHash)
				m_graphics = &pipeline;
		}
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
			cct::Logger::Warning("AmdPipeline: no GFX11 code for this VS/FS pair (SPIR-V hashes 0x{:016X} / 0x{:016X}), its draws are skipped",
								 vertexHash, fragmentHash);
			DumpUnknownSpirv(stageCode[0], vertexHash);
			DumpUnknownSpirv(stageCode[1], fragmentHash);
			return VK_SUCCESS;
		}
		return UploadCode(owner, m_graphics->esCode, m_graphics->psCode);
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
