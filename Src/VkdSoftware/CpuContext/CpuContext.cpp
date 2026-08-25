/**
 * @file CpuContext.cpp
 * @brief Implementation of CPU rendering context
 * @date 2025-10-27
 */

#include "VkdSoftware/CpuContext/CpuContext.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>

#include "Vkd/Buffer/Buffer.hpp"
#include "Vkd/DescriptorSet/DescriptorSet.hpp"
#include "Vkd/DeviceMemory/DeviceMemory.hpp"
#include "Vkd/ImageView/ImageView.hpp"
#include "VkdSoftware/Device/Device.hpp"
#include "VkdSoftware/Pipeline/Pipeline.hpp"

#include "See/Exec/Stage.hpp"
#include <spirv/unified1/spirv.h>
#include <vulkan/utility/vk_format_utils.h>

namespace vkd::software
{
	namespace
	{
		// Float in [0,1] -> 8-bit unorm.
		UInt8 FloatToUNorm8(float f)
		{
			if (f <= 0.0f)
				return 0;
			if (f >= 1.0f)
				return 255;
			return static_cast<UInt8>(f * 255.0f + 0.5f);
		}

		// Build the 4-byte RGBA pattern for the given format.
		// Returns true if the format is supported by this fast path.
		bool BuildRgba8ClearPattern(VkFormat format, const VkClearColorValue& color, UInt8 outPattern[4])
		{
			switch (format)
			{
				case VK_FORMAT_R8G8B8A8_UNORM:
				case VK_FORMAT_R8G8B8A8_SRGB:
					outPattern[0] = FloatToUNorm8(color.float32[0]);
					outPattern[1] = FloatToUNorm8(color.float32[1]);
					outPattern[2] = FloatToUNorm8(color.float32[2]);
					outPattern[3] = FloatToUNorm8(color.float32[3]);
					return true;
				case VK_FORMAT_B8G8R8A8_UNORM:
				case VK_FORMAT_B8G8R8A8_SRGB:
					outPattern[0] = FloatToUNorm8(color.float32[2]);
					outPattern[1] = FloatToUNorm8(color.float32[1]);
					outPattern[2] = FloatToUNorm8(color.float32[0]);
					outPattern[3] = FloatToUNorm8(color.float32[3]);
					return true;
				case VK_FORMAT_R8G8B8A8_UINT:
				case VK_FORMAT_R8G8B8A8_SINT:
					outPattern[0] = static_cast<UInt8>(color.uint32[0] & 0xFFu);
					outPattern[1] = static_cast<UInt8>(color.uint32[1] & 0xFFu);
					outPattern[2] = static_cast<UInt8>(color.uint32[2] & 0xFFu);
					outPattern[3] = static_cast<UInt8>(color.uint32[3] & 0xFFu);
					return true;
				case VK_FORMAT_B8G8R8A8_UINT:
				case VK_FORMAT_B8G8R8A8_SINT:
					outPattern[0] = static_cast<UInt8>(color.uint32[2] & 0xFFu);
					outPattern[1] = static_cast<UInt8>(color.uint32[1] & 0xFFu);
					outPattern[2] = static_cast<UInt8>(color.uint32[0] & 0xFFu);
					outPattern[3] = static_cast<UInt8>(color.uint32[3] & 0xFFu);
					return true;
				default:
					return false;
			}
		}

		struct ScreenVertex
		{
			float m_x;
			float m_y;
			see::exec::VertexStageOutput m_output;
		};

		// 2D cross product of (B-A) and (P-A).
		float EdgeFunction(float ax, float ay, float bx, float by, float px, float py)
		{
			return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
		}

		// Framebuffer space is Y-down, which flips the visual CW/CCW sense relative to this formula's
		// algebraic sign - a positive area here is a visually clockwise triangle, hence the swap below.
		bool IsFrontFacing(float signedArea, VkFrontFace frontFace)
		{
			return frontFace == VK_FRONT_FACE_CLOCKWISE ? signedArea > 0.0f : signedArea < 0.0f;
		}

		bool IsCulled(bool frontFacing, VkCullModeFlags cullMode)
		{
			if (frontFacing && (cullMode & VK_CULL_MODE_FRONT_BIT))
				return true;
			if (!frontFacing && (cullMode & VK_CULL_MODE_BACK_BIT))
				return true;
			return false;
		}

