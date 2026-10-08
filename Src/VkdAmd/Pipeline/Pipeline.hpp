#pragma once

#include "Vkd/Pipeline/Pipeline.hpp"

#include "VkdAmd/Pipeline/ColorBlend.hpp"
#include "VkdAmd/Shader/PrecompiledGraphics.hpp"
#include "VkdAmd/Shader/PrecompiledShaders.hpp"
#include "VkdAmd/Wddm/WddmAllocation.hpp"

namespace vkd::amd
{
	// The Vulkan state a replayed graphics pipeline's draws take from the pipeline (or from the
	// command buffer when it's dynamic): what the patched registers of PrecompiledGraphics.hpp
	// encode.
	struct GraphicsPipelineState
	{
		VkViewport viewport = {};
		VkRect2D scissor = {};
		bool dynamicViewport = false;
		bool dynamicScissor = false;
		VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
		VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		// Vertex input bindings: stride per binding, bindings [0, vertexBindingCount) in the table.
		static constexpr uint32_t kMaxVertexBindings = 32;
		uint32_t vertexStrides[kMaxVertexBindings] = {};
		uint32_t vertexBindingCount = 0;
		// Color blend state, written over the captured one (ColorBlend.hpp).
		ColorBlendRegs blend;
		uint32_t cbColorControl = 0;
		float blendConstants[4] = {};
		bool dynamicBlendConstants = false;
	};

	// A pipeline owns its shaders' GFX11 code in a GPU allocation. There's no SPIR-V compiler yet:
	// the code comes from the precompiled tables (PrecompiledShaders.hpp, PrecompiledGraphics.hpp),
	// and a pipeline whose modules aren't in them has no code -- its dispatches/draws are skipped.
	class Pipeline : public vkd::Pipeline
	{
	public:
		using vkd::Pipeline::Pipeline;
		~Pipeline() override = default;

		VkResult CreateComputePipeline(vkd::Device& owner, const VkComputePipelineCreateInfo& info,
									   const VkAllocationCallbacks& allocationCallbacks) override;
		VkResult CreateGraphicsPipeline(vkd::Device& owner, const VkGraphicsPipelineCreateInfo& info,
										const VkAllocationCallbacks& allocationCallbacks) override;

		[[nodiscard]] const PrecompiledComputeShader* GetComputeShader() const
		{
			return m_computeShader;
		}
		[[nodiscard]] D3DGPU_VIRTUAL_ADDRESS GetCodeVa() const
		{
			return m_code.GetGpuVa();
		}
		[[nodiscard]] const PrecompiledGraphicsPipeline* GetGraphics() const
		{
			return m_graphics;
		}
		// ES code at the start of the code allocation, PS code right after it (256-byte aligned).
		[[nodiscard]] D3DGPU_VIRTUAL_ADDRESS GetPsCodeVa() const
		{
			return m_code.GetGpuVa() + m_psCodeOffset;
		}
		[[nodiscard]] const GraphicsPipelineState& GetGraphicsState() const
		{
			return m_graphicsState;
		}

	private:
		VkResult UploadCode(vkd::Device& owner, std::span<const uint32_t> first, std::span<const uint32_t> second = {});

		const PrecompiledComputeShader* m_computeShader = nullptr;
		const PrecompiledGraphicsPipeline* m_graphics = nullptr;
		GraphicsPipelineState m_graphicsState;
		uint64_t m_psCodeOffset = 0;
		wddm::WddmAllocation m_code;
	};
} // namespace vkd::amd
