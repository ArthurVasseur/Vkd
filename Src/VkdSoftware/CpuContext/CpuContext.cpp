/**
 * @file CpuContext.cpp
 * @brief Implementation of CPU rendering context
 * @date 2025-10-27
 */

#include "VkdSoftware/CpuContext/CpuContext.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <future>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Vkd/Buffer/Buffer.hpp"
#include "Vkd/DescriptorSet/DescriptorSet.hpp"
#include "Vkd/DeviceMemory/DeviceMemory.hpp"
#include "Vkd/Image/ColorPacking.hpp"
#include "Vkd/Image/CopyLayout.hpp"
#include "Vkd/ImageView/ImageView.hpp"
#include "Vkd/QueryPool/QueryPool.hpp"
#include "VkdSoftware/Device/Device.hpp"
#include "VkdSoftware/Pipeline/Pipeline.hpp"

#include "See/Exec/Stage.hpp"
#include <spirv/unified1/spirv.h>
#include <vulkan/utility/vk_format_utils.h>

namespace vkd::software
{
	namespace
	{
		struct ScreenVertex
		{
			float m_x;
			float m_y;
			float m_invW; // 1/w (clip-space), for perspective-correct attribute interpolation
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
			// Integer-typed fragment inputs must be decorated Flat (SPIR-V requires it), meaning they
			// take the provoking vertex's (v0's, under Vulkan's default first-vertex convention) value
			// verbatim rather than being blended - blending would reinterpret their bit pattern as float.
			if (a.m_scalar != see::ir::ScalarKind::Float32)
				return a;

			see::exec::Value result;
			result.m_scalar = a.m_scalar;
			result.m_rows = a.m_rows;
			result.m_columns = a.m_columns;
			result.m_structType = a.m_structType;
			const UInt32 wordCount = a.WordCount();
			for (UInt32 i = 0; i < wordCount; ++i)
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

		std::unordered_map<UInt32, see::exec::ExternalBuffer> ResolveUniformBuffers(const see::ir::Module& module, const vkd::vector<vkd::DescriptorSet*>& boundSets, const std::unordered_map<UInt64, UInt32>& dynamicOffsets, vkd::vector<vkd::DeviceMemory*>& outMappedMemories)
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

				VkDeviceSize offset = bufferBinding->offset;
				const UInt64 dynamicKey = (static_cast<UInt64>(setBinding->first) << 32) | setBinding->second;
				auto dynamicIt = dynamicOffsets.find(dynamicKey);
				if (dynamicIt != dynamicOffsets.end())
					offset += dynamicIt->second;

				const VkDeviceSize byteSize = bufferBinding->range == VK_WHOLE_SIZE ? (buffer->GetSize() - offset) : bufferBinding->range;
				const VkDeviceSize absoluteOffset = buffer->GetMemoryOffset() + offset;

				void* mapped = nullptr;
				if (buffer->GetMemory()->Map(absoluteOffset, byteSize, &mapped) != VK_SUCCESS || !mapped)
					continue;

				outMappedMemories.push_back(buffer->GetMemory());
				result[global.m_id] = see::exec::ExternalBuffer{reinterpret_cast<UInt32*>(mapped), static_cast<UInt32>(byteSize / sizeof(UInt32))};
			}

			return result;
		}

		std::unordered_map<UInt32, see::exec::ExternalImage> ResolveSampledImages(const see::ir::Module& module, const vkd::vector<vkd::DescriptorSet*>& boundSets, vkd::vector<vkd::DeviceMemory*>& outMappedMemories)
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

		see::exec::Value UnpackVertexAttribute(VkFormat format, const UInt8* bytes)
		{
			see::exec::Value result;
			result.m_rows = 4;

			if (vkuFormatIsCompressed(format) || vkuFormatIsMultiplane(format) || vkuFormatIsDepthOrStencil(format))
				return result;

			const bool isSfloat = vkuFormatIsSFLOAT(format);
			const bool isUnorm = vkuFormatIsUNORM(format) || vkuFormatIsSRGB(format);
			const bool isSnorm = vkuFormatIsSNORM(format);
			const bool isUint = vkuFormatIsUINT(format);
			const bool isSint = vkuFormatIsSINT(format);
			if (!isSfloat && !isUnorm && !isSnorm && !isUint && !isSint)
				return result;

			result.m_scalar = isUint || isSint ? see::ir::ScalarKind::Int32 : see::ir::ScalarKind::Float32;

			// Vulkan expands a vertex attribute to (R,G,B,A); components the format doesn't provide
			// default to 0 except alpha, which defaults to 1 - independent of the format's component count.
			if (result.m_scalar == see::ir::ScalarKind::Int32)
				result.SetInt32(3, 1);
			else
				result.SetFloat(3, 1.0f);

			const VKU_FORMAT_INFO info = vkuGetFormatInfo(format);
			const UInt32 componentCount = std::min<UInt32>(info.component_count, 4);

			std::size_t byteOffset = 0;
			for (UInt32 i = 0; i < componentCount; ++i)
			{
				const UInt32 bits = info.components[i].size;
				const UInt32 byteCount = bits / 8;

				if (isSfloat)
				{
					if (byteCount == 4)
					{
						float value;
						std::memcpy(&value, bytes + byteOffset, 4);
						result.SetFloat(i, value);
					}
					else
					{
						UInt16 half = 0;
						std::memcpy(&half, bytes + byteOffset, 2);
						result.SetFloat(i, HalfToFloat(half));
					}
				}
				else if (isUnorm)
				{
					UInt64 raw = 0;
					std::memcpy(&raw, bytes + byteOffset, byteCount);
					result.SetFloat(i, static_cast<float>(raw) / static_cast<float>(UnsignedMaxForBits(bits)));
				}
				else if (isSnorm)
				{
					Int64 raw = 0;
					std::memcpy(&raw, bytes + byteOffset, byteCount);
					const UInt32 shift = 64 - bits;
					raw = (raw << shift) >> shift; // sign-extend
					result.SetFloat(i, std::max(static_cast<float>(raw) / static_cast<float>(SignedMaxForBits(bits)), -1.0f));
				}
				else if (isUint)
				{
					UInt64 raw = 0;
					std::memcpy(&raw, bytes + byteOffset, byteCount);
					result.SetInt32(i, static_cast<Int32>(raw));
				}
				else
				{
					Int64 raw = 0;
					std::memcpy(&raw, bytes + byteOffset, byteCount);
					const UInt32 shift = 64 - bits;
					raw = (raw << shift) >> shift; // sign-extend
					result.SetInt32(i, static_cast<Int32>(raw));
				}

				byteOffset += byteCount;
			}

			return result;
		}