		see::exec::Value InterpolateLocation(const see::exec::Value& a, const see::exec::Value& b, const see::exec::Value& c, float l0, float l1, float l2)
		{
			see::exec::Value result;
			result.m_scalar = a.m_scalar;
			result.m_rows = a.m_rows;
			for (UInt32 i = 0; i < a.m_rows; ++i)
				result.SetFloat(i, l0 * a.GetFloat(i) + l1 * b.GetFloat(i) + l2 * c.GetFloat(i));
			return result;
		}

		// Finds every Uniform/StorageBuffer Variable in `module` (via its DescriptorSet/Binding decorations),
		// resolves it against the currently bound descriptor sets, and maps the backing buffer memory so
		// see::exec can read it directly. Mapped DeviceMemory objects are appended to `outMappedMemories`
		// for the caller to Unmap once the draw call is done with them.
		// DescriptorSet/Binding decorations of a resource variable, if both are present.
		std::optional<std::pair<UInt32, UInt32>> FindDescriptorSetBinding(const see::ir::Module& module, UInt32 variableId)
		{
			auto decorationsIt = module.m_decorations.find(variableId);
			if (decorationsIt == module.m_decorations.end())
				return std::nullopt;

			std::optional<UInt32> descriptorSet;
			std::optional<UInt32> binding;
			for (const see::ir::Decoration& decoration : decorationsIt->second)
			{
				if (decoration.m_kind == SpvDecorationDescriptorSet && !decoration.m_literals.empty())
					descriptorSet = decoration.m_literals[0];
				else if (decoration.m_kind == SpvDecorationBinding && !decoration.m_literals.empty())
					binding = decoration.m_literals[0];
			}

			if (!descriptorSet || !binding)
				return std::nullopt;

			return std::make_pair(*descriptorSet, *binding);
		}

		std::unordered_map<UInt32, see::exec::ExternalBuffer> ResolveUniformBuffers(const see::ir::Module& module, const std::vector<vkd::DescriptorSet*>& boundSets, std::vector<vkd::DeviceMemory*>& outMappedMemories)
		{
			std::unordered_map<UInt32, see::exec::ExternalBuffer> result;

			for (const see::ir::Instruction& global : module.m_globalInstructions)
			{
				if (global.m_op != see::ir::Op::Variable || global.m_args.empty())
					continue;

				const auto storageClass = static_cast<SpvStorageClass>(global.m_args[0]);
				if (storageClass != SpvStorageClassUniform && storageClass != SpvStorageClassStorageBuffer)
					continue;

				std::optional<std::pair<UInt32, UInt32>> setBinding = FindDescriptorSetBinding(module, global.m_id);
				if (!setBinding || setBinding->first >= boundSets.size() || !boundSets[setBinding->first])
					continue;

				const vkd::DescriptorSet::BufferBinding* bufferBinding = boundSets[setBinding->first]->FindBufferBinding(setBinding->second);
				if (!bufferBinding || !bufferBinding->buffer || !bufferBinding->buffer->GetMemory())
					continue;

				vkd::Buffer* buffer = bufferBinding->buffer;
				const VkDeviceSize byteSize = bufferBinding->range == VK_WHOLE_SIZE ? (buffer->GetSize() - bufferBinding->offset) : bufferBinding->range;
				const VkDeviceSize absoluteOffset = buffer->GetMemoryOffset() + bufferBinding->offset;

				void* mapped = nullptr;
				if (buffer->GetMemory()->Map(absoluteOffset, byteSize, &mapped) != VK_SUCCESS || !mapped)
					continue;

				outMappedMemories.push_back(buffer->GetMemory());
				result[global.m_id] = see::exec::ExternalBuffer{reinterpret_cast<UInt32*>(mapped), static_cast<UInt32>(byteSize / sizeof(UInt32))};
			}

			return result;
		}

