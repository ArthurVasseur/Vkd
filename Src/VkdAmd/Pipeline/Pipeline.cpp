#include "VkdAmd/Pipeline/Pipeline.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>

#include <Concerto/Core/Logger/Logger.hpp>

#include "Vkd/ShaderModule/ShaderModule.hpp"

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
		m_computeShader = FindPrecompiledComputeShader(code);
		if (!m_computeShader)
		{
			cct::Logger::Warning("AmdPipeline: no GFX11 code for this compute shader (SPIR-V hash 0x{:016X}), its dispatches are skipped",
								 HashSpirv(code));
			DumpUnknownSpirv(code, HashSpirv(code));
			return VK_SUCCESS;
		}

		return UploadCode(owner, m_computeShader->code);
	}

	VkResult Pipeline::CreateGraphicsPipeline(vkd::Device& owner, const VkGraphicsPipelineCreateInfo& info,
											  const VkAllocationCallbacks& allocationCallbacks)
	{
		const VkResult result = vkd::Pipeline::CreateGraphicsPipeline(owner, info, allocationCallbacks);
		if (result != VK_SUCCESS)
			return result;

		if (info.pViewportState)
		{
			if (info.pViewportState->viewportCount > 0 && info.pViewportState->pViewports)
				m_graphicsState.viewport = info.pViewportState->pViewports[0];
			if (info.pViewportState->scissorCount > 0 && info.pViewportState->pScissors)
				m_graphicsState.scissor = info.pViewportState->pScissors[0];
		}
		if (info.pDynamicState)
		{
			for (uint32_t i = 0; i < info.pDynamicState->dynamicStateCount; ++i)
			{
				const VkDynamicState state = info.pDynamicState->pDynamicStates[i];
				m_graphicsState.dynamicViewport |= state == VK_DYNAMIC_STATE_VIEWPORT;
				m_graphicsState.dynamicScissor |= state == VK_DYNAMIC_STATE_SCISSOR;
			}
		}
		if (info.pVertexInputState)
		{
			for (uint32_t i = 0; i < info.pVertexInputState->vertexBindingDescriptionCount; ++i)
			{
				const VkVertexInputBindingDescription& binding = info.pVertexInputState->pVertexBindingDescriptions[i];
				if (binding.binding >= GraphicsPipelineState::kMaxVertexBindings)
					continue;
				m_graphicsState.vertexStrides[binding.binding] = binding.stride;
				m_graphicsState.vertexBindingCount = std::max(m_graphicsState.vertexBindingCount, binding.binding + 1);
			}
		}
		if (info.pRasterizationState)
		{
			m_graphicsState.cullMode = info.pRasterizationState->cullMode;
			m_graphicsState.frontFace = info.pRasterizationState->frontFace;
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