		struct MappedVertexBuffer
		{
			const UInt8* m_base = nullptr;
			VkDeviceSize m_size = 0;
		};

		std::vector<MappedVertexBuffer> MapVertexBuffers(const std::vector<Buffer*>& boundVertexBuffers, const std::vector<VkDeviceSize>& vertexBufferOffsets,
														 std::vector<vkd::DeviceMemory*>& outMappedMemories)
		{
			std::vector<MappedVertexBuffer> result(boundVertexBuffers.size());
			for (std::size_t binding = 0; binding < boundVertexBuffers.size(); ++binding)
			{
				vkd::Buffer* buffer = boundVertexBuffers[binding];
				if (!buffer || !buffer->GetMemory())
					continue;

				const VkDeviceSize offset = binding < vertexBufferOffsets.size() ? vertexBufferOffsets[binding] : 0;
				const VkDeviceSize size = buffer->GetSize() - offset;
				const VkDeviceSize absoluteOffset = buffer->GetMemoryOffset() + offset;

				void* mapped = nullptr;
				if (buffer->GetMemory()->Map(absoluteOffset, size, &mapped) != VK_SUCCESS || !mapped)
					continue;

				outMappedMemories.push_back(buffer->GetMemory());
				result[binding] = MappedVertexBuffer{reinterpret_cast<const UInt8*>(mapped), size};
			}
			return result;
		}

		std::unordered_map<UInt32, see::exec::Value> FetchVertexAttributes(const Pipeline& pipeline, const std::vector<MappedVertexBuffer>& mappedVertexBuffers,
																		   UInt32 vertexIndex, UInt32 instance)
		{
			std::unordered_map<UInt32, see::exec::Value> result;

			for (const VkVertexInputAttributeDescription& attribute : pipeline.GetVertexAttributes())
			{
				const auto& bindings = pipeline.GetVertexBindings();
				const auto bindingIt = std::find_if(bindings.begin(), bindings.end(),
													[&](const VkVertexInputBindingDescription& binding)
													{ return binding.binding == attribute.binding; });
				if (bindingIt == bindings.end() || attribute.binding >= mappedVertexBuffers.size())
					continue;

				const MappedVertexBuffer& mapped = mappedVertexBuffers[attribute.binding];
				if (!mapped.m_base)
					continue;

				const UInt32 elementIndex = bindingIt->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE ? instance : vertexIndex;
				const VkDeviceSize elementOffset = static_cast<VkDeviceSize>(elementIndex) * bindingIt->stride + attribute.offset;
				const VkDeviceSize attributeSize = vkuFormatElementSize(attribute.format);
				if (elementOffset + attributeSize > mapped.m_size)
					continue;

				see::exec::CopyValue(result[attribute.location], UnpackVertexAttribute(attribute.format, mapped.m_base + elementOffset));
			}

			return result;
		}

		// Everything a triangle needs from the draw that doesn't vary from one triangle to the next.
		struct RasterContext
		{
			cct::UByte* m_pixels;
			VkFormat m_format;
			VkExtent3D m_extent;
			VkRect2D m_scissor;
			VkCullModeFlags m_cullMode;
			VkFrontFace m_frontFace;
			const see::ir::Module* m_fragmentModule;
			const see::ir::Function* m_fragmentFunction;
			const see::exec::StageLocations* m_fragmentLocations;
			const see::exec::PreparedModule* m_fragmentPrepared;
			const std::unordered_map<UInt32, see::exec::ExternalBuffer>* m_fragmentUniforms;
			const std::unordered_map<UInt32, see::exec::ExternalImage>* m_fragmentImages;
			cct::ThreadPool* m_rasterPool;
		};

		// Below this, spreading the rows costs more than running them here.
		constexpr int MinParallelRasterPixels = 256;