		std::unordered_map<UInt32, see::exec::ExternalImage> ResolveSampledImages(const see::ir::Module& module, const std::vector<vkd::DescriptorSet*>& boundSets, std::vector<vkd::DeviceMemory*>& outMappedMemories)
		{
			std::unordered_map<UInt32, see::exec::ExternalImage> result;

			for (const see::ir::Instruction& global : module.m_globalInstructions)
			{
				if (global.m_op != see::ir::Op::Variable || global.m_args.empty())
					continue;

				if (static_cast<SpvStorageClass>(global.m_args[0]) != SpvStorageClassUniformConstant)
					continue;

				std::optional<std::pair<UInt32, UInt32>> setBinding = FindDescriptorSetBinding(module, global.m_id);
				if (!setBinding || setBinding->first >= boundSets.size() || !boundSets[setBinding->first])
					continue;

				const vkd::DescriptorSet::ImageBinding* imageBinding = boundSets[setBinding->first]->FindImageBinding(setBinding->second);
				if (!imageBinding || !imageBinding->imageView)
					continue;

				VKD_FROM_HANDLE(vkd::Image, image, imageBinding->imageView->GetImage());
				if (!image || !image->GetMemory())
					continue;

				// Nearest-only, RGBA8-class formats only - matches the rasterizer's own color-attachment scope.
				if (vkuFormatElementSize(image->GetFormat()) != 4)
					continue;

				const VkExtent3D extent = image->GetExtent();
				const VkDeviceSize imageSize = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;

				void* mapped = nullptr;
				if (image->GetMemory()->Map(image->GetMemoryOffset(), imageSize, &mapped) != VK_SUCCESS || !mapped)
					continue;

				outMappedMemories.push_back(image->GetMemory());
				result[global.m_id] = see::exec::ExternalImage{reinterpret_cast<const cct::UInt8*>(mapped), extent.width, extent.height};
			}

			return result;
		}

		void RasterizeTriangle(cct::UByte* pixels, VkFormat format, const VkExtent3D& extent, const VkRect2D& scissor, const ScreenVertex& v0, const ScreenVertex& v1, const ScreenVertex& v2,
							   const see::ir::Module& fragmentModule, const see::ir::Function& fragmentFunction, VkCullModeFlags cullMode, VkFrontFace frontFace,
							   const std::unordered_map<UInt32, see::exec::ExternalBuffer>& fragmentUniforms,
							   const std::unordered_map<UInt32, see::exec::ExternalImage>& fragmentImages)
		{
			VKD_AUTO_PROFILER_SCOPE();
			const VkDeviceSize pixelSize = vkuFormatElementSize(format);
			if (pixelSize != 4)
				return;

			const float area = EdgeFunction(v0.m_x, v0.m_y, v1.m_x, v1.m_y, v2.m_x, v2.m_y);
			if (area == 0.0f)
				return;

			if (IsCulled(IsFrontFacing(area, frontFace), cullMode))
				return;

			const int minX = std::max({static_cast<int>(std::floor(std::min({v0.m_x, v1.m_x, v2.m_x}))), scissor.offset.x, 0});
			const int minY = std::max({static_cast<int>(std::floor(std::min({v0.m_y, v1.m_y, v2.m_y}))), scissor.offset.y, 0});
			const int maxX = std::min({static_cast<int>(std::ceil(std::max({v0.m_x, v1.m_x, v2.m_x}))), scissor.offset.x + static_cast<int>(scissor.extent.width), static_cast<int>(extent.width)});
			const int maxY = std::min({static_cast<int>(std::ceil(std::max({v0.m_y, v1.m_y, v2.m_y}))), scissor.offset.y + static_cast<int>(scissor.extent.height), static_cast<int>(extent.height)});

			const float absArea = std::fabs(area);

			for (int y = minY; y < maxY; ++y)
			{
				for (int x = minX; x < maxX; ++x)
				{
					const float px = static_cast<float>(x) + 0.5f;
					const float py = static_cast<float>(y) + 0.5f;

					float w0 = EdgeFunction(v1.m_x, v1.m_y, v2.m_x, v2.m_y, px, py);
					float w1 = EdgeFunction(v2.m_x, v2.m_y, v0.m_x, v0.m_y, px, py);
					float w2 = EdgeFunction(v0.m_x, v0.m_y, v1.m_x, v1.m_y, px, py);

					if (area < 0.0f)
					{
						w0 = -w0;
						w1 = -w1;
						w2 = -w2;
					}

					if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
						continue;

					const float l0 = w0 / absArea;
					const float l1 = w1 / absArea;
					const float l2 = w2 / absArea;

					std::unordered_map<UInt32, see::exec::Value> interpolated;
					for (const auto& [location, value] : v0.m_output.m_locations)
					{
						auto it1 = v1.m_output.m_locations.find(location);
						auto it2 = v2.m_output.m_locations.find(location);
						if (it1 == v1.m_output.m_locations.end() || it2 == v2.m_output.m_locations.end())
							continue;

						interpolated[location] = InterpolateLocation(value, it1->second, it2->second, l0, l1, l2);
					}

					std::optional<std::unordered_map<UInt32, see::exec::Value>> fragmentOutputs = see::exec::RunFragmentStage(fragmentModule, fragmentFunction, interpolated, fragmentUniforms, fragmentImages);
					if (!fragmentOutputs)
						continue;

					auto colorIt = fragmentOutputs->find(0);
					if (colorIt == fragmentOutputs->end())
						continue;

					const see::exec::Value& color = colorIt->second;
					VkClearColorValue clearValue{};
					clearValue.float32[0] = color.GetFloat(0);
					clearValue.float32[1] = color.m_rows > 1 ? color.GetFloat(1) : 0.0f;
					clearValue.float32[2] = color.m_rows > 2 ? color.GetFloat(2) : 0.0f;
					clearValue.float32[3] = color.m_rows > 3 ? color.GetFloat(3) : 1.0f;

					UInt8 pattern[4];
					if (!BuildRgba8ClearPattern(format, clearValue, pattern))
						continue;

					cct::UByte* pixel = pixels + (static_cast<std::size_t>(y) * extent.width + static_cast<std::size_t>(x)) * pixelSize;
					std::memcpy(pixel, pattern, sizeof(pattern));
				}
			}
		}
	} // namespace

	CpuContext::CpuContext()
	{
	}

	VkResult CpuContext::BindPipeline(OpBindPipeline op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.PipelineObject != nullptr, "Pipeline cannot be null");

		m_boundPipeline = op.PipelineObject;

		return VK_SUCCESS;
	}

	VkResult CpuContext::BindVertexBuffer(OpBindVertexBuffer op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(!op.Buffers.empty(), "No vertex buffers provided");
		CCT_ASSERT(!op.Offsets.empty(), "No offsets provided");
		CCT_ASSERT(op.Buffers.size() == op.Offsets.size(), "Buffers and offsets size mismatch");

		const UInt32 maxBinding = op.FirstBinding + static_cast<UInt32>(op.Buffers.size());
		if (maxBinding > m_boundVertexBuffers.size())
		{
			m_boundVertexBuffers.resize(maxBinding, nullptr);
			m_vertexBufferOffsets.resize(maxBinding, 0);
		}

		for (size_t i = 0; i < op.Buffers.size(); ++i)
		{
			const UInt32 binding = op.FirstBinding + static_cast<UInt32>(i);
			m_boundVertexBuffers[binding] = op.Buffers[i];
			m_vertexBufferOffsets[binding] = op.Offsets[i];
		}

		return VK_SUCCESS;
	}

	VkResult CpuContext::BindDescriptorSets(OpBindDescriptorSets op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		const UInt32 maxSet = op.FirstSet + static_cast<UInt32>(op.DescriptorSets.size());
		if (maxSet > m_boundDescriptorSets.size())
			m_boundDescriptorSets.resize(maxSet, nullptr);

		for (size_t i = 0; i < op.DescriptorSets.size(); ++i)
			m_boundDescriptorSets[op.FirstSet + i] = op.DescriptorSets[i];

		return VK_SUCCESS;
	}

	VkResult CpuContext::Draw(vkd::OpDraw op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		if (!m_boundPipeline || !m_currentRenderPass || !m_currentFramebuffer)
			return VK_SUCCESS;

		auto* pipeline = static_cast<Pipeline*>(m_boundPipeline);
		if (pipeline->GetTopology() != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST || pipeline->GetViewports().empty())
			return VK_SUCCESS;

		auto* device = static_cast<SoftwareDevice*>(pipeline->GetOwner());
		see::Executor& executor = device->GetShaderExecutor();

		const see::ShaderHandle vertexHandle = pipeline->GetShaderHandle(VK_SHADER_STAGE_VERTEX_BIT);
		const see::ShaderHandle fragmentHandle = pipeline->GetShaderHandle(VK_SHADER_STAGE_FRAGMENT_BIT);
		if (vertexHandle == see::InvalidShaderHandle || fragmentHandle == see::InvalidShaderHandle)
			return VK_SUCCESS;

		const see::ir::Module* vertexModule = executor.GetIrModule(vertexHandle);
		const see::ir::Function* vertexFunction = executor.GetEntryFunction(vertexHandle);
		const see::ir::Module* fragmentModule = executor.GetIrModule(fragmentHandle);
		const see::ir::Function* fragmentFunction = executor.GetEntryFunction(fragmentHandle);
		if (!vertexModule || !vertexFunction || !fragmentModule || !fragmentFunction)
			return VK_SUCCESS;

		if (pipeline->GetSubpass() >= m_currentRenderPass->GetSubpasses().size())
			return VK_SUCCESS;

		const VkSubpassDescription& subpass = m_currentRenderPass->GetSubpasses()[pipeline->GetSubpass()];
		if (subpass.colorAttachmentCount == 0)
			return VK_SUCCESS;

		const UInt32 attachmentIndex = subpass.pColorAttachments[0].attachment;
		if (attachmentIndex >= m_currentFramebuffer->GetAttachments().size())
			return VK_SUCCESS;

		VKD_FROM_HANDLE(vkd::ImageView, colorView, m_currentFramebuffer->GetAttachments()[attachmentIndex]);
		VKD_FROM_HANDLE(vkd::Image, colorImage, colorView->GetImage());
		CCT_ASSERT(colorImage && colorImage->GetMemory(), "Invalid color attachment");

		const VkFormat format = colorImage->GetFormat();
		const VkExtent3D extent = colorImage->GetExtent();
		if (vkuFormatElementSize(format) != 4)
		{
			cct::Logger::Warning("Draw: unsupported color attachment format {}", static_cast<int>(format));
			return VK_SUCCESS;
		}

		const VkDeviceSize imageSize = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
		cct::UByte* pixels = nullptr;
		VkResult mapResult = colorImage->GetMemory()->Map(0, imageSize, reinterpret_cast<void**>(&pixels));
		if (mapResult != VK_SUCCESS || pixels == nullptr)
			return mapResult != VK_SUCCESS ? mapResult : VK_ERROR_MEMORY_MAP_FAILED;

		const VkViewport& viewport = pipeline->GetViewports()[0];

		VkRect2D scissor;
		if (pipeline->GetScissors().empty())
			scissor = VkRect2D{{0, 0}, {extent.width, extent.height}};
		else
			scissor = pipeline->GetScissors()[0];

		std::vector<vkd::DeviceMemory*> mappedUniformMemories;
		const std::unordered_map<UInt32, see::exec::ExternalBuffer> vertexUniforms = ResolveUniformBuffers(*vertexModule, m_boundDescriptorSets, mappedUniformMemories);
		const std::unordered_map<UInt32, see::exec::ExternalBuffer> fragmentUniforms = ResolveUniformBuffers(*fragmentModule, m_boundDescriptorSets, mappedUniformMemories);
		const std::unordered_map<UInt32, see::exec::ExternalImage> vertexImages = ResolveSampledImages(*vertexModule, m_boundDescriptorSets, mappedUniformMemories);
		const std::unordered_map<UInt32, see::exec::ExternalImage> fragmentImages = ResolveSampledImages(*fragmentModule, m_boundDescriptorSets, mappedUniformMemories);

		for (UInt32 instance = 0; instance < op.InstanceCount; ++instance)
		{
			(void)instance; // no InstanceIndex builtin wired up yet - every instance draws identically

			for (UInt32 base = 0; base + 3 <= op.VertexCount; base += 3)
			{
				ScreenVertex screenVertices[3];
				bool allValid = true;

				for (UInt32 i = 0; i < 3; ++i)
				{
					const UInt32 vertexIndex = op.FirstVertex + base + i;
					std::optional<see::exec::VertexStageOutput> output = see::exec::RunVertexStage(*vertexModule, *vertexFunction, vertexIndex, vertexUniforms, vertexImages);
					if (!output)
					{
						allValid = false;
						break;
					}

					const float w = output->m_position.GetFloat(3);
					if (w == 0.0f)
					{
						allValid = false;
						break;
					}

					const float ndcX = output->m_position.GetFloat(0) / w;
					const float ndcY = output->m_position.GetFloat(1) / w;

					screenVertices[i].m_x = viewport.x + (ndcX * 0.5f + 0.5f) * viewport.width;
					screenVertices[i].m_y = viewport.y + (ndcY * 0.5f + 0.5f) * viewport.height;
					screenVertices[i].m_output = std::move(*output);
				}

				if (!allValid)
					continue;

				RasterizeTriangle(pixels, format, extent, scissor, screenVertices[0], screenVertices[1], screenVertices[2], *fragmentModule, *fragmentFunction, pipeline->GetCullMode(), pipeline->GetFrontFace(), fragmentUniforms, fragmentImages);
			}
		}

		for (vkd::DeviceMemory* memory : mappedUniformMemories)
			memory->Unmap();

		colorImage->GetMemory()->Unmap();
		return VK_SUCCESS;
	}

	VkResult CpuContext::CopyBuffer(vkd::Buffer::OpCopy op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		for (auto& region : op.regions)
		{
			CCT_ASSERT(op.src && op.src->GetMemory(), "Invalid pointer");
			CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

			cct::UByte* srcData = nullptr;
			op.src->GetMemory()->Map(region.srcOffset, region.size, reinterpret_cast<void**>(&srcData));

			cct::UByte* dstData = nullptr;
			op.dst->GetMemory()->Map(region.dstOffset, region.size, reinterpret_cast<void**>(&dstData));

			std::memcpy(dstData, srcData, region.size);

			op.dst->GetMemory()->Unmap();
			op.src->GetMemory()->Unmap();
		}

		return VK_SUCCESS;
	}

	VkResult CpuContext::CopyBuffer2(vkd::Buffer::OpCopy2 op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		for (auto& region : op.regions)
		{
			CCT_ASSERT(op.src && op.src->GetMemory(), "Invalid pointer");
			CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

			cct::UByte* srcData = nullptr;
			op.src->GetMemory()->Map(region.srcOffset, region.size, reinterpret_cast<void**>(&srcData));

			cct::UByte* dstData = nullptr;
			op.dst->GetMemory()->Map(region.dstOffset, region.size, reinterpret_cast<void**>(&dstData));

			std::memcpy(dstData, srcData, region.size);

			op.dst->GetMemory()->Unmap();
			op.src->GetMemory()->Unmap();
		}

		return VK_SUCCESS;
	}

	VkResult CpuContext::UpdateBuffer(vkd::Buffer::OpUpdate op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

		cct::UByte* dstData = nullptr;
		op.dst->GetMemory()->Map(op.offset, op.data.size(), reinterpret_cast<void**>(&dstData));

		std::memcpy(dstData, op.data.data(), op.data.size());

		op.dst->GetMemory()->Unmap();

		return VK_SUCCESS;
	}

	VkResult CpuContext::FillBuffer(vkd::Buffer::OpFill op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		cct::UByte* data = nullptr;
		op.dst->GetMemory()->Map(op.offset, op.size, reinterpret_cast<void**>(&data));

		UInt32* data32 = reinterpret_cast<UInt32*>(data);
		size_t count = op.size / sizeof(UInt32);
		for (size_t i = 0; i < count; ++i)
			data32[i] = op.data;

		op.dst->GetMemory()->Unmap();

		return VK_SUCCESS;
	}

	VkResult CpuContext::CopyImage(vkd::Image::OpCopy op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		for (auto& region : op.regions)
		{
			CCT_ASSERT(op.src && op.src->GetMemory(), "Invalid pointer");
			CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

			VkDeviceSize pixelSize = vkuFormatElementSize(op.src->GetFormat());
			VkDeviceSize srcRowPitch = op.src->GetExtent().width * pixelSize;
			VkDeviceSize dstRowPitch = op.dst->GetExtent().width * pixelSize;

			VkDeviceSize srcImageSize = srcRowPitch * op.src->GetExtent().height * op.src->GetExtent().depth;
			VkDeviceSize dstImageSize = dstRowPitch * op.dst->GetExtent().height * op.dst->GetExtent().depth;

			cct::UByte* srcBase = nullptr;
			op.src->GetMemory()->Map(0, srcImageSize, reinterpret_cast<void**>(&srcBase));

			cct::UByte* dstBase = nullptr;
			op.dst->GetMemory()->Map(0, dstImageSize, reinterpret_cast<void**>(&dstBase));

			VkDeviceSize rowSize = region.extent.width * pixelSize;
			for (UInt32 z = 0; z < region.extent.depth; ++z)
			{
				for (UInt32 y = 0; y < region.extent.height; ++y)
				{
					VkDeviceSize srcOffset = ((region.srcOffset.z + z) * op.src->GetExtent().height +
											  (region.srcOffset.y + y)) *
												 srcRowPitch +
											 region.srcOffset.x * pixelSize;
					VkDeviceSize dstOffset = ((region.dstOffset.z + z) *
												  op.dst->GetExtent().height +
											  (region.dstOffset.y + y)) *
												 dstRowPitch +
											 region.dstOffset.x * pixelSize;
					std::memcpy(dstBase + dstOffset, srcBase + srcOffset, rowSize);
				}
			}

			op.dst->GetMemory()->Unmap();
			op.src->GetMemory()->Unmap();
		}

		return VK_SUCCESS;
	}

	VkResult CpuContext::CopyBufferToImage(vkd::Buffer::OpCopyBufferToImage op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		for (auto& region : op.regions)
		{
			CCT_ASSERT(op.src && op.src->GetMemory(), "Invalid pointer");
			CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

			VkDeviceSize pixelSize = vkuFormatElementSize(op.dst->GetFormat());
			VkDeviceSize imageRowPitch = op.dst->GetExtent().width * pixelSize;
			VkDeviceSize imageSize = imageRowPitch * op.dst->GetExtent().height * op.dst->GetExtent().depth;

			UInt32 bufferRowLength = region.bufferRowLength ? region.bufferRowLength : region.imageExtent.width;
			VkDeviceSize bufferRowPitch = bufferRowLength * pixelSize;

			cct::UByte* srcBase = nullptr;
			op.src->GetMemory()->Map(region.bufferOffset,
									 bufferRowPitch * region.imageExtent.height * region.imageExtent.depth,
									 reinterpret_cast<void**>(&srcBase));

			cct::UByte* dstBase = nullptr;
			op.dst->GetMemory()->Map(0, imageSize, reinterpret_cast<void**>(&dstBase));

			VkDeviceSize rowSize = region.imageExtent.width * pixelSize;
			for (UInt32 z = 0; z < region.imageExtent.depth; ++z)
			{
				for (UInt32 y = 0; y < region.imageExtent.height; ++y)
				{
					VkDeviceSize srcOffset = (z * bufferRowLength * region.imageExtent.height + y * bufferRowLength) *
											 pixelSize;
					VkDeviceSize dstOffset = ((region.imageOffset.z + z) * op.dst->GetExtent().height +
											  (region.imageOffset.y + y)) *
												 imageRowPitch +
											 region.imageOffset.x * pixelSize;

					std::memcpy(dstBase + dstOffset, srcBase + srcOffset, rowSize);
				}
			}

			op.dst->GetMemory()->Unmap();
			op.src->GetMemory()->Unmap();
		}

		return VK_SUCCESS;
	}

	VkResult CpuContext::CopyImageToBuffer(vkd::Buffer::OpCopyImageToBuffer op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		for (auto& region : op.regions)
		{
			CCT_ASSERT(op.src && op.src->GetMemory(), "Invalid pointer");
			CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

			VkDeviceSize pixelSize = vkuFormatElementSize(op.src->GetFormat());
			VkDeviceSize imageRowPitch = op.src->GetExtent().width * pixelSize;
			VkDeviceSize imageSize = imageRowPitch * op.src->GetExtent().height * op.src->GetExtent().depth;

			UInt32 bufferRowLength = region.bufferRowLength ? region.bufferRowLength : region.imageExtent.width;
			VkDeviceSize bufferRowPitch = bufferRowLength * pixelSize;

			cct::UByte* srcBase = nullptr;
			op.src->GetMemory()->Map(0, imageSize, reinterpret_cast<void**>(&srcBase));

			cct::UByte* dstBase = nullptr;
			op.dst->GetMemory()->Map(region.bufferOffset,
									 bufferRowPitch * region.imageExtent.height * region.imageExtent.depth,
									 reinterpret_cast<void**>(&dstBase));

			VkDeviceSize rowSize = region.imageExtent.width * pixelSize;
			for (UInt32 z = 0; z < region.imageExtent.depth; ++z)
			{
				for (UInt32 y = 0; y < region.imageExtent.height; ++y)
				{
					VkDeviceSize srcOffset = ((region.imageOffset.z + z) * op.src->GetExtent().height +
											  (region.imageOffset.y + y)) *
												 imageRowPitch +
											 region.imageOffset.x * pixelSize;
					VkDeviceSize dstOffset = (z * bufferRowLength * region.imageExtent.height + y * bufferRowLength) *
											 pixelSize;

					std::memcpy(dstBase + dstOffset, srcBase + srcOffset, rowSize);
				}
			}

			op.dst->GetMemory()->Unmap();
			op.src->GetMemory()->Unmap();
		}

		return VK_SUCCESS;
	}

	VkResult CpuContext::ClearColorImage(vkd::Image::OpClearColorImage op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.image && op.image->GetMemory(), "Invalid pointer");

		for (auto& range : op.ranges)
		{
			(void)range;
			VkResult result = ClearImageWithColor(op.image, op.clearColor);
			if (result != VK_SUCCESS)
				return result;
		}

		return VK_SUCCESS;
	}

	VkResult CpuContext::BeginRenderPass(vkd::OpBeginRenderPass op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.RenderPassObject && op.FramebufferObject, "Invalid render pass / framebuffer");

		m_currentRenderPass = op.RenderPassObject;
		m_currentFramebuffer = op.FramebufferObject;
		m_renderArea = op.RenderArea;

		const auto& attachments = op.RenderPassObject->GetAttachments();
		const auto& views = op.FramebufferObject->GetAttachments();

		const std::size_t count = std::min(attachments.size(), views.size());
		for (std::size_t i = 0; i < count; ++i)
		{
			if (attachments[i].loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR)
				continue;

			if (i >= op.ClearValues.size())
			{
				cct::Logger::Warning("BeginRenderPass: missing clear value for attachment {}", i);
				continue;
			}

			VKD_FROM_HANDLE(vkd::ImageView, viewObj, views[i]);
			VKD_FROM_HANDLE(vkd::Image, imageObj, viewObj->GetImage());

			VkResult result = ClearImageWithColor(imageObj, op.ClearValues[i].color);
			if (result != VK_SUCCESS)
				return result;
		}

		return VK_SUCCESS;
	}

	VkResult CpuContext::EndRenderPass(vkd::OpEndRenderPass op)
	{
		VKD_AUTO_PROFILER_SCOPE();
		(void)op;

		// storeOp == STORE is implicit since we wrote directly to image memory.
		// storeOp == DONT_CARE could be optimized later; for now we keep the data.
		m_currentRenderPass = nullptr;
		m_currentFramebuffer = nullptr;
		m_renderArea = {};

		return VK_SUCCESS;
	}

	VkResult CpuContext::ClearImageWithColor(vkd::Image* image, const VkClearColorValue& color)
	{
		CCT_ASSERT(image && image->GetMemory(), "Invalid image / memory");

		const VkFormat format = image->GetFormat();
		const VkDeviceSize pixelSize = vkuFormatElementSize(format);
		if (pixelSize == 0)
			return VK_ERROR_FORMAT_NOT_SUPPORTED;

		const VkExtent3D extent = image->GetExtent();
		const VkDeviceSize imageSize = static_cast<VkDeviceSize>(extent.width) * extent.height * extent.depth * pixelSize;

		cct::UByte* data = nullptr;
		VkResult mapResult = image->GetMemory()->Map(0, imageSize, reinterpret_cast<void**>(&data));
		if (mapResult != VK_SUCCESS || data == nullptr)
			return mapResult != VK_SUCCESS ? mapResult : VK_ERROR_MEMORY_MAP_FAILED;

		UInt8 pattern[4] = {0, 0, 0, 0};
		const bool supported = pixelSize == 4 && BuildRgba8ClearPattern(format, color, pattern);

		if (supported)
		{
			UInt32 clearWord;
			std::memcpy(&clearWord, pattern, sizeof(clearWord));
			UInt32* data32 = reinterpret_cast<UInt32*>(data);
			const std::size_t wordCount = static_cast<std::size_t>(imageSize / sizeof(UInt32));
			for (std::size_t i = 0; i < wordCount; ++i)
				data32[i] = clearWord;
		}
		else
		{
			cct::Logger::Warning("ClearImageWithColor: unsupported format {}, zeroing memory", static_cast<int>(format));
			std::memset(data, 0, static_cast<std::size_t>(imageSize));
		}

		image->GetMemory()->Unmap();
		return VK_SUCCESS;
	}
} // namespace vkd::software