		void RasterizeTriangle(const RasterContext& context, const ScreenVertex& v0, const ScreenVertex& v1, const ScreenVertex& v2)
		{
			VKD_AUTO_PROFILER_SCOPE();
			const VkDeviceSize pixelSize = vkuFormatElementSize(context.m_format);
			if (!IsSupportedColorFormat(context.m_format))
				return;

			const float area = EdgeFunction(v0.m_x, v0.m_y, v1.m_x, v1.m_y, v2.m_x, v2.m_y);
			if (area == 0.0f)
				return;

			if (IsCulled(IsFrontFacing(area, context.m_frontFace), context.m_cullMode))
				return;

			const VkRect2D& scissor = context.m_scissor;
			const int minX = std::max({static_cast<int>(std::floor(std::min({v0.m_x, v1.m_x, v2.m_x}))), scissor.offset.x, 0});
			const int minY = std::max({static_cast<int>(std::floor(std::min({v0.m_y, v1.m_y, v2.m_y}))), scissor.offset.y, 0});
			const int maxX = std::min({static_cast<int>(std::ceil(std::max({v0.m_x, v1.m_x, v2.m_x}))), scissor.offset.x + static_cast<int>(scissor.extent.width), static_cast<int>(context.m_extent.width)});
			const int maxY = std::min({static_cast<int>(std::ceil(std::max({v0.m_y, v1.m_y, v2.m_y}))), scissor.offset.y + static_cast<int>(scissor.extent.height), static_cast<int>(context.m_extent.height)});
			if (minX >= maxX || minY >= maxY)
				return;

			const float absArea = std::fabs(area);

			// `interpolated` is reused across pixels: its location set comes from the three vertices, not from the pixel.
			auto rasterizeRow = [&](int y, std::unordered_map<UInt32, see::exec::Value>& interpolated, see::exec::ExecutionStorage& storage)
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

					// Perspective-correct: screen-space barycentric weights interpolate attributes
					// linearly in screen space, which is only correct when w is constant across the
					// triangle. Re-weighting by 1/w and renormalizing corrects for that; it's a no-op
					// when all three w match (e.g. w=1 everywhere, as when there's no projection).
					const float pw0 = l0 * v0.m_invW;
					const float pw1 = l1 * v1.m_invW;
					const float pw2 = l2 * v2.m_invW;
					const float invPwSum = 1.0f / (pw0 + pw1 + pw2);
					const float pl0 = pw0 * invPwSum;
					const float pl1 = pw1 * invPwSum;
					const float pl2 = pw2 * invPwSum;

					for (const auto& [location, value] : v0.m_output.m_locations)
					{
						auto it1 = v1.m_output.m_locations.find(location);
						auto it2 = v2.m_output.m_locations.find(location);
						if (it1 == v1.m_output.m_locations.end() || it2 == v2.m_output.m_locations.end())
							continue;

						see::exec::CopyValue(interpolated[location], InterpolateLocation(value, it1->second, it2->second, pl0, pl1, pl2));
					}

					std::optional<std::unordered_map<UInt32, see::exec::Value>> fragmentOutputs =
						see::exec::RunFragmentStage(*context.m_fragmentModule, *context.m_fragmentFunction, *context.m_fragmentLocations, interpolated, *context.m_fragmentUniforms,
													*context.m_fragmentImages, *context.m_fragmentPrepared, storage);
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

					UInt8 pattern[32];
					PackColor(context.m_format, clearValue, pattern);

					cct::UByte* pixel = context.m_pixels + (static_cast<std::size_t>(y) * context.m_extent.width + static_cast<std::size_t>(x)) * pixelSize;
					std::memcpy(pixel, pattern, pixelSize);
				}
			};

			const int rowCount = maxY - minY;
			const std::size_t workerCount = std::min<std::size_t>(context.m_rasterPool->GetWorkerCount(), static_cast<std::size_t>(rowCount));
			if (workerCount <= 1 || rowCount * (maxX - minX) < MinParallelRasterPixels)
			{
				std::unordered_map<UInt32, see::exec::Value> interpolated;
				see::exec::ExecutionStorage storage;
				for (int y = minY; y < maxY; ++y)
					rasterizeRow(y, interpolated, storage);
				return;
			}

			// Rows are handed out one at a time rather than in equal bands: a triangle's rows don't cost the same.
			std::atomic<int> nextRow{minY};
			auto consumeRows = [&]()
			{
				std::unordered_map<UInt32, see::exec::Value> interpolated;
				see::exec::ExecutionStorage storage;
				for (int y = nextRow.fetch_add(1, std::memory_order_relaxed); y < maxY; y = nextRow.fetch_add(1, std::memory_order_relaxed))
					rasterizeRow(y, interpolated, storage);
			};

			std::vector<std::future<void>> pendingRows;
			pendingRows.reserve(workerCount - 1);
			for (std::size_t worker = 1; worker < workerCount; ++worker)
				pendingRows.push_back(context.m_rasterPool->Submit(consumeRows));

			// The calling thread takes a share too, so a stopped or saturated pool only costs time.
			consumeRows();

			for (std::future<void>& pendingRow : pendingRows)
				pendingRow.wait();
		}

		VkResult MapRange(vkd::DeviceMemory* memory, VkDeviceSize offset, VkDeviceSize size, cct::UByte** outData)
		{
			CCT_ASSERT(memory, "Invalid memory");

			*outData = nullptr;
			const VkResult result = memory->Map(offset, size, reinterpret_cast<void**>(outData));
			if (result != VK_SUCCESS)
				return result;
			if (*outData == nullptr)
				return VK_ERROR_MEMORY_MAP_FAILED;

			return VK_SUCCESS;
		}

		// Maps two memories whole (they may be the same one) for the row helpers of
		// Vkd/Image/CopyLayout.hpp, whose offsets are relative to the start of the memory, and runs
		// fn(srcBase, srcSize, dstBase, dstSize).
		template<typename Fn>
		VkResult WithMappedMemories(vkd::DeviceMemory* src, vkd::DeviceMemory* dst, Fn&& fn)
		{
			CCT_ASSERT(src && dst, "Invalid memory");
			cct::UByte* srcBase = nullptr;
			VkResult result = MapRange(src, 0, VK_WHOLE_SIZE, &srcBase);
			if (result != VK_SUCCESS)
				return result;
			cct::UByte* dstBase = srcBase;
			if (dst != src)
			{
				result = MapRange(dst, 0, VK_WHOLE_SIZE, &dstBase);
				if (result != VK_SUCCESS)
				{
					src->Unmap();
					return result;
				}
			}
			result = fn(srcBase, src->GetSize(), dstBase, dst->GetSize());
			if (dst != src)
				dst->Unmap();
			src->Unmap();
			return result;
		}

		// Row mover for CopyLayout.hpp: memmove (src and dst may share a memory), out-of-range rows
		// are dropped and reported.
		struct RowCopier
		{
			cct::UByte* srcBase;
			VkDeviceSize srcSize;
			cct::UByte* dstBase;
			VkDeviceSize dstSize;
			bool inRange = true;

			void operator()(VkDeviceSize srcOffset, VkDeviceSize dstOffset, VkDeviceSize bytes)
			{
				if (srcOffset + bytes > srcSize || dstOffset + bytes > dstSize)
				{
					inRange = false;
					return;
				}
				std::memmove(dstBase + dstOffset, srcBase + srcOffset, static_cast<std::size_t>(bytes));
			}

			[[nodiscard]] VkResult Result(bool supported) const
			{
				if (!supported)
					return VK_ERROR_FORMAT_NOT_SUPPORTED;
				return inRange ? VK_SUCCESS : VK_ERROR_UNKNOWN;
			}
		};

		// Writes a texel over a rectangle of z slices of one subresource, only the bytes set in
		// writeMask (a one-aspect clear of a combined depth/stencil format keeps the other aspect).
		bool FillTexels(cct::UByte* base, VkDeviceSize memorySize, const SubresourceFootprint& fp, VkRect2D rect,
						UInt32 firstSlice, UInt32 sliceCount, const UInt8* texel, const UInt8* writeMask, UInt32 texelSize)
		{
			const Int64 x0 = std::max<Int64>(rect.offset.x, 0);
			const Int64 y0 = std::max<Int64>(rect.offset.y, 0);
			const Int64 x1 = std::min<Int64>(Int64{rect.offset.x} + rect.extent.width, fp.extent.width);
			const Int64 y1 = std::min<Int64>(Int64{rect.offset.y} + rect.extent.height, fp.extent.height);
			bool fullMask = true;
			for (UInt32 i = 0; i < texelSize; ++i)
				fullMask &= writeMask[i] == 0xFF;

			for (UInt32 z = firstSlice; z < firstSlice + sliceCount && z < fp.extent.depth; ++z)
			{
				for (Int64 y = y0; y < y1; ++y)
				{
					const VkDeviceSize row = ImageTexelOffset(fp, static_cast<UInt32>(x0), static_cast<UInt32>(y), z, texelSize);
					if (x1 > x0 && row + static_cast<VkDeviceSize>(x1 - x0) * texelSize > memorySize)
						return false;
					for (Int64 x = x0; x < x1; ++x)
					{
						cct::UByte* dst = base + row + static_cast<VkDeviceSize>(x - x0) * texelSize;
						if (fullMask)
							std::memcpy(dst, texel, texelSize);
						else
						{
							for (UInt32 i = 0; i < texelSize; ++i)
								dst[i] = static_cast<cct::UByte>((dst[i] & ~writeMask[i]) | (texel[i] & writeMask[i]));
						}
					}
				}
			}
			return true;
		}
	} // namespace

	CpuContext::CpuContext(const VkAllocationCallbacks& allocationCallbacks) :
		m_allocationCallbacks(&allocationCallbacks),
		m_boundVertexBuffers(CommandAllocator<Buffer*>(allocationCallbacks)),
		m_vertexBufferOffsets(CommandAllocator<VkDeviceSize>(allocationCallbacks)),
		m_boundDescriptorSets(CommandAllocator<vkd::DescriptorSet*>(allocationCallbacks)),
		m_dynamicViewports(CommandAllocator<VkViewport>(allocationCallbacks)),
		m_dynamicScissors(CommandAllocator<VkRect2D>(allocationCallbacks))
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

	VkResult CpuContext::BindIndexBuffer(OpBindIndexBuffer op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		m_boundIndexBuffer = op.BufferHandle;
		m_indexBufferOffset = op.Offset;
		m_indexType = op.IndexType;

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

		for (const DynamicOffsetEntry& entry : op.DynamicOffsets)
		{
			const UInt32 set = op.FirstSet + entry.SetIndex;
			const UInt64 key = (static_cast<UInt64>(set) << 32) | entry.Binding;
			m_dynamicOffsets[key] = entry.Offset;
		}

		return VK_SUCCESS;
	}

	VkResult CpuContext::SetViewport(OpSetViewport op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		const UInt32 maxViewport = op.FirstViewport + static_cast<UInt32>(op.Viewports.size());
		if (maxViewport > m_dynamicViewports.size())
			m_dynamicViewports.resize(maxViewport);

		for (size_t i = 0; i < op.Viewports.size(); ++i)
			m_dynamicViewports[op.FirstViewport + i] = op.Viewports[i];

		return VK_SUCCESS;
	}

	VkResult CpuContext::SetScissor(OpSetScissor op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		const UInt32 maxScissor = op.FirstScissor + static_cast<UInt32>(op.Scissors.size());
		if (maxScissor > m_dynamicScissors.size())
			m_dynamicScissors.resize(maxScissor);

		for (size_t i = 0; i < op.Scissors.size(); ++i)
			m_dynamicScissors[op.FirstScissor + i] = op.Scissors[i];

		return VK_SUCCESS;
	}

	VkResult CpuContext::Draw(vkd::OpDraw op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		return DrawPrimitives(op.VertexCount, op.InstanceCount, op.FirstInstance,
							  [firstVertex = op.FirstVertex](UInt32 primitiveIndex)
							  { return firstVertex + primitiveIndex; });
	}

	VkResult CpuContext::DrawIndexed(vkd::OpDrawIndexed op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		if (!m_boundIndexBuffer || !m_boundIndexBuffer->GetMemory())
			return VK_SUCCESS;

		const VkDeviceSize indexSize = m_indexType == VK_INDEX_TYPE_UINT16 ? sizeof(UInt16) : sizeof(UInt32);
		const VkDeviceSize indexBufferSize = m_boundIndexBuffer->GetSize() - m_indexBufferOffset;
		const VkDeviceSize absoluteOffset = m_boundIndexBuffer->GetMemoryOffset() + m_indexBufferOffset;

		void* mapped = nullptr;
		if (m_boundIndexBuffer->GetMemory()->Map(absoluteOffset, indexBufferSize, &mapped) != VK_SUCCESS || !mapped)
			return VK_SUCCESS;

		const auto* indexBytes = reinterpret_cast<const UInt8*>(mapped);
		const bool isUint16 = m_indexType == VK_INDEX_TYPE_UINT16;

		const VkResult result = DrawPrimitives(op.IndexCount, op.InstanceCount, op.FirstInstance,
											   [op, indexBytes, indexBufferSize, indexSize, isUint16](UInt32 primitiveIndex) -> UInt32
											   {
												   const VkDeviceSize elementOffset = static_cast<VkDeviceSize>(op.FirstIndex + primitiveIndex) * indexSize;
												   if (elementOffset + indexSize > indexBufferSize)
													   return 0;

												   const UInt32 rawIndex = isUint16 ? *reinterpret_cast<const UInt16*>(indexBytes + elementOffset) : *reinterpret_cast<const UInt32*>(indexBytes + elementOffset);
												   return static_cast<UInt32>(static_cast<Int64>(rawIndex) + op.VertexOffset);
											   });

		m_boundIndexBuffer->GetMemory()->Unmap();
		return result;
	}

	VkResult CpuContext::DrawPrimitives(UInt32 vertexCount, UInt32 instanceCount, UInt32 firstInstance, const std::function<UInt32(UInt32)>& resolveVertexIndex)
	{
		if (!m_boundPipeline || !m_currentRenderPass || !m_currentFramebuffer)
			return VK_SUCCESS;

		auto* pipeline = static_cast<Pipeline*>(m_boundPipeline);
		if (pipeline->GetTopology() != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			return VK_SUCCESS;

		const bool viewportDynamic = pipeline->IsViewportDynamic();
		if (viewportDynamic ? m_dynamicViewports.empty() : pipeline->GetViewports().empty())
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
		const VkResult mapResult = MapRange(colorImage->GetMemory(), colorImage->GetMemoryOffset(), imageSize, &pixels);
		if (mapResult != VK_SUCCESS)
			return mapResult;

		const VkViewport& viewport = viewportDynamic ? m_dynamicViewports[0] : pipeline->GetViewports()[0];

		VkRect2D scissor;
		if (pipeline->IsScissorDynamic())
		{
			if (m_dynamicScissors.empty())
				return VK_SUCCESS;
			scissor = m_dynamicScissors[0];
		}
		else if (pipeline->GetScissors().empty())
			scissor = VkRect2D{{0, 0}, {extent.width, extent.height}};
		else
			scissor = pipeline->GetScissors()[0];

		std::vector<vkd::DeviceMemory*> mappedMemories;
		const std::unordered_map<UInt32, see::exec::ExternalBuffer> vertexUniforms = ResolveUniformBuffers(*vertexModule, m_boundDescriptorSets, mappedMemories);
		const std::unordered_map<UInt32, see::exec::ExternalBuffer> fragmentUniforms = ResolveUniformBuffers(*fragmentModule, m_boundDescriptorSets, mappedMemories);
		const std::unordered_map<UInt32, see::exec::ExternalImage> vertexImages = ResolveSampledImages(*vertexModule, m_boundDescriptorSets, mappedMemories);
		const std::unordered_map<UInt32, see::exec::ExternalImage> fragmentImages = ResolveSampledImages(*fragmentModule, m_boundDescriptorSets, mappedMemories);
		const std::vector<MappedVertexBuffer> mappedVertexBuffers = MapVertexBuffers(m_boundVertexBuffers, m_vertexBufferOffsets, mappedMemories);
		const see::exec::StageLocations fragmentLocations = see::exec::BuildStageLocations(*fragmentModule);
		see::exec::ExecutionStorage vertexStorage;
		const see::exec::PreparedModule vertexPrepared = see::exec::BuildPreparedModule(*vertexModule);
		const see::exec::PreparedModule fragmentPrepared = see::exec::BuildPreparedModule(*fragmentModule);

		const RasterContext rasterContext{.m_pixels = pixels,
										  .m_format = format,
										  .m_extent = extent,
										  .m_scissor = scissor,
										  .m_cullMode = pipeline->GetCullMode(),
										  .m_frontFace = pipeline->GetFrontFace(),
										  .m_fragmentModule = fragmentModule,
										  .m_fragmentFunction = fragmentFunction,
										  .m_fragmentLocations = &fragmentLocations,
										  .m_fragmentPrepared = &fragmentPrepared,
										  .m_fragmentUniforms = &fragmentUniforms,
										  .m_fragmentImages = &fragmentImages,
										  .m_rasterPool = &device->GetRasterThreadPool()};

		for (UInt32 instance = 0; instance < instanceCount; ++instance)
		{
			const UInt32 instanceIndex = firstInstance + instance;

			for (UInt32 base = 0; base + 3 <= vertexCount; base += 3)
			{
				ScreenVertex screenVertices[3];
				bool allValid = true;

				for (UInt32 i = 0; i < 3; ++i)
				{
					const UInt32 vertexIndex = resolveVertexIndex(base + i);
					const std::unordered_map<UInt32, see::exec::Value> vertexAttributes = FetchVertexAttributes(*pipeline, mappedVertexBuffers, vertexIndex, instance);
					std::optional<see::exec::VertexStageOutput> output =
						see::exec::RunVertexStage(*vertexModule, *vertexFunction, vertexIndex, instanceIndex, vertexAttributes, vertexUniforms, vertexImages, vertexPrepared, vertexStorage);
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
					screenVertices[i].m_invW = 1.0f / w;
					see::exec::CopyValue(screenVertices[i].m_output.m_position, output->m_position);
					screenVertices[i].m_output.m_locations = std::move(output->m_locations);
				}

				if (!allValid)
					continue;

				RasterizeTriangle(rasterContext, screenVertices[0], screenVertices[1], screenVertices[2]);
			}
		}

		for (vkd::DeviceMemory* memory : mappedMemories)
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
			VkResult result = MapRange(op.src->GetMemory(), op.src->GetMemoryOffset() + region.srcOffset, region.size, &srcData);
			if (result != VK_SUCCESS)
				return result;

			cct::UByte* dstData = nullptr;
			result = MapRange(op.dst->GetMemory(), op.dst->GetMemoryOffset() + region.dstOffset, region.size, &dstData);
			if (result != VK_SUCCESS)
			{
				op.src->GetMemory()->Unmap();
				return result;
			}

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
			VkResult result = MapRange(op.src->GetMemory(), op.src->GetMemoryOffset() + region.srcOffset, region.size, &srcData);
			if (result != VK_SUCCESS)
				return result;

			cct::UByte* dstData = nullptr;
			result = MapRange(op.dst->GetMemory(), op.dst->GetMemoryOffset() + region.dstOffset, region.size, &dstData);
			if (result != VK_SUCCESS)
			{
				op.src->GetMemory()->Unmap();
				return result;
			}

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
		const VkResult result = MapRange(op.dst->GetMemory(), op.dst->GetMemoryOffset() + op.offset, op.data.size(), &dstData);
		if (result != VK_SUCCESS)
			return result;

		std::memcpy(dstData, op.data.data(), op.data.size());

		op.dst->GetMemory()->Unmap();

		return VK_SUCCESS;
	}

	VkResult CpuContext::FillBuffer(vkd::Buffer::OpFill op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

		if (op.offset >= op.dst->GetSize())
			return VK_SUCCESS;

		// VK_WHOLE_SIZE fills to the end of the buffer, and either way the range is truncated to whole UInt32s.
		const VkDeviceSize available = op.dst->GetSize() - op.offset;
		const VkDeviceSize size = std::min(op.size == VK_WHOLE_SIZE ? available : op.size, available) & ~static_cast<VkDeviceSize>(3);
		if (size == 0)
			return VK_SUCCESS;

		cct::UByte* data = nullptr;
		const VkResult mapResult = op.dst->GetMemory()->Map(op.dst->GetMemoryOffset() + op.offset, size, reinterpret_cast<void**>(&data));
		if (mapResult != VK_SUCCESS || data == nullptr)
			return mapResult != VK_SUCCESS ? mapResult : VK_ERROR_MEMORY_MAP_FAILED;

		UInt32* data32 = reinterpret_cast<UInt32*>(data);
		const std::size_t count = size / sizeof(UInt32);
		for (std::size_t i = 0; i < count; ++i)
			data32[i] = op.data;

		op.dst->GetMemory()->Unmap();

		return VK_SUCCESS;
	}

	VkResult CpuContext::CopyImage(vkd::Image::OpCopy op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.src && op.src->GetMemory(), "Invalid pointer");
		CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

		return WithMappedMemories(op.src->GetMemory(), op.dst->GetMemory(), [&](cct::UByte* srcBase, VkDeviceSize srcSize, cct::UByte* dstBase, VkDeviceSize dstSize)
								  {
			RowCopier rows{srcBase, srcSize, dstBase, dstSize};
			bool supported = true;
			for (const VkImageCopy& region : op.regions)
				supported &= ForEachImageCopyRow(*op.src, *op.dst, region, rows);
			return rows.Result(supported); });
	}

	VkResult CpuContext::CopyBufferToImage(vkd::Buffer::OpCopyBufferToImage op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.src && op.src->GetMemory(), "Invalid pointer");
		CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

		return WithMappedMemories(op.src->GetMemory(), op.dst->GetMemory(), [&](cct::UByte* srcBase, VkDeviceSize srcSize, cct::UByte* dstBase, VkDeviceSize dstSize)
								  {
			RowCopier rows{srcBase, srcSize, dstBase, dstSize};
			bool supported = true;
			for (const VkBufferImageCopy& region : op.regions)
				supported &= ForEachBufferImageRow(*op.src, *op.dst, region, rows);
			return rows.Result(supported); });
	}

	VkResult CpuContext::CopyImageToBuffer(vkd::Buffer::OpCopyImageToBuffer op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.src && op.src->GetMemory(), "Invalid pointer");
		CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

		return WithMappedMemories(op.src->GetMemory(), op.dst->GetMemory(), [&](cct::UByte* srcBase, VkDeviceSize srcSize, cct::UByte* dstBase, VkDeviceSize dstSize)
								  {
			RowCopier rows{srcBase, srcSize, dstBase, dstSize};
			bool supported = true;
			for (const VkBufferImageCopy& region : op.regions)
			{
				// Same rows as buffer -> image, with source and destination swapped.
				supported &= ForEachBufferImageRow(*op.dst, *op.src, region, [&](VkDeviceSize bufferOffset, VkDeviceSize imageOffset, VkDeviceSize bytes)
				{
					rows(imageOffset, bufferOffset, bytes);
				});
			}
			return rows.Result(supported); });
	}

	VkResult CpuContext::ClearColorImage(vkd::Image::OpClearColorImage op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.image && op.image->GetMemory(), "Invalid pointer");

		const VkFormat format = op.image->GetFormat();
		const VkDeviceSize pixelSize = vkuFormatElementSize(format);
		if (pixelSize == 0 || pixelSize > 32)
			return VK_ERROR_FORMAT_NOT_SUPPORTED;

		UInt8 pattern[32] = {};
		if (IsSupportedColorFormat(format))
			PackColor(format, op.clearColor, pattern);
		else
			cct::Logger::Warning("ClearColorImage: unsupported format {}, zeroing memory", static_cast<int>(format));

		vkd::DeviceMemory* memory = op.image->GetMemory();
		return WithMappedMemories(memory, memory, [&](cct::UByte* base, VkDeviceSize size, cct::UByte*, VkDeviceSize)
								  {
			bool inRange = true;
			for (const VkImageSubresourceRange& range : op.ranges)
			{
				ForEachSubresourceRange(*op.image, range, [&](const SubresourceFootprint& fp, VkDeviceSize bytes)
				{
					if (fp.offset + bytes > size)
					{
						inRange = false;
						return;
					}
					for (VkDeviceSize i = 0; i < bytes; i += pixelSize)
						std::memcpy(base + fp.offset + i, pattern, static_cast<std::size_t>(pixelSize));
				});
			}
			return inRange ? VK_SUCCESS : VK_ERROR_UNKNOWN; });
	}

	VkResult CpuContext::ClearDepthStencilImage(vkd::Image::OpClearDepthStencilImage op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.image && op.image->GetMemory(), "Invalid pointer");

		vkd::DeviceMemory* memory = op.image->GetMemory();
		return WithMappedMemories(memory, memory, [&](cct::UByte* base, VkDeviceSize size, cct::UByte*, VkDeviceSize)
								  {
			bool inRange = true;
			for (const VkImageSubresourceRange& range : op.ranges)
			{
				UInt8 texel[8];
				UInt8 mask[8];
				const UInt32 texelSize = PackDepthStencil(op.image->GetFormat(), op.value, range.aspectMask, texel, mask);
				if (texelSize == 0)
					return VK_ERROR_FORMAT_NOT_SUPPORTED;
				ForEachSubresourceRange(*op.image, range, [&](const SubresourceFootprint& fp, VkDeviceSize)
				{
					const VkRect2D whole{{0, 0}, {fp.extent.width, fp.extent.height}};
					inRange &= FillTexels(base, size, fp, whole, 0, fp.extent.depth, texel, mask, texelSize);
				});
			}
			return inRange ? VK_SUCCESS : VK_ERROR_UNKNOWN; });
	}

	VkResult CpuContext::ClearAttachments(vkd::OpClearAttachments op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		if (!m_currentRenderPass || !m_currentFramebuffer || m_currentRenderPass->GetSubpasses().empty())
			return VK_SUCCESS;

		const VkSubpassDescription& subpass = m_currentRenderPass->GetSubpasses()[0];
		const auto& views = m_currentFramebuffer->GetAttachments();
		for (const VkClearAttachment& clear : op.Attachments)
		{
			UInt32 attachment = VK_ATTACHMENT_UNUSED;
			if (clear.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
			{
				if (clear.colorAttachment < subpass.colorAttachmentCount)
					attachment = subpass.pColorAttachments[clear.colorAttachment].attachment;
			}
			else if (subpass.pDepthStencilAttachment)
				attachment = subpass.pDepthStencilAttachment->attachment;
			if (attachment == VK_ATTACHMENT_UNUSED || attachment >= views.size())
				continue;

			VKD_FROM_HANDLE(vkd::ImageView, viewObj, views[attachment]);
			VKD_FROM_HANDLE(vkd::Image, imageObj, viewObj->GetImage());

			UInt8 texel[32] = {};
			UInt8 mask[32] = {};
			UInt32 texelSize = 0;
			if (clear.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
			{
				texelSize = static_cast<UInt32>(vkuFormatElementSize(imageObj->GetFormat()));
				if (texelSize == 0 || texelSize > sizeof(texel))
					continue;
				if (IsSupportedColorFormat(imageObj->GetFormat()))
					PackColor(imageObj->GetFormat(), clear.clearValue.color, texel);
				std::memset(mask, 0xFF, texelSize);
			}
			else
				texelSize = PackDepthStencil(imageObj->GetFormat(), clear.clearValue.depthStencil, clear.aspectMask, texel, mask);
			if (texelSize == 0)
				continue;

			const VkImageSubresourceRange& range = viewObj->GetSubresourceRange();
			vkd::DeviceMemory* memory = imageObj->GetMemory();
			const VkResult result = WithMappedMemories(memory, memory, [&](cct::UByte* base, VkDeviceSize size, cct::UByte*, VkDeviceSize)
													   {
				bool inRange = true;
				for (const VkClearRect& rect : op.Rects)
				{
					for (UInt32 layer = 0; layer < rect.layerCount; ++layer)
					{
						const UInt32 viewLayer = rect.baseArrayLayer + layer;
						// A 3D image viewed as 2D layers clears z slices; an array image clears layers.
						const bool is3d = imageObj->GetImageType() == VK_IMAGE_TYPE_3D;
						const SubresourceFootprint fp = GetSubresourceFootprint(*imageObj, range.baseMipLevel, is3d ? 0 : range.baseArrayLayer + viewLayer);
						inRange &= FillTexels(base, size, fp, rect.rect, is3d ? viewLayer : 0, 1, texel, mask, texelSize);
					}
				}
				return inRange ? VK_SUCCESS : VK_ERROR_UNKNOWN; });
			if (result != VK_SUCCESS)
				return result;
		}
		return VK_SUCCESS;
	}

	VkResult CpuContext::ResolveImage(vkd::Image::OpResolve op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.src && op.src->GetMemory(), "Invalid pointer");
		CCT_ASSERT(op.dst && op.dst->GetMemory(), "Invalid pointer");

		// Images are single-sampled here (no MSAA rasterization), so a resolve is a copy.
		return WithMappedMemories(op.src->GetMemory(), op.dst->GetMemory(), [&](cct::UByte* srcBase, VkDeviceSize srcSize, cct::UByte* dstBase, VkDeviceSize dstSize)
								  {
			RowCopier rows{srcBase, srcSize, dstBase, dstSize};
			bool supported = true;
			for (const VkImageResolve& r : op.regions)
				supported &= ForEachImageCopyRow(*op.src, *op.dst, VkImageCopy{r.srcSubresource, r.srcOffset, r.dstSubresource, r.dstOffset, r.extent}, rows);
			return rows.Result(supported); });
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

			VkResult result = ClearImageWithColor(imageObj, op.ClearValues[i].color, &op.RenderArea);
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

	VkResult CpuContext::ResetQueryPool(vkd::OpResetQueryPool op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.QueryPoolObject, "Invalid pointer");
		op.QueryPoolObject->Reset(op.FirstQuery, op.QueryCount);

		return VK_SUCCESS;
	}

	VkResult CpuContext::BeginQuery(vkd::OpBeginQuery op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.QueryPoolObject, "Invalid pointer");
		op.QueryPoolObject->Begin(op.Query, op.Flags);

		return VK_SUCCESS;
	}

	VkResult CpuContext::EndQuery(vkd::OpEndQuery op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.QueryPoolObject, "Invalid pointer");
		op.QueryPoolObject->End(op.Query);

		return VK_SUCCESS;
	}

	VkResult CpuContext::WriteTimestamp(vkd::OpWriteTimestamp op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.QueryPoolObject, "Invalid pointer");
		op.QueryPoolObject->WriteTimestamp(op.Query);

		return VK_SUCCESS;
	}

	VkResult CpuContext::CopyQueryPoolResults(vkd::OpCopyQueryPoolResults op)
	{
		VKD_AUTO_PROFILER_SCOPE();

		CCT_ASSERT(op.QueryPoolObject && op.DstBuffer && op.DstBuffer->GetMemory(), "Invalid pointer");

		const VkDeviceSize available = op.DstBuffer->GetSize() - op.DstOffset;

		cct::UByte* dstData = nullptr;
		const VkResult result = MapRange(op.DstBuffer->GetMemory(), op.DstBuffer->GetMemoryOffset() + op.DstOffset, available, &dstData);
		if (result != VK_SUCCESS)
			return result;

		op.QueryPoolObject->GetResults(op.FirstQuery, op.QueryCount, static_cast<std::size_t>(available), dstData, op.Stride, op.Flags);

		op.DstBuffer->GetMemory()->Unmap();

		return VK_SUCCESS;
	}

	VkResult CpuContext::ClearImageWithColor(vkd::Image* image, const VkClearColorValue& color, const VkRect2D* area)
	{
		CCT_ASSERT(image && image->GetMemory(), "Invalid image / memory");

		const VkFormat format = image->GetFormat();
		const VkDeviceSize pixelSize = vkuFormatElementSize(format);
		if (pixelSize == 0)
			return VK_ERROR_FORMAT_NOT_SUPPORTED;

		const VkExtent3D extent = image->GetExtent();
		const VkDeviceSize imageSize = static_cast<VkDeviceSize>(extent.width) * extent.height * extent.depth * pixelSize;

		cct::UByte* data = nullptr;
		const VkResult mapResult = MapRange(image->GetMemory(), image->GetMemoryOffset(), imageSize, &data);
		if (mapResult != VK_SUCCESS)
			return mapResult;

		UInt8 pattern[32] = {};
		const bool supported = IsSupportedColorFormat(format);
		if (supported)
			PackColor(format, color, pattern);

		const int minX = area ? std::max(area->offset.x, 0) : 0;
		const int minY = area ? std::max(area->offset.y, 0) : 0;
		const int maxX = area ? std::min(area->offset.x + static_cast<int>(area->extent.width), static_cast<int>(extent.width)) : static_cast<int>(extent.width);
		const int maxY = area ? std::min(area->offset.y + static_cast<int>(area->extent.height), static_cast<int>(extent.height)) : static_cast<int>(extent.height);
		const bool coversWholeImage = minX == 0 && minY == 0 && maxX == static_cast<int>(extent.width) && maxY == static_cast<int>(extent.height);

		if (supported)
		{
			if (coversWholeImage)
			{
				const std::size_t pixelCount = static_cast<std::size_t>(imageSize / pixelSize);
				for (std::size_t i = 0; i < pixelCount; ++i)
					std::memcpy(data + i * pixelSize, pattern, pixelSize);
			}
			else
			{
				for (int y = minY; y < maxY; ++y)
				{
					cct::UByte* row = data + static_cast<std::size_t>(y) * extent.width * pixelSize;
					for (int x = minX; x < maxX; ++x)
						std::memcpy(row + static_cast<std::size_t>(x) * pixelSize, pattern, pixelSize);
				}
			}
		}
		else
		{
			cct::Logger::Warning("ClearImageWithColor: unsupported format {}, zeroing memory", static_cast<int>(format));

			if (coversWholeImage)
				std::memset(data, 0, static_cast<std::size_t>(imageSize));
			else
			{
				for (int y = minY; y < maxY; ++y)
					std::memset(data + (static_cast<std::size_t>(y) * extent.width + minX) * pixelSize, 0, static_cast<std::size_t>(maxX - minX) * pixelSize);
			}
		}

		image->GetMemory()->Unmap();
		return VK_SUCCESS;
	}
} // namespace vkd::software
