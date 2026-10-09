#include "VkdAmd/CommandBuffer/CommandBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <span>
#include <variant>

#include <Concerto/Core/Logger/Logger.hpp>

#include "Vkd/Buffer/Buffer.hpp"
#include "Vkd/CommandPool/CommandPool.hpp"
#include "Vkd/DescriptorSet/DescriptorSet.hpp"
#include "Vkd/Framebuffer/Framebuffer.hpp"
#include "Vkd/Image/ColorPacking.hpp"
#include "Vkd/Image/CopyLayout.hpp"
#include "Vkd/Image/Image.hpp"
#include "Vkd/ImageView/ImageView.hpp"
#include "Vkd/QueryPool/QueryPool.hpp"
#include "Vkd/RenderPass/RenderPass.hpp"
#include "Vkd/Sampler/Sampler.hpp"
#include "Vkd/Synchronization/Event/Event.hpp"

#include "VkdAmd/Device/Device.hpp"
#include "VkdAmd/DeviceMemory/DeviceMemory.hpp"
#include "VkdAmd/Image/Image.hpp"
#include "VkdAmd/Pipeline/Pipeline.hpp"
#include "VkdAmd/Pm4.hpp"
#include "VkdAmd/PrivateData.hpp"
#include "VkdAmd/Shader/Gfx11Descriptors.hpp"

namespace vkd::amd
{
	namespace
	{
		// WRITE_DATA payload per packet (the PM4 count field allows ~16K DWORDs; stay well below).
		constexpr uint32_t kMaxWriteDataDw = 4096;
		// CP DMA bytes per DMA_DATA packet (BYTE_COUNT is 26 bits).
		constexpr uint64_t kMaxDmaBytes = 1ull << 20;
		// The IB starts at 64KB and doubles when a command buffer doesn't fit, up to this size.
		constexpr uint64_t kMaxIbBytes = 16ull << 20;

		const amd::DeviceMemory* AmdMemory(const vkd::DeviceMemory* memory)
		{
			return static_cast<const amd::DeviceMemory*>(memory);
		}

		bool IsBound(const vkd::Buffer* buffer)
		{
			return buffer && buffer->IsBound();
		}
		bool IsBound(const vkd::Image* image)
		{
			return image && image->IsBound();
		}

		// Writes totalBytes (multiple of 4) at memory+offset with WRITE_DATA, DWORD i of the range
		// being dwordAt(i).
		template<typename DwordAt>
		VkResult WriteDwords(Pm4Stream& stream, const amd::DeviceMemory* memory, uint64_t offset, uint64_t totalBytes,
							 DwordAt&& dwordAt)
		{
			if (!memory || offset % 4 != 0 || totalBytes % 4 != 0)
				return VK_ERROR_FORMAT_NOT_SUPPORTED;
			uint64_t done = 0;
			while (done < totalBytes)
			{
				VkDeviceSize contiguous = 0;
				const D3DGPU_VIRTUAL_ADDRESS va = memory->GetGpuVaAt(offset + done, contiguous);
				if (va == 0)
					return VK_ERROR_DEVICE_LOST;
				const uint64_t chunk = std::min({totalBytes - done, uint64_t{contiguous}, uint64_t{kMaxWriteDataDw} * 4}) & ~uint64_t{3};
				const uint32_t chunkDw = static_cast<uint32_t>(chunk / 4);
				if (chunkDw == 0 || !stream.HasRoom(4 + chunkDw))
					return VK_ERROR_OUT_OF_DEVICE_MEMORY;

				stream.WriteDataHeader(va, chunkDw, Pm4Stream::kWriteDataMe);
				const uint64_t firstDw = done / 4;
				for (uint32_t i = 0; i < chunkDw; ++i)
					stream.Emit(dwordAt(firstDw + i));
				done += chunk;
			}
			return VK_SUCCESS;
		}

		// CP DMA copies between memories. Consecutive ranges contiguous on both sides are coalesced
		// (a whole-image copy is one DMA_DATA per slice, not one per row), then split at kMaxDmaBytes.
		// One copier per recorded op: ranges never merge across ops.
		class DmaCopier
		{
		public:
			explicit DmaCopier(Pm4Stream& stream) :
				m_stream(stream)
			{
			}

			VkResult Copy(const amd::DeviceMemory* src, uint64_t srcOffset, const amd::DeviceMemory* dst,
						  uint64_t dstOffset, uint64_t size)
			{
				if (!src || !dst)
					return VK_ERROR_INITIALIZATION_FAILED;
				if (size == 0)
					return VK_SUCCESS;
				if (m_size != 0 && src == m_src && dst == m_dst && srcOffset == m_srcOffset + m_size &&
					dstOffset == m_dstOffset + m_size)
				{
					m_size += size;
					return VK_SUCCESS;
				}
				if (const VkResult r = Flush(); r != VK_SUCCESS)
					return r;
				m_src = src;
				m_dst = dst;
				m_srcOffset = srcOffset;
				m_dstOffset = dstOffset;
				m_size = size;
				return VK_SUCCESS;
			}

			VkResult Flush()
			{
				uint64_t done = 0;
				while (done < m_size)
				{
					VkDeviceSize srcContiguous = 0;
					VkDeviceSize dstContiguous = 0;
					const D3DGPU_VIRTUAL_ADDRESS srcVa = m_src->GetGpuVaAt(m_srcOffset + done, srcContiguous);
					const D3DGPU_VIRTUAL_ADDRESS dstVa = m_dst->GetGpuVaAt(m_dstOffset + done, dstContiguous);
					if (srcVa == 0 || dstVa == 0)
						return VK_ERROR_DEVICE_LOST;
					const uint64_t chunk = std::min({m_size - done, uint64_t{srcContiguous}, uint64_t{dstContiguous}, kMaxDmaBytes});
					if (!m_stream.HasRoom(7))
						return VK_ERROR_OUT_OF_DEVICE_MEMORY;
					m_stream.DmaCopy(srcVa, dstVa, static_cast<uint32_t>(chunk));
					done += chunk;
				}
				m_size = 0;
				return VK_SUCCESS;
			}

		private:
			Pm4Stream& m_stream;
			const amd::DeviceMemory* m_src = nullptr;
			const amd::DeviceMemory* m_dst = nullptr;
			uint64_t m_srcOffset = 0;
			uint64_t m_dstOffset = 0;
			uint64_t m_size = 0;
		};

		// vkCmdFillBuffer: VK_WHOLE_SIZE fills to the end, the range is truncated to whole DWORDs
		// (same as the software driver). A DMA_DATA constant fill, whatever the size.
		VkResult EncodeFillBuffer(Pm4Stream& stream, const vkd::Buffer::OpFill& op)
		{
			if (!IsBound(op.dst) || op.offset >= op.dst->GetSize())
				return VK_SUCCESS;
			const VkDeviceSize available = op.dst->GetSize() - op.offset;
			const VkDeviceSize size = std::min(op.size == VK_WHOLE_SIZE ? available : op.size, available) & ~VkDeviceSize{3};
			if (size == 0)
				return VK_SUCCESS;
			const amd::DeviceMemory* memory = AmdMemory(op.dst->GetMemory());
			const uint64_t offset = op.dst->GetMemoryOffset() + op.offset;
			if (offset % 4 != 0)
				return WriteDwords(stream, memory, offset, size, [&](uint64_t)
								   { return op.data; });
			VkDeviceSize contiguous = 0;
			const D3DGPU_VIRTUAL_ADDRESS va = memory->GetGpuVaAt(offset, contiguous);
			if (va == 0 || contiguous < size)
				return VK_ERROR_DEVICE_LOST;
			for (uint64_t done = 0; done < size;)
			{
				const uint64_t chunk = std::min(size - done, kMaxDmaBytes);
				if (!stream.HasRoom(7))
					return VK_ERROR_OUT_OF_DEVICE_MEMORY;
				stream.DmaFill(va + done, op.data, static_cast<uint32_t>(chunk));
				done += chunk;
			}
			return VK_SUCCESS;
		}

		// vkCmdUpdateBuffer: dataSize and dstOffset are multiples of 4 by spec.
		VkResult EncodeUpdateBuffer(Pm4Stream& stream, const vkd::Buffer::OpUpdate& op)
		{
			if (!IsBound(op.dst) || op.data.empty())
				return VK_SUCCESS;
			return WriteDwords(stream, AmdMemory(op.dst->GetMemory()), op.dst->GetMemoryOffset() + op.offset,
							   op.data.size(), [&](uint64_t i)
							   {
								   uint32_t v;
								   std::memcpy(&v, op.data.data() + i * 4, sizeof(v));
								   return v; });
		}

		// Fills [offset, offset + bytes) of a memory with a repeated texel. A texel pattern that
		// repeats per DWORD becomes a DMA_DATA constant fill. Any other pattern is embedded once in
		// the IB (one block of whole texels), copied in, then the filled prefix is doubled with DMA
		// copies inside the destination -- every DMA_DATA has CP_SYNC, so a copy only starts once
		// the previous one landed. No shader, any texel size, byte-granular.
		VkResult FillPattern(Pm4Stream& stream, const amd::DeviceMemory* memory, uint64_t offset, uint64_t bytes,
							 const uint8_t* texel, uint32_t texelSize)
		{
			if (bytes == 0)
				return VK_SUCCESS;
			if (!memory || texelSize == 0 || texelSize > 32)
				return VK_ERROR_FORMAT_NOT_SUPPORTED;

			VkDeviceSize contiguous = 0;
			const D3DGPU_VIRTUAL_ADDRESS dstVa = memory->GetGpuVaAt(offset, contiguous);
			if (dstVa == 0 || contiguous < bytes)
				return VK_ERROR_DEVICE_LOST;

			const uint32_t patternBytes = std::lcm(texelSize, 4u);
			uint8_t pattern[96] = {};
			for (uint32_t i = 0; i < patternBytes; ++i)
				pattern[i] = texel[i % texelSize];
			bool uniformDword = true;
			for (uint32_t i = 4; i < patternBytes; ++i)
				uniformDword &= pattern[i] == pattern[i % 4];

			if (uniformDword && offset % 4 == 0 && bytes % 4 == 0)
			{
				uint32_t value;
				std::memcpy(&value, pattern, sizeof(value));
				for (uint64_t done = 0; done < bytes;)
				{
					const uint64_t chunk = std::min(bytes - done, kMaxDmaBytes);
					if (!stream.HasRoom(7))
						return VK_ERROR_OUT_OF_DEVICE_MEMORY;
					stream.DmaFill(dstVa + done, value, static_cast<uint32_t>(chunk));
					done += chunk;
				}
				return VK_SUCCESS;
			}

			constexpr uint32_t kBlockBytes = 256;
			const uint32_t blockBytes = static_cast<uint32_t>(std::min<uint64_t>(bytes, (kBlockBytes / texelSize) * texelSize));
			uint8_t block[kBlockBytes];
			for (uint32_t i = 0; i < blockBytes; ++i)
				block[i] = texel[i % texelSize];
			const uint64_t blockVa = stream.EmbedData(block, blockBytes);
			if (blockVa == 0 || !stream.HasRoom(7))
				return VK_ERROR_OUT_OF_DEVICE_MEMORY;
			stream.DmaCopy(blockVa, dstVa, blockBytes);

			const uint64_t maxChunk = (kMaxDmaBytes / texelSize) * texelSize;
			for (uint64_t done = blockBytes; done < bytes;)
			{
				const uint64_t chunk = std::min({done, bytes - done, maxChunk});
				if (!stream.HasRoom(7))
					return VK_ERROR_OUT_OF_DEVICE_MEMORY;
				stream.DmaCopy(dstVa, dstVa + done, static_cast<uint32_t>(chunk));
				done += chunk;
			}
			return VK_SUCCESS;
		}

		// Texel bytes of a clear color, or zeros for a format PackColor doesn't handle (the software
		// driver zeroes those too). Returns the texel size, 0 if the format can't be cleared at all.
		uint32_t PackClearTexel(VkFormat format, const VkClearColorValue& color, uint8_t (&texel)[32])
		{
			const uint32_t texelSize = vkuFormatElementSize(format);
			if (texelSize == 0 || texelSize > sizeof(texel) || vkuFormatIsDepthOrStencil(format) || vkuFormatIsCompressed(format))
				return 0;
			std::memset(texel, 0, sizeof(texel));
			if (vkd::IsSupportedColorFormat(format))
				vkd::PackColor(format, color, texel);
			else
				cct::Logger::Warning("AmdCommandBuffer: clear of unsupported format {}, zeroing", static_cast<int>(format));
			return texelSize;
		}

		// Clears a rectangle of one subresource: rows spanning the full width are one contiguous
		// range; otherwise the first row is filled and DMA-copied to the others.
		VkResult FillRect(Pm4Stream& stream, const amd::DeviceMemory* memory, const vkd::SubresourceFootprint& fp,
						  VkRect2D rect, const uint8_t* texel, uint32_t texelSize)
		{
			const int64_t x0 = std::max<int64_t>(rect.offset.x, 0);
			const int64_t y0 = std::max<int64_t>(rect.offset.y, 0);
			const int64_t x1 = std::min<int64_t>(int64_t{rect.offset.x} + rect.extent.width, fp.extent.width);
			const int64_t y1 = std::min<int64_t>(int64_t{rect.offset.y} + rect.extent.height, fp.extent.height);
			if (x1 <= x0 || y1 <= y0)
				return VK_SUCCESS;

			const uint64_t rowBytes = static_cast<uint64_t>(x1 - x0) * texelSize;
			const uint64_t firstRow = vkd::ImageTexelOffset(fp, static_cast<uint32_t>(x0), static_cast<uint32_t>(y0), 0, texelSize);
			if (rowBytes == fp.rowPitch)
				return FillPattern(stream, memory, firstRow, rowBytes * static_cast<uint64_t>(y1 - y0), texel, texelSize);

			if (const VkResult r = FillPattern(stream, memory, firstRow, rowBytes, texel, texelSize); r != VK_SUCCESS)
				return r;
			DmaCopier copier(stream);
			for (int64_t y = y0 + 1; y < y1; ++y)
			{
				const uint64_t row = vkd::ImageTexelOffset(fp, static_cast<uint32_t>(x0), static_cast<uint32_t>(y), 0, texelSize);
				if (const VkResult r = copier.Copy(memory, firstRow, memory, row, rowBytes); r != VK_SUCCESS)
					return r;
				if (const VkResult r = copier.Flush(); r != VK_SUCCESS)
					return r;
			}
			return VK_SUCCESS;
		}

		VkResult FillSubresource(Pm4Stream& stream, const amd::DeviceMemory* memory, const vkd::SubresourceFootprint& fp,
								 VkDeviceSize bytes, const uint8_t* texel, uint32_t texelSize)
		{
			if (fp.rowPitch % texelSize == 0 && fp.depthPitch % texelSize == 0)
				return FillPattern(stream, memory, fp.offset, bytes, texel, texelSize);
			const VkRect2D whole{{0, 0}, {fp.extent.width, fp.extent.height}};
			for (uint32_t z = 0; z < fp.extent.depth; ++z)
			{
				vkd::SubresourceFootprint slice = fp;
				slice.offset += VkDeviceSize{z} * fp.depthPitch;
				if (const VkResult r = FillRect(stream, memory, slice, whole, texel, texelSize); r != VK_SUCCESS)
					return r;
			}
			return VK_SUCCESS;
		}

		// vkCmdClearColorImage: each (layer, mip) of each range is one contiguous byte range of the
		// linear image (Vkd/Image/CopyLayout.hpp).
		VkResult EncodeClearColorImage(Pm4Stream& stream, const vkd::Image::OpClearColorImage& op)
		{
			if (!IsBound(op.image))
				return VK_ERROR_INITIALIZATION_FAILED;
			uint8_t texel[32];
			const uint32_t texelSize = PackClearTexel(op.image->GetFormat(), op.clearColor, texel);
			if (texelSize == 0)
			{
				cct::Logger::Warning("AmdCommandBuffer: clear of format {} not supported, skipped", static_cast<int>(op.image->GetFormat()));
				return VK_SUCCESS;
			}
			const amd::DeviceMemory* memory = AmdMemory(op.image->GetMemory());
			VkResult result = VK_SUCCESS;
			for (const VkImageSubresourceRange& range : op.ranges)
			{
				vkd::ForEachSubresourceRange(*op.image, range, [&](const vkd::SubresourceFootprint& fp, VkDeviceSize bytes)
											 {
					if (result == VK_SUCCESS)
						result = FillSubresource(stream, memory, fp, bytes, texel, texelSize); });
			}
			return result;
		}

		// Render pass load-op clears: the render area of every CLEAR color attachment, on the view's
		// mip and on each framebuffer layer. Draws aren't executed yet, so the clear is all the
		// render pass does to its attachments.
		struct EncodeState;
		VkResult EncodeTiledClear(Pm4Stream& stream, const amd::Image& image, VkImageAspectFlagBits aspect, const VkClearDepthStencilValue& value,
								  uint32_t mipLevel, uint32_t baseLayer, uint32_t layerCount, const VkRect2D& rect, EncodeState& state);
		template<typename Fn>
		VkResult ForEachTiledAspect(const amd::Image& image, VkImageAspectFlags mask, Fn&& fn);

		VkResult EncodeBeginRenderPass(Pm4Stream& stream, const vkd::OpBeginRenderPass& op, EncodeState& state)
		{
			if (!op.RenderPassObject || !op.FramebufferObject)
				return VK_SUCCESS;
			const auto& attachments = op.RenderPassObject->GetAttachments();
			const auto& views = op.FramebufferObject->GetAttachments();
			const std::size_t count = std::min(attachments.size(), views.size());
			for (std::size_t i = 0; i < count; ++i)
			{
				if ((attachments[i].loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR && attachments[i].stencilLoadOp != VK_ATTACHMENT_LOAD_OP_CLEAR) ||
					i >= op.ClearValues.size())
					continue;
				VKD_FROM_HANDLE(vkd::ImageView, view, views[i]);
				VKD_FROM_HANDLE(vkd::Image, image, view->GetImage());
				if (!IsBound(image))
					continue;
				if (vkuFormatIsDepthOrStencil(image->GetFormat()))
				{
					const auto* tiled = static_cast<const amd::Image*>(image);
					if (!tiled->IsTiled())
						continue;
					const VkImageSubresourceRange& range = view->GetSubresourceRange();
					const uint32_t viewLayers = vkd::ResolveLayerCount(*image, range.baseArrayLayer, range.layerCount);
					const uint32_t layers = std::min(viewLayers, std::max(op.FramebufferObject->GetLayers(), 1u));
					const VkImageAspectFlags cleared = (attachments[i].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? VK_IMAGE_ASPECT_DEPTH_BIT : 0u) |
													   (attachments[i].stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? VK_IMAGE_ASPECT_STENCIL_BIT : 0u);
					if (const VkResult r = ForEachTiledAspect(*tiled, cleared, [&](VkImageAspectFlagBits aspect)
															  { return EncodeTiledClear(stream, *tiled, aspect, op.ClearValues[i].depthStencil, range.baseMipLevel,
																						range.baseArrayLayer, layers, op.RenderArea, state); });
						r != VK_SUCCESS)
						return r;
					continue;
				}
				uint8_t texel[32];
				const uint32_t texelSize = PackClearTexel(image->GetFormat(), op.ClearValues[i].color, texel);
				if (texelSize == 0)
					continue;

				const VkImageSubresourceRange& range = view->GetSubresourceRange();
				const uint32_t viewLayers = vkd::ResolveLayerCount(*image, range.baseArrayLayer, range.layerCount);
				const uint32_t layers = std::min(viewLayers, std::max(op.FramebufferObject->GetLayers(), 1u));
				for (uint32_t layer = 0; layer < layers; ++layer)
				{
					const vkd::SubresourceFootprint fp = vkd::GetSubresourceFootprint(*image, range.baseMipLevel, range.baseArrayLayer + layer);
					if (const VkResult r = FillRect(stream, AmdMemory(image->GetMemory()), fp, op.RenderArea, texel, texelSize); r != VK_SUCCESS)
						return r;
				}
			}
			return VK_SUCCESS;
		}

		// Copies go through the row helpers of Vkd/Image/CopyLayout.hpp (layers, mips,
		// bufferRowLength/bufferImageHeight), the same rows the software driver memcpy's, coalesced
		// by DmaCopier. A region the linear model can't express (one aspect of a depth/stencil
		// format) is skipped with a warning.
		template<typename ForEachRow>
		VkResult EncodeRows(Pm4Stream& stream, ForEachRow&& forEachRow)
		{
			DmaCopier copier(stream);
			VkResult result = VK_SUCCESS;
			const bool supported = forEachRow([&](const amd::DeviceMemory* src, uint64_t srcOffset, const amd::DeviceMemory* dst,
												  uint64_t dstOffset, uint64_t bytes)
											  {
				if (result == VK_SUCCESS)
					result = copier.Copy(src, srcOffset, dst, dstOffset, bytes); });
			if (!supported)
				cct::Logger::Warning("AmdCommandBuffer: copy region not supported (depth/stencil aspect), skipped");
			if (result != VK_SUCCESS)
				return result;
			return copier.Flush();
		}

		VkResult EncodeCopyImage(Pm4Stream& stream, const vkd::Image::OpCopy& op)
		{
			if (!IsBound(op.src) || !IsBound(op.dst))
				return VK_ERROR_INITIALIZATION_FAILED;
			const amd::DeviceMemory* src = AmdMemory(op.src->GetMemory());
			const amd::DeviceMemory* dst = AmdMemory(op.dst->GetMemory());
			return EncodeRows(stream, [&](auto&& copy)
							  {
				bool supported = true;
				for (const VkImageCopy& r : op.regions)
					supported &= vkd::ForEachImageCopyRow(*op.src, *op.dst, r, [&](VkDeviceSize s, VkDeviceSize d, VkDeviceSize n) { copy(src, s, dst, d, n); });
				return supported; });
		}

		VkResult EncodeCopyBufferToImage(Pm4Stream& stream, const vkd::Buffer::OpCopyBufferToImage& op)
		{
			if (!IsBound(op.src) || !IsBound(op.dst))
				return VK_ERROR_INITIALIZATION_FAILED;
			const amd::DeviceMemory* buffer = AmdMemory(op.src->GetMemory());
			const amd::DeviceMemory* image = AmdMemory(op.dst->GetMemory());
			return EncodeRows(stream, [&](auto&& copy)
							  {
				bool supported = true;
				for (const VkBufferImageCopy& r : op.regions)
					supported &= vkd::ForEachBufferImageRow(*op.src, *op.dst, r, [&](VkDeviceSize b, VkDeviceSize i, VkDeviceSize n) { copy(buffer, b, image, i, n); });
				return supported; });
		}

		VkResult EncodeCopyImageToBuffer(Pm4Stream& stream, const vkd::Buffer::OpCopyImageToBuffer& op)
		{
			if (!IsBound(op.src) || !IsBound(op.dst))
				return VK_ERROR_INITIALIZATION_FAILED;
			const amd::DeviceMemory* image = AmdMemory(op.src->GetMemory());
			const amd::DeviceMemory* buffer = AmdMemory(op.dst->GetMemory());
			return EncodeRows(stream, [&](auto&& copy)
							  {
				bool supported = true;
				for (const VkBufferImageCopy& r : op.regions)
					supported &= vkd::ForEachBufferImageRow(*op.dst, *op.src, r, [&](VkDeviceSize b, VkDeviceSize i, VkDeviceSize n) { copy(image, i, buffer, b, n); });
				return supported; });
		}

		template<typename Region>
		VkResult EncodeCopyBuffer(Pm4Stream& stream, const vkd::Buffer* src, const vkd::Buffer* dst, const Region& regions)
		{
			if (!IsBound(src) || !IsBound(dst))
				return VK_ERROR_INITIALIZATION_FAILED;
			DmaCopier copier(stream);
			for (const auto& r : regions)
			{
				if (const VkResult res = copier.Copy(AmdMemory(src->GetMemory()), src->GetMemoryOffset() + r.srcOffset,
													 AmdMemory(dst->GetMemory()), dst->GetMemoryOffset() + r.dstOffset, r.size);
					res != VK_SUCCESS)
					return res;
			}
			return copier.Flush();
		}

		// Depth/stencil clears are DMA fills too, as long as the clear owns whole texels: one aspect
		// of a combined format would need a read-modify-write (a shader), so it's skipped.
		bool PackDepthStencilTexel(VkFormat format, const VkClearDepthStencilValue& value, VkImageAspectFlags aspects,
								   uint8_t (&texel)[8], uint32_t& texelSize)
		{
			uint8_t mask[8];
			texelSize = vkd::PackDepthStencil(format, value, aspects, texel, mask);
			if (texelSize == 0)
				return false;
			for (uint32_t i = 0; i < texelSize; ++i)
			{
				if (mask[i] != 0xFF)
				{
					cct::Logger::Warning("AmdCommandBuffer: one-aspect clear of combined depth/stencil format {} not supported, skipped",
										 static_cast<int>(format));
					return false;
				}
			}
			return true;
		}

		VkResult EncodeClearDepthStencilImage(Pm4Stream& stream, const vkd::Image::OpClearDepthStencilImage& op, EncodeState& state)
		{
			if (!IsBound(op.image))
				return VK_ERROR_INITIALIZATION_FAILED;
			if (const auto* tiled = static_cast<const amd::Image*>(op.image); tiled->IsTiled())
			{
				for (const VkImageSubresourceRange& range : op.ranges)
				{
					const uint32_t levels = range.levelCount == VK_REMAINING_MIP_LEVELS ? tiled->GetMipLevels() - range.baseMipLevel : range.levelCount;
					const uint32_t layers = vkd::ResolveLayerCount(*tiled, range.baseArrayLayer, range.layerCount);
					for (uint32_t level = range.baseMipLevel; level < range.baseMipLevel + levels; ++level)
					{
						const VkRect2D whole = {{0, 0}, {std::max(tiled->GetExtent().width >> level, 1u), std::max(tiled->GetExtent().height >> level, 1u)}};
						if (const VkResult r = ForEachTiledAspect(*tiled, range.aspectMask, [&](VkImageAspectFlagBits aspect)
																  { return EncodeTiledClear(stream, *tiled, aspect, op.value, level, range.baseArrayLayer, layers, whole, state); });
							r != VK_SUCCESS)
							return r;
					}
				}
				return VK_SUCCESS;
			}
			const amd::DeviceMemory* memory = AmdMemory(op.image->GetMemory());
			VkResult result = VK_SUCCESS;
			for (const VkImageSubresourceRange& range : op.ranges)
			{
				uint8_t texel[8];
				uint32_t texelSize = 0;
				if (!PackDepthStencilTexel(op.image->GetFormat(), op.value, range.aspectMask, texel, texelSize))
					continue;
				vkd::ForEachSubresourceRange(*op.image, range, [&](const vkd::SubresourceFootprint& fp, VkDeviceSize bytes)
											 {
					if (result == VK_SUCCESS)
						result = FillSubresource(stream, memory, fp, bytes, texel, texelSize); });
			}
			return result;
		}

		// CPU-written GPU memory for descriptor tables, in the shader VA window, filled while encoding
		// (bump allocator, reset per Encode). Lives in the command buffer.
		struct DescriptorArena
		{
			wddm::WddmAllocation* allocation = nullptr;
			uint8_t* cpu = nullptr;
			uint64_t used = 0;

			// 64-byte aligned slice; 0 when full.
			uint64_t Allocate(uint64_t bytes, uint8_t*& out)
			{
				const uint64_t offset = (used + 63) & ~uint64_t{63};
				if (!allocation || !cpu || offset + bytes > allocation->GetSize())
					return 0;
				used = offset + bytes;
				out = cpu + offset;
				return allocation->GetGpuVa() + offset;
			}
		};

		struct EncodeState
		{
			const vkd::OpBeginRenderPass* renderPass = nullptr;
			const amd::Pipeline* computePipeline = nullptr;
			const vkd::DescriptorSet* computeSets[kMaxPrecompiledSets] = {};
			std::vector<vkd::DynamicOffsetEntry> computeDynamicOffsets;
			uint8_t pushConstants[256] = {};
			bool shaderCachesInvalidated = false;
			bool computeEngine = false; // queue families 1/2 run on the compute engine (node 2)
			const amd::Pipeline* graphicsPipeline = nullptr;
			const vkd::DescriptorSet* graphicsSets[kMaxPrecompiledSets] = {};
			std::vector<vkd::DynamicOffsetEntry> graphicsDynamicOffsets;
			// vkCmdSetViewport/vkCmdSetScissor (index 0), used when the pipeline makes them dynamic.
			VkViewport viewport = {};
			VkRect2D scissor = {};
			float blendConstants[4] = {};
			// vkCmdSetStencilCompareMask/WriteMask/Reference (front, back), vkCmdSetDepthBounds,
			// vkCmdSetDepthBias: used when the pipeline makes them dynamic.
			StencilFaceValues stencil[2] = {};
			float depthBounds[2] = {0.0f, 1.0f};
			DepthBias depthBias;
			// vkCmdBindVertexBuffers
			struct VertexBinding
			{
				const vkd::Buffer* buffer = nullptr;
				VkDeviceSize offset = 0;
			};
			VertexBinding vertexBuffers[GraphicsPipelineState::kMaxVertexBindings] = {};
			// vkCmdBindIndexBuffer
			const vkd::Buffer* indexBuffer = nullptr;
			VkDeviceSize indexOffset = 0;
			VkIndexType indexType = VK_INDEX_TYPE_UINT16;
			bool contextCleared = false;
			D3DGPU_VIRTUAL_ADDRESS attributeRingVa = 0;
			uint32_t shaderRingTable = 0;
			AmdDevice* device = nullptr;
			DescriptorArena* descriptors = nullptr;
			// vkCmdWriteTimestamp: slot n of the command buffer's timestamp allocation.
			D3DGPU_VIRTUAL_ADDRESS timestampVa = 0;
			uint32_t timestampCapacity = 0;
			uint32_t timestampCount = 0;
			// Occlusion queries: slot n per vkCmdBeginQuery; the ones begun and not ended yet.
			D3DGPU_VIRTUAL_ADDRESS occlusionVa = 0;
			uint32_t occlusionCapacity = 0;
			uint32_t occlusionCount = 0;
			bool pixelPipeStatControl = false;
			struct ActiveQuery
			{
				const vkd::QueryPool* pool;
				uint32_t query;
				uint32_t slot;
			};
			std::vector<ActiveQuery> activeOcclusion;
		};

		// Bytes a descriptor takes in a set table, as AMDVLK lays it out: V# and S# 16, T# 32, a
		// combined image sampler T# then S#.
		uint32_t DescriptorBytes(VkDescriptorType type)
		{
			switch (type)
			{
				case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
					return 48;
				case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
				case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
					return 32;
				default:
					return 16;
			}
		}

		// T# of an image view: one mip level (Vkd lays out mips itself, the hardware's linear mip
		// chain differs), its layers only when the image has a single mip (Vkd's layer stride is the
		// whole mip chain).
		bool WriteImageViewDescriptor(uint8_t* out, const vkd::ImageView& view)
		{
			VKD_FROM_HANDLE(vkd::Image, image, view.GetImage());
			if (!IsBound(image))
				return false;
			const VkImageSubresourceRange& range = view.GetSubresourceRange();
			const uint32_t layers = vkd::ResolveLayerCount(*image, range.baseArrayLayer, range.layerCount);
			if ((layers > 1 && image->GetMipLevels() > 1) || vkd::ResolveLevelCount(*image, range.baseMipLevel, range.levelCount) > 1)
			{
				static bool warned = false;
				if (!warned)
				{
					warned = true;
					cct::Logger::Warning("AmdCommandBuffer: sampled views see their base mip only (and no layers of mipmapped images)");
				}
			}
			const vkd::SubresourceFootprint fp = vkd::GetSubresourceFootprint(*image, range.baseMipLevel, range.baseArrayLayer);
			gfx11::ImageView t{};
			t.address = AmdMemory(image->GetMemory())->GetGpuVa() + fp.offset;
			t.format = view.GetFormat();
			t.viewType = view.GetViewType();
			t.width = fp.extent.width;
			t.height = fp.extent.height;
			t.depth = view.GetViewType() == VK_IMAGE_VIEW_TYPE_3D ? fp.extent.depth : (image->GetMipLevels() > 1 ? 1 : layers);
			t.components = view.GetComponents();
			if (!gfx11::WriteImageDescriptor(out, t))
			{
				static bool warned = false;
				if (!warned)
				{
					warned = true;
					cct::Logger::Warning("AmdCommandBuffer: image view format {} not readable by the texture unit yet, descriptor left null",
										 static_cast<int>(view.GetFormat()));
				}
				return false;
			}
			return true;
		}

		// Writes the descriptor table of one set as the replayed shaders read it (offsets from the
		// capture, PrecompiledDescriptor) and returns the 32-bit pointer its user-data SGPR gets. The
		// table must share the code's 4GB window -- the shader takes the high half of the address
		// from its own PC. 0 when the arena is full.
		uint32_t DynamicOffset(std::span<const vkd::DynamicOffsetEntry> dynamicOffsets, uint32_t set, uint32_t binding, uint32_t element)
		{
			for (const vkd::DynamicOffsetEntry& entry : dynamicOffsets)
			{
				if (entry.SetIndex == set && entry.Binding == binding && entry.ArrayElement == element)
					return entry.Offset;
			}
			return 0;
		}

		uint32_t BuildDescriptorTable(std::span<const PrecompiledDescriptor> descriptors, uint32_t set, const vkd::DescriptorSet* bound,
									  std::span<const vkd::DynamicOffsetEntry> dynamicOffsets, uint64_t codeVa, DescriptorArena* arena)
		{
			uint64_t tableBytes = 16;
			for (const PrecompiledDescriptor& d : descriptors)
				if (d.set == set)
					tableBytes = std::max<uint64_t>(tableBytes, d.offset + uint64_t{d.count} * DescriptorBytes(d.type));
			uint8_t* table = nullptr;
			const uint64_t tableVa = arena ? arena->Allocate(tableBytes, table) : 0;
			if (tableVa == 0 || (tableVa >> 32) != (codeVa >> 32))
				return 0;
			std::memset(table, 0, static_cast<size_t>(tableBytes));
			if (!bound)
				return static_cast<uint32_t>(tableVa);

			for (const PrecompiledDescriptor& d : descriptors)
			{
				if (d.set != set)
					continue;
				for (uint32_t element = 0; element < d.count; ++element)
				{
					uint8_t* out = table + d.offset + element * DescriptorBytes(d.type);
					switch (d.type)
					{
						case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
						case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
						{
							const vkd::DescriptorSet::BufferBinding* b = bound->FindBufferBinding(d.binding, element);
							if (!b || !IsBound(b->buffer))
								break;
							const uint64_t offset = b->offset + DynamicOffset(dynamicOffsets, set, d.binding, element);
							if (offset > b->buffer->GetSize())
								break;
							const uint64_t address = AmdMemory(b->buffer->GetMemory())->GetGpuVa() + b->buffer->GetMemoryOffset() + offset;
							const uint64_t range = b->range == VK_WHOLE_SIZE ? b->buffer->GetSize() - offset : b->range;
							gfx11::WriteBufferDescriptor(out, address, range);
							break;
						}
						case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
						case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
						case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
						case VK_DESCRIPTOR_TYPE_SAMPLER:
						{
							const vkd::DescriptorSet::ImageBinding* b = bound->FindImageBinding(d.binding, element);
							if (!b)
								break;
							if (d.type != VK_DESCRIPTOR_TYPE_SAMPLER && b->imageView)
								WriteImageViewDescriptor(out, *b->imageView);
							if (b->sampler && (d.type == VK_DESCRIPTOR_TYPE_SAMPLER || d.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER))
								gfx11::WriteSamplerDescriptor(d.type == VK_DESCRIPTOR_TYPE_SAMPLER ? out : out + 32, b->sampler->GetCreateInfo());
							break;
						}
						default:
							break;
					}
				}
			}
			return static_cast<uint32_t>(tableVa);
		}

		// Per set, the 32-bit pointer of its table (0 = the shader doesn't read the set).
		bool BuildDescriptorTables(const PrecompiledComputeShader& shader, uint64_t codeVa, EncodeState& state,
								   uint32_t (&tablePointers)[kMaxPrecompiledSets])
		{
			for (uint32_t set = 0; set < kMaxPrecompiledSets; ++set)
			{
				tablePointers[set] = 0;
				if (shader.setTableUserData[set] == 0)
					continue;
				tablePointers[set] = BuildDescriptorTable(shader.descriptors, set, state.computeSets[set], state.computeDynamicOffsets, codeVa, state.descriptors);
				if (tablePointers[set] == 0)
					return false;
			}
			return true;
		}

		// The vertex buffer table of a draw: one descriptor per binding of the pipeline's vertex
		// input, from the bound buffers. 32-bit pointer in the shader window, 0 when the arena is full.
		uint32_t BuildVertexBufferTable(const GraphicsPipelineState& gs, const EncodeState& state, uint64_t codeVa)
		{
			const uint64_t tableBytes = std::max<uint64_t>(uint64_t{gs.vertexBindingCount} * 16, 16);
			uint8_t* table = nullptr;
			const uint64_t tableVa = state.descriptors ? state.descriptors->Allocate(tableBytes, table) : 0;
			if (tableVa == 0 || (tableVa >> 32) != (codeVa >> 32))
				return 0;
			std::memset(table, 0, static_cast<size_t>(tableBytes));
			for (uint32_t b = 0; b < gs.vertexBindingCount; ++b)
			{
				const auto& bound = state.vertexBuffers[b];
				if (!IsBound(bound.buffer) || bound.offset >= bound.buffer->GetSize())
					continue;
				const uint64_t address = AmdMemory(bound.buffer->GetMemory())->GetGpuVa() + bound.buffer->GetMemoryOffset() + bound.offset;
				gfx11::WriteVertexBufferDescriptor(table + b * 16, address, bound.buffer->GetSize() - bound.offset, gs.vertexStrides[b]);
			}
			return static_cast<uint32_t>(tableVa);
		}

		uint32_t FloatBits(float value)
		{
			uint32_t bits;
			std::memcpy(&bits, &value, sizeof(bits));
			return bits;
		}

		// Viewport 0 registers, as PAL's UniversalCmdBuffer::ValidateViewports writes them for one
		// viewport (Vulkan's [0, 1] depth range, upper-left origin): scale/offset, z clamp, the guard
		// band (how far beyond the viewport primitives may extend before being clipped, within the
		// +/-32768 screen coordinates) and the screen offset that centers it on the viewport.
		struct ViewportRegs
		{
			float xScale, xOffset, yScale, yOffset, zScale, zOffset, zMin, zMax;
			float vertClip, horzClip;
			uint32_t screenOffset;
		};

		ViewportRegs ComputeViewportRegs(const VkViewport& vp)
		{
			ViewportRegs r{};
			r.xScale = vp.width * 0.5f;
			r.xOffset = vp.x + r.xScale;
			r.yScale = vp.height * 0.5f;
			r.yOffset = vp.y + r.yScale;
			r.zScale = vp.maxDepth - vp.minDepth;
			r.zOffset = vp.minDepth;
			r.zMin = std::min(vp.minDepth, vp.maxDepth);
			r.zMax = std::max(vp.minDepth, vp.maxDepth);

			constexpr float kMaxScreenCoord = 32768.0f;
			const float left = std::min(vp.x, vp.x + vp.width);
			const float right = std::max(vp.x, vp.x + vp.width);
			const float top = std::min(vp.y, vp.y + vp.height);
			const float bottom = std::max(vp.y, vp.y + vp.height);
			const float centerX = std::max((left + right) / 2, 0.0f);
			const float centerY = std::max((top + bottom) / 2, 0.0f);
			const float clipLimitX = std::min(kMaxScreenCoord + left, kMaxScreenCoord - right);
			const float clipLimitY = std::min(kMaxScreenCoord + top, kMaxScreenCoord - bottom);
			const float xs = std::max((right - left) * 0.5f, 0.5f);
			const float ys = std::max((bottom - top) * 0.5f, 0.5f);
			r.horzClip = (clipLimitX + xs) / xs;
			r.vertClip = (clipLimitY + ys) / ys;
			// GFX11: bit 0 of both offsets must be 0 (VRS), as PAL masks them.
			const uint32_t offsetX = static_cast<uint32_t>(centerX / 16.0f) & 0xFFEu;
			const uint32_t offsetY = static_cast<uint32_t>(centerY / 16.0f) & 0xFFEu;
			r.screenOffset = offsetX | (offsetY << 16);
			return r;
		}

		// Dispatch of a PAL RPM pipeline, the way PAL programs one (PipelineChunkCs): its metadata
		// registers, the internal table in COMPUTE_USER_DATA_1 (ConstBufTblStartReg), user-data
		// entry 0 (the embedded table) where the metadata maps it. The vector caches are invalidated
		// first: RPM shaders read images earlier work wrote.
		// constants go to user-data entries 1..n (consecutive USER_DATA registers after entry 0's).
		VkResult EncodeRpmDispatch(Pm4Stream& stream, const AmdDevice::RpmProgram& program, uint32_t entry0, const uint32_t (&groups)[3],
								   const EncodeState& state, std::span<const uint32_t> constants = {})
		{
			const RpmPipeline& pipeline = *program.pipeline;
			if (!stream.HasRoom(112 + constants.size()))
				return VK_ERROR_OUT_OF_DEVICE_MEMORY;
			stream.AcquireMemInvalidateShaderCaches(state.computeEngine, true);

			const uint32_t pgm[2] = {static_cast<uint32_t>(program.codeVa >> 8), static_cast<uint32_t>(program.codeVa >> 40)};
			const uint32_t rsrc12[2] = {pipeline.pgmRsrc1, pipeline.pgmRsrc2};
			const uint32_t start[3] = {0, 0, 0};
			stream.SetComputeShRegs(kComputePgmLo, pgm, 2);
			stream.SetComputeShRegs(kComputePgmRsrc1, rsrc12, 2);
			stream.SetComputeShReg(kComputePgmRsrc3, pipeline.pgmRsrc3);
			stream.SetComputeShRegs(kComputeNumThreadX, pipeline.numThread, 3);
			stream.SetComputeShRegs(kComputeStartX, start, 3);
			stream.SetComputeShReg(kComputeResourceLimits, 0);
			stream.SetComputeShReg(kComputeDispatchInterleave, 0x40);
			stream.SetComputeShReg(kComputeTmpringSize, 0x100);
			stream.SetComputeShReg(kComputePerfcountEnable, 0);
			stream.SetComputeShRegsCuMasked(kComputeStaticThreadMgmtSe0, 0xFFFFFFFFu, 0);
			stream.SetComputeShRegsCuMasked(kComputeStaticThreadMgmtSe2, 0, 0);
			if (!pipeline.internalTable.empty())
				stream.SetComputeShReg(kComputeUserData0 + 1, static_cast<uint32_t>(program.tableVa));
			if (pipeline.entryUserData[0] != UINT32_MAX)
				stream.SetComputeShReg(kComputeUserData0 + pipeline.entryUserData[0], entry0);
			if (!constants.empty())
				stream.SetComputeShRegs(kComputeUserData0 + pipeline.entryUserData[1], constants.data(), static_cast<uint32_t>(constants.size()));
			// COMPUTE_SHADER_EN | FORCE_START_AT_000 | ORDER_MODE | TUNNEL_ENABLE, CS_W32_EN for wave32.
			stream.DispatchDirect(groups[0], groups[1], groups[2], 0x2045u | (pipeline.wave32 ? 0x8000u : 0u));
			stream.EventWrite(kEventCsPartialFlush);
			return VK_SUCCESS;
		}

		// The UNORM twin of an sRGB format: RPM shaders store linear-to-sRGB-converted values through
		// it (UAV stores can't encode sRGB).
		VkFormat UnormOfSrgb(VkFormat format)
		{
			switch (format)
			{
				case VK_FORMAT_R8_SRGB:
					return VK_FORMAT_R8_UNORM;
				case VK_FORMAT_R8G8_SRGB:
					return VK_FORMAT_R8G8_UNORM;
				case VK_FORMAT_R8G8B8A8_SRGB:
					return VK_FORMAT_R8G8B8A8_UNORM;
				case VK_FORMAT_B8G8R8A8_SRGB:
					return VK_FORMAT_B8G8R8A8_UNORM;
				case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
					return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
				default:
					return format;
			}
		}

		bool IsSrgb(VkFormat format)
		{
			return UnormOfSrgb(format) != format;
		}

		// T# of one subresource (one mip, one layer -- or the whole depth of a 3D mip).
		bool WriteTiledPlaneDescriptor(uint8_t* out, const amd::Image& image, uint64_t planeVa, const amdaddr::gfx11::SurfaceLayout& plane, VkFormat format,
									   uint32_t mipLevel, uint32_t layer);
		uint64_t TiledImageVa(const amd::Image& image);

		bool WriteSubresourceDescriptor(uint8_t* out, const vkd::Image& image, const vkd::SubresourceFootprint& fp, VkFormat format, uint32_t mipLevel,
										uint32_t layer)
		{
			if (const auto* tiled = static_cast<const amd::Image*>(&image); tiled->IsTiled())
			{
				// Blits sample/store the depth plane (D16, D32: the formats with a T# entry).
				const uint64_t imageVa = TiledImageVa(*tiled);
				return imageVa != 0 && tiled->DepthPlane().bytesPerElement != 0 &&
					   WriteTiledPlaneDescriptor(out, *tiled, imageVa, tiled->DepthPlane(), format, mipLevel, layer);
			}
			gfx11::ImageView view{};
			view.address = AmdMemory(image.GetMemory())->GetGpuVa() + fp.offset;
			view.format = format;
			view.viewType = image.GetImageType() == VK_IMAGE_TYPE_3D   ? VK_IMAGE_VIEW_TYPE_3D
							: image.GetImageType() == VK_IMAGE_TYPE_1D ? VK_IMAGE_VIEW_TYPE_1D
																	   : VK_IMAGE_VIEW_TYPE_2D;
			view.width = fp.extent.width;
			view.height = fp.extent.height;
			view.depth = image.GetImageType() == VK_IMAGE_TYPE_3D ? fp.extent.depth : 1;
			view.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
			return gfx11::WriteImageDescriptor(out, view);
		}

		// vkCmdBlitImage with PAL's ScaledCopyImage2d/3d RPM shaders, set up as PAL's
		// RsrcProcMgr::ScaledCopyImageCompute does: an embedded table of {dst T#, src T#, S#} (32-byte
		// slots) then 28 dwords of copy constants, bound to user-data entry 0; one thread per
		// destination texel in 8x8x1 groups. Layers go one dispatch each (Vkd lays out layers of
		// mipmapped images itself).
		VkResult EncodeBlitImage(Pm4Stream& stream, const vkd::Image::OpBlit& op, EncodeState& state, AmdDevice& device)
		{
			if (!IsBound(op.src) || !IsBound(op.dst))
				return VK_ERROR_INITIALIZATION_FAILED;
			const bool is3d = op.src->GetImageType() == VK_IMAGE_TYPE_3D;
			if (is3d != (op.dst->GetImageType() == VK_IMAGE_TYPE_3D))
			{
				cct::Logger::Warning("AmdCommandBuffer: blit between a 3D and a non-3D image not supported yet, skipped");
				return VK_SUCCESS;
			}
			AmdDevice::RpmProgram program;
			if (!device.GetRpmProgram(is3d ? RpmPipelineId::ScaledCopyImage3d : RpmPipelineId::ScaledCopyImage2d, program))
				return VK_ERROR_OUT_OF_DEVICE_MEMORY;

			const VkFormat srcFormat = op.src->GetFormat();
			const VkFormat dstFormat = UnormOfSrgb(op.dst->GetFormat());
			const bool linear = op.filter == VK_FILTER_LINEAR;

			for (const VkImageBlit& r : op.regions)
			{
				// Vulkan offsets pairs -> offset + signed extent, then PAL's
				// ConvertNegativeImageScaledCopyRegion: a reversed destination reverses the source.
				int32_t srcOffset[3] = {r.srcOffsets[0].x, r.srcOffsets[0].y, r.srcOffsets[0].z};
				int32_t srcExtent[3] = {r.srcOffsets[1].x - srcOffset[0], r.srcOffsets[1].y - srcOffset[1], r.srcOffsets[1].z - srcOffset[2]};
				int32_t dstOffset[3] = {r.dstOffsets[0].x, r.dstOffsets[0].y, r.dstOffsets[0].z};
				int32_t dstExtent[3] = {r.dstOffsets[1].x - dstOffset[0], r.dstOffsets[1].y - dstOffset[1], r.dstOffsets[1].z - dstOffset[2]};
				for (int a = 0; a < 3; ++a)
				{
					if (dstExtent[a] < 0)
					{
						dstOffset[a] += dstExtent[a];
						srcOffset[a] += srcExtent[a];
						srcExtent[a] = -srcExtent[a];
						dstExtent[a] = -dstExtent[a];
					}
				}
				if (dstExtent[0] == 0 || dstExtent[1] == 0 || dstExtent[2] == 0)
					continue;

				const uint32_t layers = is3d ? 1 : vkd::ResolveLayerCount(*op.src, r.srcSubresource.baseArrayLayer, r.srcSubresource.layerCount);
				for (uint32_t layer = 0; layer < layers; ++layer)
				{
					const vkd::SubresourceFootprint srcFp = vkd::GetSubresourceFootprint(*op.src, r.srcSubresource.mipLevel, r.srcSubresource.baseArrayLayer + layer);
					const vkd::SubresourceFootprint dstFp = vkd::GetSubresourceFootprint(*op.dst, r.dstSubresource.mipLevel, r.dstSubresource.baseArrayLayer + layer);

					const float srcW = static_cast<float>(srcFp.extent.width);
					const float srcH = static_cast<float>(srcFp.extent.height);
					const float srcD = static_cast<float>(srcFp.extent.depth);
					const float srcLeft = srcOffset[0] / srcW;
					const float srcTop = srcOffset[1] / srcH;
					const float srcRight = (srcOffset[0] + srcExtent[0]) / srcW;
					const float srcBottom = (srcOffset[1] + srcExtent[1]) / srcH;
					const float srcSlice = is3d ? srcOffset[2] / srcD : 0.0f;
					const float srcDepth = is3d ? (srcOffset[2] + srcExtent[2]) / srcD : 1.0f;
					const uint32_t depth = is3d ? static_cast<uint32_t>(dstExtent[2]) : 1;
					const float zOffset = (is3d || linear) ? 0.5f : 0.0f;
					const uint32_t copyData[28] = {
						FloatBits(srcLeft),
						FloatBits(srcTop),
						FloatBits(srcSlice),
						static_cast<uint32_t>(dstExtent[0]),
						static_cast<uint32_t>(dstOffset[0]),
						static_cast<uint32_t>(dstOffset[1]),
						is3d ? static_cast<uint32_t>(dstOffset[2]) : 0u,
						static_cast<uint32_t>(dstExtent[1]),
						FloatBits(srcRight),
						FloatBits(srcBottom),
						FloatBits(srcDepth),
						depth,
						IsSrgb(op.dst->GetFormat()) ? 1u : 0u, // gamma conversion on store
						FloatBits(zOffset),
						1u, // source samples
						0u, // no color key / alpha blend
						FloatBits(1.0f),
						0u,
						0u,
						FloatBits(1.0f),
						0u,
						0u, // rotation: identity
						0u,
						0u,
						0u,
						0u,
						0u,
						0u, // alpha diff mul, threshold, color key
					};

					constexpr uint32_t kSlotBytes = 32; // PAL SrdDwordAlignment() * 4 on GFX11
					uint8_t* table = nullptr;
					const uint64_t tableVa = state.descriptors ? state.descriptors->Allocate(3 * kSlotBytes + sizeof(copyData), table) : 0;
					if (tableVa == 0 || (tableVa >> 32) != (program.codeVa >> 32))
						return VK_ERROR_OUT_OF_POOL_MEMORY;
					std::memset(table, 0, 3 * kSlotBytes);
					if (!WriteSubresourceDescriptor(table, *op.dst, dstFp, dstFormat, r.dstSubresource.mipLevel, r.dstSubresource.baseArrayLayer + layer) ||
						!WriteSubresourceDescriptor(table + kSlotBytes, *op.src, srcFp, srcFormat, r.srcSubresource.mipLevel,
													r.srcSubresource.baseArrayLayer + layer))
					{
						cct::Logger::Warning("AmdCommandBuffer: blit format {} -> {} not supported yet, skipped", static_cast<int>(srcFormat),
											 static_cast<int>(op.dst->GetFormat()));
						return VK_SUCCESS;
					}
					// S# as PAL's RPM sampler: clamp to edge, the blit's filter for x/y, no mip/z
					// filtering, compare func ALWAYS (unused by the sample instruction).
					const uint32_t xyFilter = linear ? 1u : 0u;
					const uint32_t sampler[4] = {2u | (2u << 3) | (2u << 6) | (7u << 12), 0u, (xyFilter << 20) | (xyFilter << 22), 0u};
					std::memcpy(table + 2 * kSlotBytes, sampler, sizeof(sampler));
					std::memcpy(table + 3 * kSlotBytes, copyData, sizeof(copyData));

					const uint32_t groups[3] = {(static_cast<uint32_t>(dstExtent[0]) + 7) / 8, (static_cast<uint32_t>(dstExtent[1]) + 7) / 8, depth};
					if (const VkResult res = EncodeRpmDispatch(stream, program, static_cast<uint32_t>(tableVa), groups, state); res != VK_SUCCESS)
						return res;
				}
			}
			return VK_SUCCESS;
		}

		// The plane an aspect of a tiled image lives in, viewed as the raw unsigned format of its
		// element size; false if the image has no such plane.
		bool GetTiledPlane(const amd::Image& image, VkImageAspectFlags aspect, const amdaddr::gfx11::SurfaceLayout*& plane, VkDeviceSize& planeOffset,
						   VkFormat& rawFormat)
		{
			const bool stencil = aspect == VK_IMAGE_ASPECT_STENCIL_BIT;
			plane = stencil ? &image.StencilPlane() : &image.DepthPlane();
			planeOffset = stencil ? image.StencilPlaneOffset() : 0;
			switch (plane->bytesPerElement)
			{
				case 1:
					rawFormat = VK_FORMAT_R8_UINT;
					return true;
				case 2:
					rawFormat = VK_FORMAT_R16_UINT;
					return true;
				case 4:
					rawFormat = VK_FORMAT_R32_UINT;
					return true;
				default:
					return false;
			}
		}

		// T# of one mip and layer of a tiled plane: the surface's address and mip 0 extent, the
		// hardware finds the subresource.
		bool WriteTiledPlaneDescriptor(uint8_t* out, const amd::Image& image, uint64_t planeVa, const amdaddr::gfx11::SurfaceLayout& plane, VkFormat format,
									   uint32_t mipLevel, uint32_t layer)
		{
			gfx11::ImageView view{};
			view.address = planeVa;
			view.format = format;
			view.viewType = image.GetArrayLayers() > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
			view.width = image.GetExtent().width;
			view.height = image.GetExtent().height;
			view.depth = 1;
			view.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
			view.swizzleMode = plane.swizzleMode;
			view.mipLevels = image.GetMipLevels();
			view.baseLevel = mipLevel;
			view.baseArray = layer;
			return gfx11::WriteImageDescriptor(out, view);
		}

		// Buffer <-> tiled image copies with PAL's CopyMemToImg2d / CopyImgToMem2d, set up as
		// RsrcProcMgr::CopyBetweenMemoryAndImageCs: an embedded table {typed buffer V#, plane T#}
		// (32-byte slots) in user-data entry 0, {x, y, z, row pitch, width, height, depth, depth pitch}
		// (texels) in entries 1-8, one thread per texel in 8x8 groups, one dispatch per layer.
		// GPU address of a tiled image, 0 if its memory can't hold it (the swizzles need 64KB alignment).
		uint64_t TiledImageVa(const amd::Image& image)
		{
			VkMemoryRequirements requirements{};
			image.GetMemoryRequirements(requirements);
			VkDeviceSize imageBytes = 0;
			const uint64_t imageVa = AmdMemory(image.GetMemory())->GetGpuVaAt(image.GetMemoryOffset(), imageBytes);
			if (imageVa == 0 || imageBytes < requirements.size || (imageVa & (amdaddr::gfx11::kBlock64Kb - 1)) != 0)
			{
				cct::Logger::Error("AmdCommandBuffer: tiled image memory not 64KB aligned or too small, skipped");
				return 0;
			}
			return imageVa;
		}

		VkResult EncodeTiledBufferImageCopy(Pm4Stream& stream, const vkd::Buffer& buffer, const amd::Image& image, std::span<const VkBufferImageCopy> regions,
											bool toImage, EncodeState& state)
		{
			AmdDevice::RpmProgram program;
			if (!state.device->GetRpmProgram(toImage ? RpmPipelineId::CopyMemToImg2d : RpmPipelineId::CopyImgToMem2d, program))
				return VK_ERROR_OUT_OF_DEVICE_MEMORY;
			const uint64_t imageVa = TiledImageVa(image);
			if (imageVa == 0)
				return VK_SUCCESS;

			for (const VkBufferImageCopy& r : regions)
			{
				const amdaddr::gfx11::SurfaceLayout* plane = nullptr;
				VkDeviceSize planeOffset = 0;
				VkFormat rawFormat = VK_FORMAT_UNDEFINED;
				if (!GetTiledPlane(image, r.imageSubresource.aspectMask, plane, planeOffset, rawFormat) || r.imageExtent.width == 0 ||
					r.imageExtent.height == 0)
					continue;
				gfx11::ImageFormat rawImgFmt{};
				gfx11::GetImageFormat(rawFormat, rawImgFmt);
				const uint32_t bpe = plane->bytesPerElement;
				const uint32_t rowLength = r.bufferRowLength ? r.bufferRowLength : r.imageExtent.width;
				const uint32_t imageHeight = r.bufferImageHeight ? r.bufferImageHeight : r.imageExtent.height;
				const uint64_t elements = uint64_t{rowLength} * (r.imageExtent.height - 1) + r.imageExtent.width;
				const uint64_t layerBytes = uint64_t{rowLength} * imageHeight * bpe;
				const uint32_t layers = vkd::ResolveLayerCount(image, r.imageSubresource.baseArrayLayer, r.imageSubresource.layerCount);
				const uint32_t copyData[8] = {static_cast<uint32_t>(r.imageOffset.x),
											  static_cast<uint32_t>(r.imageOffset.y),
											  0u,
											  rowLength,
											  r.imageExtent.width,
											  r.imageExtent.height,
											  1u,
											  rowLength * imageHeight};
				for (uint32_t layer = 0; layer < layers; ++layer)
				{
					const VkDeviceSize bufferOffset = buffer.GetMemoryOffset() + r.bufferOffset + layer * layerBytes;
					VkDeviceSize bufferBytes = 0;
					const uint64_t bufferVa = AmdMemory(buffer.GetMemory())->GetGpuVaAt(bufferOffset, bufferBytes);
					if (bufferVa == 0 || bufferBytes < elements * bpe)
					{
						cct::Logger::Error("AmdCommandBuffer: buffer range of a tiled image copy out of its memory, skipped");
						return VK_SUCCESS;
					}

					constexpr uint32_t kSlotBytes = 32; // PAL SrdDwordAlignment() * 4 on GFX11
					uint8_t* table = nullptr;
					const uint64_t tableVa = state.descriptors ? state.descriptors->Allocate(2 * kSlotBytes, table) : 0;
					if (tableVa == 0 || (tableVa >> 32) != (program.codeVa >> 32))
						return VK_ERROR_OUT_OF_POOL_MEMORY;
					std::memset(table, 0, 2 * kSlotBytes);
					gfx11::WriteTypedBufferDescriptor(table, bufferVa, elements, bpe, rawImgFmt.imgFmt);
					if (!WriteTiledPlaneDescriptor(table + kSlotBytes, image, imageVa + planeOffset, *plane, rawFormat, r.imageSubresource.mipLevel,
												   r.imageSubresource.baseArrayLayer + layer))
						return VK_ERROR_FORMAT_NOT_SUPPORTED;

					const uint32_t groups[3] = {(r.imageExtent.width + 7) / 8, (r.imageExtent.height + 7) / 8, 1};
					if (const VkResult res = EncodeRpmDispatch(stream, program, static_cast<uint32_t>(tableVa), groups, state, copyData); res != VK_SUCCESS)
						return res;
				}
			}
			return VK_SUCCESS;
		}

		// T# of one aspect, mip and layer of an image viewed with a raw format, tiled or linear.
		bool WriteRawAspectDescriptor(uint8_t* out, const vkd::Image& image, VkImageAspectFlagBits aspect, uint32_t mipLevel, uint32_t layer,
									  VkFormat rawFormat)
		{
			if (const auto* tiled = static_cast<const amd::Image*>(&image); tiled->IsTiled())
			{
				const amdaddr::gfx11::SurfaceLayout* plane = nullptr;
				VkDeviceSize planeOffset = 0;
				VkFormat planeFormat = VK_FORMAT_UNDEFINED;
				const uint64_t imageVa = TiledImageVa(*tiled);
				return imageVa != 0 && GetTiledPlane(*tiled, aspect, plane, planeOffset, planeFormat) && planeFormat == rawFormat &&
					   WriteTiledPlaneDescriptor(out, *tiled, imageVa + planeOffset, *plane, rawFormat, mipLevel, layer);
			}
			if (vkuFormatElementSize(image.GetFormat()) != vkuFormatElementSize(rawFormat))
				return false; // one aspect of a linear combined depth/stencil texel
			// A linear subresource as a 2D image at its address (same rows, 256-byte aligned), as CopyImage2d
			// reads and writes it: a 1D/2D layer, or a z slice of a 3D mip (layer = z).
			const bool is3d = image.GetImageType() == VK_IMAGE_TYPE_3D;
			const vkd::SubresourceFootprint fp = vkd::GetSubresourceFootprint(image, mipLevel, is3d ? 0 : layer);
			if (is3d && layer >= fp.extent.depth)
				return false;
			gfx11::ImageView view{};
			view.address = AmdMemory(image.GetMemory())->GetGpuVa() + fp.offset + (is3d ? VkDeviceSize{layer} * fp.depthPitch : 0);
			view.format = rawFormat;
			view.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view.width = fp.extent.width;
			view.height = fp.extent.height;
			view.depth = 1;
			view.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
			return gfx11::WriteImageDescriptor(out, view);
		}

		// vkCmdCopyImage with a tiled side, through PAL's CopyImage2d as RsrcProcMgr::CopyImageCompute
		// sets it up (mip in the descriptors): an embedded table {dst T#, src T#} in user-data entry 0,
		// {src offset, samples, dst offset, packed mips, width, height} in entries 1-10, 8x8 groups. Both
		// sides are viewed with the raw format of the aspect's element, one dispatch per layer.
		VkResult EncodeTiledImageCopy(Pm4Stream& stream, const vkd::Image::OpCopy& op, EncodeState& state)
		{
			// Tiled images are 1D or 2D (1D ones laid out as 2D W x 1); the other side may be a linear 3D
			// image (2D <-> 3D copies: its z slices pair with the 2D side's layers).
			const bool src3d = op.src->GetImageType() == VK_IMAGE_TYPE_3D;
			const bool dst3d = op.dst->GetImageType() == VK_IMAGE_TYPE_3D;
			AmdDevice::RpmProgram program;
			if (!state.device->GetRpmProgram(RpmPipelineId::CopyImage2d, program))
				return VK_ERROR_OUT_OF_DEVICE_MEMORY;
			const amd::Image& tiled = *static_cast<const amd::Image*>(static_cast<const amd::Image*>(op.src)->IsTiled() ? op.src : op.dst);
			for (const VkImageCopy& r : op.regions)
			{
				if (r.extent.width == 0 || r.extent.height == 0)
					continue;
				const uint32_t layers = src3d ? r.extent.depth : vkd::ResolveLayerCount(*op.src, r.srcSubresource.baseArrayLayer, r.srcSubresource.layerCount);
				const uint32_t srcFirst = src3d ? static_cast<uint32_t>(r.srcOffset.z) : r.srcSubresource.baseArrayLayer;
				const uint32_t dstFirst = dst3d ? static_cast<uint32_t>(r.dstOffset.z) : r.dstSubresource.baseArrayLayer;
				const VkResult res = ForEachTiledAspect(tiled, r.srcSubresource.aspectMask, [&](VkImageAspectFlagBits aspect) -> VkResult
														{
					const amdaddr::gfx11::SurfaceLayout* plane = nullptr;
					VkDeviceSize planeOffset = 0;
					VkFormat rawFormat = VK_FORMAT_UNDEFINED;
					if (!GetTiledPlane(tiled, aspect, plane, planeOffset, rawFormat))
						return VK_SUCCESS;
					// srcOffset (z = slice, 0: the layer is in the T#), samples, dstOffset, packed mips, extent.
					const uint32_t constants[10] = {static_cast<uint32_t>(r.srcOffset.x), static_cast<uint32_t>(r.srcOffset.y), 0u, 1u,
													static_cast<uint32_t>(r.dstOffset.x), static_cast<uint32_t>(r.dstOffset.y), 0u,
													r.srcSubresource.mipLevel | (r.dstSubresource.mipLevel << 16), r.extent.width, r.extent.height};
					for (uint32_t layer = 0; layer < layers; ++layer)
					{
						constexpr uint32_t kSlotBytes = 32;
						uint8_t* table = nullptr;
						const uint64_t tableVa = state.descriptors ? state.descriptors->Allocate(2 * kSlotBytes, table) : 0;
						if (tableVa == 0 || (tableVa >> 32) != (program.codeVa >> 32))
							return VK_ERROR_OUT_OF_POOL_MEMORY;
						std::memset(table, 0, 2 * kSlotBytes);
						if (!WriteRawAspectDescriptor(table, *op.dst, aspect, r.dstSubresource.mipLevel, dstFirst + layer, rawFormat) ||
							!WriteRawAspectDescriptor(table + kSlotBytes, *op.src, aspect, r.srcSubresource.mipLevel, srcFirst + layer, rawFormat))
						{
							cct::Logger::Warning("AmdCommandBuffer: image copy with a tiled side not supported for these images, skipped");
							return VK_SUCCESS;
						}
						const uint32_t groups[3] = {(r.extent.width + 7) / 8, (r.extent.height + 7) / 8, 1};
						if (const VkResult e = EncodeRpmDispatch(stream, program, static_cast<uint32_t>(tableVa), groups, state, constants); e != VK_SUCCESS)
							return e;
					}
					return VK_SUCCESS; });
				if (res != VK_SUCCESS)
					return res;
			}
			return VK_SUCCESS;
		}

		// A depth/stencil clear value as a plane's raw element: D32 float bits, D16 unorm, stencil byte.
		uint32_t PackPlaneClearValue(VkImageAspectFlagBits aspect, uint32_t bytesPerElement, const VkClearDepthStencilValue& value)
		{
			if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT)
				return value.stencil & 0xFFu;
			const float depth = std::clamp(value.depth, 0.0f, 1.0f);
			if (bytesPerElement == 2)
				return static_cast<uint32_t>(std::lround(depth * 65535.0f));
			return FloatBits(depth);
		}

		// Clears a rectangle of one mip, some layers, of a tiled plane with PAL's ClearImage, set up as
		// RsrcProcMgr::SlowClearCompute / ClearImageCs: the packed value in user-data entries 0-3, the
		// packed constants (8x8x1 thread groups, always the windowed path) in 4, the address of the
		// "slow constants" {disable mask, first texel, last texel} in 5, the T# in 6-13.
		VkResult EncodeTiledClear(Pm4Stream& stream, const amd::Image& image, VkImageAspectFlagBits aspect, const VkClearDepthStencilValue& value,
								  uint32_t mipLevel, uint32_t baseLayer, uint32_t layerCount, const VkRect2D& rect, EncodeState& state)
		{
			const amdaddr::gfx11::SurfaceLayout* plane = nullptr;
			VkDeviceSize planeOffset = 0;
			VkFormat rawFormat = VK_FORMAT_UNDEFINED;
			if (!GetTiledPlane(image, aspect, plane, planeOffset, rawFormat) || mipLevel >= image.GetMipLevels())
				return VK_SUCCESS;
			const uint64_t imageVa = TiledImageVa(image);
			if (imageVa == 0)
				return VK_SUCCESS;
			AmdDevice::RpmProgram program;
			if (!state.device->GetRpmProgram(RpmPipelineId::ClearImage, program))
				return VK_ERROR_OUT_OF_DEVICE_MEMORY;

			const uint32_t mipWidth = std::max(image.GetExtent().width >> mipLevel, 1u);
			const uint32_t mipHeight = std::max(image.GetExtent().height >> mipLevel, 1u);
			const uint32_t x0 = static_cast<uint32_t>(std::clamp<int64_t>(rect.offset.x, 0, mipWidth));
			const uint32_t y0 = static_cast<uint32_t>(std::clamp<int64_t>(rect.offset.y, 0, mipHeight));
			const uint32_t x1 = static_cast<uint32_t>(std::clamp<int64_t>(int64_t{rect.offset.x} + rect.extent.width, 0, mipWidth));
			const uint32_t y1 = static_cast<uint32_t>(std::clamp<int64_t>(int64_t{rect.offset.y} + rect.extent.height, 0, mipHeight));
			if (x0 >= x1 || y0 >= y1)
				return VK_SUCCESS;
			const uint32_t packed = PackPlaneClearValue(aspect, plane->bytesPerElement, value);
			constexpr uint32_t kPackedConsts = 3u | (3u << 8); // log2 threads 8x8x1, not the fast path, unmasked, 1 sample
			const uint32_t groups[3] = {(x1 - (x0 & ~7u) + 7) / 8, (y1 - (y0 & ~7u) + 7) / 8, 1};

			for (uint32_t layer = 0; layer < layerCount; ++layer)
			{
				uint8_t* slow = nullptr;
				const uint64_t slowVa = state.descriptors ? state.descriptors->Allocate(64, slow) : 0;
				if (slowVa == 0 || (slowVa >> 32) != (program.codeVa >> 32))
					return VK_ERROR_OUT_OF_POOL_MEMORY;
				const uint32_t slowConsts[12] = {0, 0, 0, 0, x0, y0, 0, 0, x1 - 1, y1 - 1, 0, 0};
				std::memcpy(slow, slowConsts, sizeof(slowConsts));

				uint32_t constants[13] = {0, 0, 0, kPackedConsts, static_cast<uint32_t>(slowVa)};
				if (!WriteTiledPlaneDescriptor(reinterpret_cast<uint8_t*>(constants + 5), image, imageVa + planeOffset, *plane, rawFormat, mipLevel,
											   baseLayer + layer))
					return VK_ERROR_FORMAT_NOT_SUPPORTED;
				if (const VkResult res = EncodeRpmDispatch(stream, program, packed, groups, state, constants); res != VK_SUCCESS)
					return res;
			}
			return VK_SUCCESS;
		}

		// Every aspect of a tiled image a mask selects, among the planes it has.
		template<typename Fn>
		VkResult ForEachTiledAspect(const amd::Image& image, VkImageAspectFlags mask, Fn&& fn)
		{
			for (const VkImageAspectFlagBits aspect : {VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_ASPECT_STENCIL_BIT})
			{
				const bool present = aspect == VK_IMAGE_ASPECT_DEPTH_BIT ? image.DepthPlane().bytesPerElement != 0 : image.StencilPlane().bytesPerElement != 0;
				if ((mask & aspect) && present)
					if (const VkResult r = fn(aspect); r != VK_SUCCESS)
						return r;
			}
			return VK_SUCCESS;
		}

		uint32_t ScissorCoord(int64_t v)
		{
			return static_cast<uint32_t>(std::clamp<int64_t>(v, 0, 16384));
		}

		// vkCmdDraw with a replayed pipeline (PrecompiledGraphics.hpp): CLEAR_STATE and the shared
		// context once, then every register write AMDVLK's draw relied on with what depends on the
		// Vulkan state patched (addresses, target extent, viewport, scissor, cull mode, descriptor
		// tables), the draw, and AMDVLK's post-draw barrier. Color attachment 0 of subpass 0 is the
		// target; it must have the format the pipeline was captured with.
		// vkCmdDraw / vkCmdDrawIndexed parameters.
		struct DrawParams
		{
			uint32_t count; // vertices, or indices
			uint32_t instanceCount;
			int32_t baseVertex; // firstVertex, or vertexOffset
			uint32_t firstInstance;
			bool indexed;
			uint32_t firstIndex;
		};

		// The subpass's depth/stencil attachment as PAL's DepthStencilView programs an image without
		// HTILE (Gfx10DepthStencilView::InitRegisters), then the pipeline's tests (DepthStencil.hpp).
		// Without an attachment the replayed state stands: DB_Z_INFO invalid, tests off.
		void EmitDepthStencilTarget(Pm4Stream& stream, const VkSubpassDescription& subpass, std::span<const VkImageView> views, const GraphicsPipelineState& gs,
									const EncodeState& state)
		{
			if (!subpass.pDepthStencilAttachment || subpass.pDepthStencilAttachment->attachment >= views.size())
				return;
			VKD_FROM_HANDLE(vkd::ImageView, view, views[subpass.pDepthStencilAttachment->attachment]);
			VKD_FROM_HANDLE(vkd::Image, image, view->GetImage());
			const auto* tiled = static_cast<const amd::Image*>(image);
			const uint64_t imageVa = IsBound(image) && tiled->IsTiled() ? TiledImageVa(*tiled) : 0;
			if (imageVa == 0)
			{
				static bool warned = false;
				if (!warned)
				{
					warned = true;
					cct::Logger::Warning("AmdCommandBuffer: depth/stencil attachment that isn't a tiled image, ignored");
				}
				return;
			}
			const amdaddr::gfx11::SurfaceLayout& depth = tiled->DepthPlane();
			const amdaddr::gfx11::SurfaceLayout& stencil = tiled->StencilPlane();
			const uint64_t zVa = depth.bytesPerElement ? imageVa : 0;
			const uint64_t sVa = stencil.bytesPerElement ? imageVa + tiled->StencilPlaneOffset() : 0;
			const VkImageSubresourceRange& range = view->GetSubresourceRange();
			const uint32_t firstLayer = range.baseArrayLayer;
			const uint32_t lastLayer = firstLayer + vkd::ResolveLayerCount(*image, range.baseArrayLayer, range.layerCount) - 1;

			// Z_16 / Z_32_FLOAT, SW_MODE, MAXMIP, ITERATE_256; no TILE_SURFACE_ENABLE (HTILE).
			const uint32_t zFormat = depth.bytesPerElement == 4 ? 3u : (depth.bytesPerElement == 2 ? 1u : 0u);
			const uint32_t zInfo = zFormat | (amdaddr::gfx11::kSwMode64KbZX << 4) | ((image->GetMipLevels() - 1) << 16) | (1u << 20);
			// STENCIL_8, SW_MODE, ITERATE_256, TILE_STENCIL_DISABLE.
			const uint32_t stencilInfo = (stencil.bytesPerElement ? 1u : 0u) | (amdaddr::gfx11::kSwMode64KbZX << 4) | (1u << 20) | (1u << 29);
			// SLICE_START(_HI), SLICE_MAX(_HI), MIPID.
			const uint32_t depthView = (firstLayer & 0x7FFu) | (((firstLayer >> 11) & 3u) << 11) | ((lastLayer & 0x7FFu) << 13) |
									   ((range.baseMipLevel & 0xFu) << 26) | (((lastLayer >> 11) & 3u) << 30);
			const DepthStencilRegs& ds = gs.depthStencil;
			// The pipeline's stencil values and depth bounds, or the dynamic ones.
			StencilFaceValues faces[2] = {ds.front, ds.back};
			for (int face = 0; face < 2; ++face)
			{
				if (gs.dynamicStencilCompareMask)
					faces[face].compareMask = state.stencil[face].compareMask;
				if (gs.dynamicStencilWriteMask)
					faces[face].writeMask = state.stencil[face].writeMask;
				if (gs.dynamicStencilReference)
					faces[face].reference = state.stencil[face].reference;
			}
			const float boundsMin = gs.dynamicDepthBounds ? state.depthBounds[0] : ds.depthBoundsMin;
			const float boundsMax = gs.dynamicDepthBounds ? state.depthBounds[1] : ds.depthBoundsMax;

			stream.SetContextReg(kDbRenderControl, 0x60); // DEPTH_COMPRESS_DISABLE | STENCIL_COMPRESS_DISABLE
			stream.SetContextReg(kDbDepthView, depthView);
			stream.SetContextReg(kDbRenderOverride, 0x2A); // FORCE_HIZ_ENABLE, FORCE_HIS_ENABLE0/1 = FORCE_DISABLE
			stream.SetContextReg(kDbHtileDataBase, 0);
			const uint32_t sizeAndBounds[3] = {(image->GetExtent().width - 1) | ((image->GetExtent().height - 1) << 16), FloatBits(boundsMin),
											   FloatBits(boundsMax)};
			stream.SetContextRegs(kDbDepthSizeXy, sizeAndBounds, 3);
			const uint32_t bases[6] = {zInfo,
									   stencilInfo,
									   static_cast<uint32_t>(zVa >> 8),
									   static_cast<uint32_t>(sVa >> 8),
									   static_cast<uint32_t>(zVa >> 8),
									   static_cast<uint32_t>(sVa >> 8)};
			stream.SetContextRegs(kDbZInfo, bases, 6);
			const uint32_t basesHi[5] = {static_cast<uint32_t>(zVa >> 40), static_cast<uint32_t>(sVa >> 40), static_cast<uint32_t>(zVa >> 40),
										 static_cast<uint32_t>(sVa >> 40), 0};
			stream.SetContextRegs(kDbZReadBaseHi, basesHi, 5);
			const uint32_t stencilRegs[3] = {ds.dbStencilControl, depthstencil::RefMask(faces[0]), depthstencil::RefMask(faces[1])};
			stream.SetContextRegs(kDbStencilControl, stencilRegs, 3);
			stream.SetContextReg(kDbDepthControl, ds.dbDepthControl);
			// POLY_OFFSET_NEG_NUM_DB_BITS, POLY_OFFSET_DB_IS_FLOAT_FMT: -23 and float for D32; -15 for D16 --
			// PAL's lowZplanePolyOffsetBits, which AMDVLK sets for its draws (a D16 bias unit is 2^-15) --;
			// AMDVLK's -24 without depth.
			stream.SetContextReg(kPaSuPolyOffsetDbFmtCntl, depth.bytesPerElement == 4 ? 0x1E9u : (depth.bytesPerElement == 2 ? 0xF1u : 0xE8u));
			// Depth bias as PAL's CmdSetDepthBiasState: clamp, then front/back slope (x16, the hardware
			// counts 1/16 pixel) and constant; PA_SU_SC_MODE_CNTL enables it (CullMode patch).
			const DepthBias& bias = gs.dynamicDepthBias ? state.depthBias : gs.depthBias;
			const float slope = gs.depthBiasEnable ? bias.slopeFactor * 16.0f : 0.0f;
			const float constant = gs.depthBiasEnable ? bias.constantFactor : 0.0f;
			const uint32_t polyOffset[5] = {FloatBits(gs.depthBiasEnable ? bias.clamp : 0.0f), FloatBits(slope), FloatBits(constant), FloatBits(slope),
											FloatBits(constant)};
			stream.SetContextRegs(kPaSuPolyOffsetClamp, polyOffset, 5);
		}

		VkResult EncodeDraw(Pm4Stream& stream, const DrawParams& op, EncodeState& state)
		{
			const amd::Pipeline* pipeline = state.graphicsPipeline;
			const PrecompiledGraphicsPipeline* graphics = pipeline ? pipeline->GetGraphics() : nullptr;
			const vkd::OpBeginRenderPass* pass = state.renderPass;
			if (!graphics || pipeline->GetCodeVa() == 0 || !pass || !pass->RenderPassObject || !pass->FramebufferObject ||
				pass->RenderPassObject->GetSubpasses().empty() || state.computeEngine)
			{
				static bool warned = false;
				if (!warned)
				{
					warned = true;
					cct::Logger::Warning("AmdCommandBuffer: draw without GFX11 code for its pipeline (or outside a render pass), skipped");
				}
				return VK_SUCCESS;
			}
			if (op.count == 0 || op.instanceCount == 0)
				return VK_SUCCESS;

			// Indices: INDEX_BASE at the first index, MAX_SIZE = what's left in the buffer.
			uint64_t indexVa = 0;
			uint32_t maxIndices = 0;
			if (op.indexed)
			{
				if (!IsBound(state.indexBuffer) || (state.indexType != VK_INDEX_TYPE_UINT16 && state.indexType != VK_INDEX_TYPE_UINT32))
				{
					cct::Logger::Warning("AmdCommandBuffer: indexed draw without a bound 16/32-bit index buffer, skipped");
					return VK_SUCCESS;
				}
				const uint64_t indexSize = state.indexType == VK_INDEX_TYPE_UINT32 ? 4 : 2;
				const uint64_t available = state.indexOffset < state.indexBuffer->GetSize() ? (state.indexBuffer->GetSize() - state.indexOffset) / indexSize : 0;
				maxIndices = static_cast<uint32_t>(available > op.firstIndex ? std::min<uint64_t>(available - op.firstIndex, UINT32_MAX) : 0);
				indexVa = AmdMemory(state.indexBuffer->GetMemory())->GetGpuVa() + state.indexBuffer->GetMemoryOffset() + state.indexOffset +
						  uint64_t{op.firstIndex} * indexSize;
			}

			const VkSubpassDescription& subpass = pass->RenderPassObject->GetSubpasses()[0];
			const auto& views = pass->FramebufferObject->GetAttachments();
			if (subpass.colorAttachmentCount == 0 || subpass.pColorAttachments[0].attachment >= views.size())
				return VK_SUCCESS;
			VKD_FROM_HANDLE(vkd::ImageView, view, views[subpass.pColorAttachments[0].attachment]);
			VKD_FROM_HANDLE(vkd::Image, image, view->GetImage());
			if (!IsBound(image))
				return VK_ERROR_INITIALIZATION_FAILED;
			if (view->GetFormat() != graphics->colorFormat)
			{
				static bool warned = false;
				if (!warned)
				{
					warned = true;
					cct::Logger::Warning("AmdCommandBuffer: draw into a format {} target with a pipeline captured for format {}, skipped",
										 static_cast<int>(view->GetFormat()), static_cast<int>(graphics->colorFormat));
				}
				return VK_SUCCESS;
			}
			const VkImageSubresourceRange& range = view->GetSubresourceRange();
			const vkd::SubresourceFootprint fp = vkd::GetSubresourceFootprint(*image, range.baseMipLevel, range.baseArrayLayer);
			const uint64_t targetVa = AmdMemory(image->GetMemory())->GetGpuVa() + fp.offset;
			if (targetVa == 0 || state.attributeRingVa == 0 || state.shaderRingTable == 0)
				return VK_ERROR_DEVICE_LOST;

			uint32_t setTables[kMaxPrecompiledSets] = {};
			for (uint32_t set = 0; set < kMaxPrecompiledSets; ++set)
			{
				const bool used = std::any_of(graphics->descriptors.begin(), graphics->descriptors.end(),
											  [&](const PrecompiledDescriptor& d)
											  { return d.set == set; });
				if (!used)
					continue;
				setTables[set] = BuildDescriptorTable(graphics->descriptors, set, state.graphicsSets[set], state.graphicsDynamicOffsets, pipeline->GetCodeVa(),
													  state.descriptors);
				if (setTables[set] == 0)
				{
					cct::Logger::Error("AmdCommandBuffer: no room for the draw's descriptor tables in the shader VA window");
					return VK_ERROR_OUT_OF_DEVICE_MEMORY;
				}
			}

			const GraphicsPipelineState& gs = pipeline->GetGraphicsState();
			uint32_t vertexBufferTable = 0;
			if (gs.vertexBindingCount > 0)
			{
				vertexBufferTable = BuildVertexBufferTable(gs, state, pipeline->GetCodeVa());
				if (vertexBufferTable == 0)
				{
					cct::Logger::Error("AmdCommandBuffer: no room for the draw's vertex buffer table in the shader VA window");
					return VK_ERROR_OUT_OF_DEVICE_MEMORY;
				}
			}
			auto pushConstantDword = [&](uint32_t reg) -> uint32_t
			{
				for (const PrecompiledPushConstants& range : graphics->pushConstants)
				{
					if (reg < range.firstReg || reg >= range.firstReg + range.dwords)
						continue;
					const size_t offset = range.byteOffset + size_t{reg - range.firstReg} * 4;
					uint32_t dword = 0;
					if (offset + 4 <= sizeof(state.pushConstants))
						std::memcpy(&dword, state.pushConstants + offset, sizeof(dword));
					return dword;
				}
				return 0;
			};
			const VkViewport& viewport = gs.dynamicViewport ? state.viewport : gs.viewport;
			const VkRect2D& scissor = gs.dynamicScissor ? state.scissor : gs.scissor;
			const ViewportRegs vp = ComputeViewportRegs(viewport);

			if (!stream.HasRoom(130 + uint32_t(std::size(kGraphicsInitState) + graphics->drawState.size()) * 3))
				return VK_ERROR_OUT_OF_DEVICE_MEMORY;
			if (!state.shaderCachesInvalidated)
			{
				stream.AcquireMemInvalidateShaderCaches(false);
				state.shaderCachesInvalidated = true;
			}

			auto emit = [&](const RegWrite& w)
			{
				uint32_t value = w.value;
				switch (w.patch)
				{
					case RegPatch::None:
						break;
					case RegPatch::EsCodeLo:
						value = static_cast<uint32_t>(pipeline->GetCodeVa() >> 8);
						break;
					case RegPatch::PsCodeLo:
						value = static_cast<uint32_t>(pipeline->GetPsCodeVa() >> 8);
						break;
					case RegPatch::ColorBase:
						value = static_cast<uint32_t>(targetVa >> 8);
						break;
					case RegPatch::ColorSize:
						value = ((fp.extent.height - 1) & 0x3FFFu) | (((fp.extent.width - 1) & 0x3FFFu) << 14);
						break;
					case RegPatch::AttributeRingBase:
						value = static_cast<uint32_t>(state.attributeRingVa >> 16);
						break;
					case RegPatch::ShaderRingTable:
						value = state.shaderRingTable;
						break;
					case RegPatch::SetTable0:
						value = setTables[0];
						break;
					case RegPatch::SetTable1:
						value = setTables[1];
						break;
					case RegPatch::SetTable2:
						value = setTables[2];
						break;
					case RegPatch::SetTable3:
						value = setTables[3];
						break;
					case RegPatch::TargetExtent:
						value = ScissorCoord(fp.extent.width) | (ScissorCoord(fp.extent.height) << 16);
						break;
					case RegPatch::VportXScale:
						value = FloatBits(vp.xScale);
						break;
					case RegPatch::VportXOffset:
						value = FloatBits(vp.xOffset);
						break;
					case RegPatch::VportYScale:
						value = FloatBits(vp.yScale);
						break;
					case RegPatch::VportYOffset:
						value = FloatBits(vp.yOffset);
						break;
					case RegPatch::VportZScale:
						value = FloatBits(vp.zScale);
						break;
					case RegPatch::VportZOffset:
						value = FloatBits(vp.zOffset);
						break;
					case RegPatch::VportZMin:
						value = FloatBits(vp.zMin);
						break;
					case RegPatch::VportZMax:
						value = FloatBits(vp.zMax);
						break;
					case RegPatch::GbVertClip:
						value = FloatBits(vp.vertClip);
						break;
					case RegPatch::GbHorzClip:
						value = FloatBits(vp.horzClip);
						break;
					case RegPatch::GbVertDisc:
					case RegPatch::GbHorzDisc:
						value = FloatBits(1.0f);
						break;
					case RegPatch::ScreenOffset:
						value = vp.screenOffset;
						break;
					case RegPatch::ScissorTl:
						value = ScissorCoord(scissor.offset.x) | (ScissorCoord(scissor.offset.y) << 16) | 0x80000000u; // WINDOW_OFFSET_DISABLE
						break;
					case RegPatch::ScissorBr:
						value = ScissorCoord(int64_t{scissor.offset.x} + scissor.extent.width) |
								(ScissorCoord(int64_t{scissor.offset.y} + scissor.extent.height) << 16);
						break;
					case RegPatch::CullMode:
						value = (value & ~(7u | kPaSuScModeCntlPolyOffsetEnable)) | ((gs.cullMode & VK_CULL_MODE_FRONT_BIT) ? 1u : 0u) |
								((gs.cullMode & VK_CULL_MODE_BACK_BIT) ? 2u : 0u) | (gs.frontFace == VK_FRONT_FACE_CLOCKWISE ? 4u : 0u) |
								(gs.depthBiasEnable ? kPaSuScModeCntlPolyOffsetEnable : 0u);
						break;
					case RegPatch::BaseVertex:
						value = static_cast<uint32_t>(op.baseVertex);
						break;
					case RegPatch::BaseInstance:
						value = op.firstInstance;
						break;
					case RegPatch::IndexType:
						value = (value & ~3u) | (!op.indexed ? 2u : state.indexType == VK_INDEX_TYPE_UINT32 ? 1u
																											: 0u);
						break;
					case RegPatch::VertexBufferTable:
						value = vertexBufferTable;
						break;
					case RegPatch::DbCountControl:
						// PAL UpdateDbCountControl: z-pass counting while an occlusion query is active.
						if (!state.activeOcclusion.empty())
							value |= 0x102u; // PERFECT_ZPASS_COUNTS | ZPASS_ENABLE
						break;
					case RegPatch::PushConstant:
						value = pushConstantDword(w.reg);
						break;
				}
				switch (w.kind)
				{
					case RegKind::Context:
						stream.SetContextReg(w.reg, value);
						break;
					case RegKind::Sh:
						stream.SetGraphicsShReg(w.reg, value);
						break;
					case RegKind::ShIndex:
						stream.SetGraphicsShRegCuMasked(w.reg, value);
						break;
					case RegKind::Uconfig:
						stream.SetUconfigReg(w.reg, value);
						break;
					case RegKind::UconfigIndex2:
						stream.SetUconfigReg(w.reg, value, 2);
						break;
				}
			};
			// The context AMDVLK starts every submit from: golden defaults, then its saved queue state.
			if (!state.contextCleared)
			{
				stream.ClearState();
				for (const RegWrite& w : kGraphicsInitState)
					emit(w);
				state.contextCleared = true;
			}
			for (const RegWrite& w : graphics->drawState)
				emit(w);
			// The pipeline's color blend state over the captured one: SX_MRT0..7_BLEND_OPT and
			// CB_BLEND0..7_CONTROL are consecutive, as PAL writes them.
			uint32_t blendControl[2 * kMaxColorTargets];
			std::copy_n(gs.blend.sxMrtBlendOpt, kMaxColorTargets, blendControl);
			std::copy_n(gs.blend.cbBlendControl, kMaxColorTargets, blendControl + kMaxColorTargets);
			stream.SetContextRegs(kSxMrt0BlendOpt, blendControl, 2 * kMaxColorTargets);
			stream.SetContextReg(kCbTargetMask, gs.blend.cbTargetMask);
			stream.SetContextReg(kSxBlendOptControl, gs.blend.sxBlendOptControl);
			stream.SetContextReg(kCbColorControl, gs.cbColorControl);
			const float* constants = gs.dynamicBlendConstants ? state.blendConstants : gs.blendConstants;
			const uint32_t constantBits[4] = {FloatBits(constants[0]), FloatBits(constants[1]), FloatBits(constants[2]), FloatBits(constants[3])};
			stream.SetContextRegs(kCbBlendRed, constantBits, 4);
			EmitDepthStencilTarget(stream, subpass, views, gs, state);
			if (op.indexed)
				stream.DrawIndex2(op.instanceCount, maxIndices, indexVa, op.count);
			else
				stream.DrawIndexAuto(op.instanceCount, op.count, graphics->drawInitiator);
			stream.PostDrawBarrier();
			return VK_SUCCESS;
		}

		// vkCmdDispatch: the shader registers AMDVLK programs for the same pipeline, push constants in
		// COMPUTE_USER_DATA_n, DISPATCH_DIRECT, then CS_PARTIAL_FLUSH so the next packets (CP DMA,
		// the EOP postamble) see the results.
		VkResult EncodeDispatch(Pm4Stream& stream, const vkd::OpDispatch& op, EncodeState& state)
		{
			const amd::Pipeline* pipeline = state.computePipeline;
			const PrecompiledComputeShader* shader = pipeline ? pipeline->GetComputeShader() : nullptr;
			if (!shader || pipeline->GetCodeVa() == 0)
			{
				static bool warned = false;
				if (!warned)
				{
					warned = true;
					cct::Logger::Warning("AmdCommandBuffer: dispatch without GFX11 code for its pipeline, skipped");
				}
				return VK_SUCCESS;
			}
			if (op.GroupCountX == 0 || op.GroupCountY == 0 || op.GroupCountZ == 0)
				return VK_SUCCESS;
			if (!stream.HasRoom(96 + shader->pushConstantDwords))
				return VK_ERROR_OUT_OF_DEVICE_MEMORY;

			const uint64_t codeVa = pipeline->GetCodeVa();
			uint32_t tablePointers[kMaxPrecompiledSets] = {};
			if (!BuildDescriptorTables(*shader, codeVa, state, tablePointers))
			{
				cct::Logger::Error("AmdCommandBuffer: no room for the dispatch's descriptor tables in the shader VA window");
				return VK_ERROR_OUT_OF_DEVICE_MEMORY;
			}

			// Also covers the descriptor tables just written through the CPU: s_load reads them
			// through the scalar cache.
			if (!state.shaderCachesInvalidated)
			{
				stream.AcquireMemInvalidateShaderCaches(state.computeEngine);
				state.shaderCachesInvalidated = true;
			}

			const uint32_t pgm[2] = {static_cast<uint32_t>(codeVa >> 8), static_cast<uint32_t>(codeVa >> 40)};
			const uint32_t numThreads[3] = {shader->numThreadX, shader->numThreadY, shader->numThreadZ};
			const uint32_t rsrc12[2] = {shader->pgmRsrc1, shader->pgmRsrc2};
			const uint32_t start[3] = {0, 0, 0};
			stream.SetComputeShRegs(kComputePgmLo, pgm, 2);
			stream.SetComputeShRegs(kComputePgmRsrc1, rsrc12, 2);
			stream.SetComputeShReg(kComputePgmRsrc3, shader->pgmRsrc3);
			stream.SetComputeShRegs(kComputeNumThreadX, numThreads, 3);
			stream.SetComputeShRegs(kComputeStartX, start, 3);
			stream.SetComputeShReg(kComputeResourceLimits, 0);
			stream.SetComputeShReg(kComputeDispatchInterleave, 0x40); // AMDVLK's value
			stream.SetComputeShReg(kComputeTmpringSize, 0x100); // AMDVLK's value: no scratch
			stream.SetComputeShReg(kComputePerfcountEnable, 0);
			stream.SetComputeShRegsCuMasked(kComputeStaticThreadMgmtSe0, 0xFFFFFFFFu, 0);
			stream.SetComputeShRegsCuMasked(kComputeStaticThreadMgmtSe2, 0, 0);
			if (shader->pushConstantDwords > 0)
			{
				uint32_t userData[64] = {};
				std::memcpy(userData, state.pushConstants, std::min<size_t>(sizeof(userData), sizeof(state.pushConstants)));
				stream.SetComputeShRegs(kComputeUserData0 + shader->pushConstantUserData, userData, shader->pushConstantDwords);
			}
			for (uint32_t set = 0; set < kMaxPrecompiledSets; ++set)
			{
				if (shader->setTableUserData[set] != 0)
					stream.SetComputeShReg(kComputeUserData0 + shader->setTableUserData[set], tablePointers[set]);
			}
			if (shader->numWorkgroupsUserData != 0)
			{
				uint8_t* groups = nullptr;
				const uint64_t groupsVa = state.descriptors ? state.descriptors->Allocate(16, groups) : 0;
				if (groupsVa == 0)
					return VK_ERROR_OUT_OF_DEVICE_MEMORY;
				const uint32_t counts[4] = {op.GroupCountX, op.GroupCountY, op.GroupCountZ, 0};
				std::memcpy(groups, counts, sizeof(counts));
				const uint32_t address[2] = {static_cast<uint32_t>(groupsVa), static_cast<uint32_t>(groupsVa >> 32)};
				stream.SetComputeShRegs(kComputeUserData0 + shader->numWorkgroupsUserData, address, 2);
			}
			stream.DispatchDirect(op.GroupCountX, op.GroupCountY, op.GroupCountZ, shader->dispatchInitiator);
			stream.EventWrite(kEventCsPartialFlush);
			return VK_SUCCESS;
		}

		// vkCmdClearAttachments: rects of attachments of the current subpass (subpass 0: Vkd doesn't
		// record vkCmdNextSubpass). A 3D image bound as 2D layers clears z slices.
		VkResult EncodeClearAttachments(Pm4Stream& stream, const vkd::OpClearAttachments& op, EncodeState& state)
		{
			const vkd::OpBeginRenderPass* pass = state.renderPass;
			if (!pass || !pass->RenderPassObject || !pass->FramebufferObject || pass->RenderPassObject->GetSubpasses().empty())
				return VK_SUCCESS;
			const VkSubpassDescription& subpass = pass->RenderPassObject->GetSubpasses()[0];
			const auto& views = pass->FramebufferObject->GetAttachments();

			for (const VkClearAttachment& clear : op.Attachments)
			{
				uint32_t attachment = VK_ATTACHMENT_UNUSED;
				if (clear.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
				{
					if (clear.colorAttachment < subpass.colorAttachmentCount)
						attachment = subpass.pColorAttachments[clear.colorAttachment].attachment;
				}
				else if (subpass.pDepthStencilAttachment)
					attachment = subpass.pDepthStencilAttachment->attachment;
				if (attachment == VK_ATTACHMENT_UNUSED || attachment >= views.size())
					continue;

				VKD_FROM_HANDLE(vkd::ImageView, view, views[attachment]);
				VKD_FROM_HANDLE(vkd::Image, image, view->GetImage());
				if (!IsBound(image))
					continue;
				if (const auto* tiled = static_cast<const amd::Image*>(image); tiled->IsTiled())
				{
					const VkImageSubresourceRange& range = view->GetSubresourceRange();
					for (const VkClearRect& rect : op.Rects)
						if (const VkResult r = ForEachTiledAspect(*tiled, clear.aspectMask, [&](VkImageAspectFlagBits aspect)
																  { return EncodeTiledClear(stream, *tiled, aspect, clear.clearValue.depthStencil, range.baseMipLevel,
																							range.baseArrayLayer + rect.baseArrayLayer, rect.layerCount, rect.rect, state); });
							r != VK_SUCCESS)
							return r;
					continue;
				}

				uint32_t texelSize = 0;
				uint8_t colorTexel[32];
				uint8_t depthTexel[8];
				const uint8_t* texel = nullptr;
				if (clear.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
				{
					texelSize = PackClearTexel(image->GetFormat(), clear.clearValue.color, colorTexel);
					texel = colorTexel;
				}
				else if (PackDepthStencilTexel(image->GetFormat(), clear.clearValue.depthStencil, clear.aspectMask, depthTexel, texelSize))
					texel = depthTexel;
				if (texelSize == 0 || !texel)
					continue;

				const VkImageSubresourceRange& range = view->GetSubresourceRange();
				const bool is3d = image->GetImageType() == VK_IMAGE_TYPE_3D;
				for (const VkClearRect& rect : op.Rects)
				{
					for (uint32_t layer = 0; layer < rect.layerCount; ++layer)
					{
						const uint32_t viewLayer = rect.baseArrayLayer + layer;
						vkd::SubresourceFootprint fp = vkd::GetSubresourceFootprint(*image, range.baseMipLevel, is3d ? 0 : range.baseArrayLayer + viewLayer);
						if (is3d)
						{
							if (viewLayer >= fp.extent.depth)
								continue;
							fp.offset += VkDeviceSize{viewLayer} * fp.depthPitch;
						}
						if (const VkResult r = FillRect(stream, AmdMemory(image->GetMemory()), fp, rect.rect, texel, texelSize); r != VK_SUCCESS)
							return r;
					}
				}
			}
			return VK_SUCCESS;
		}

		// Images are single-sampled here (no MSAA rendering yet), so a resolve is a copy.
		VkResult EncodeResolveImage(Pm4Stream& stream, const vkd::Image::OpResolve& op)
		{
			if (!IsBound(op.src) || !IsBound(op.dst))
				return VK_ERROR_INITIALIZATION_FAILED;
			const amd::DeviceMemory* src = AmdMemory(op.src->GetMemory());
			const amd::DeviceMemory* dst = AmdMemory(op.dst->GetMemory());
			return EncodeRows(stream, [&](auto&& copy)
							  {
				bool supported = true;
				for (const VkImageResolve& r : op.regions)
				{
					const VkImageCopy region{r.srcSubresource, r.srcOffset, r.dstSubresource, r.dstOffset, r.extent};
					supported &= vkd::ForEachImageCopyRow(*op.src, *op.dst, region, [&](VkDeviceSize s, VkDeviceSize d, VkDeviceSize n) { copy(src, s, dst, d, n); });
				}
				return supported; });
		}

		VkResult EncodeOps(Pm4Stream& stream, std::span<const vkd::CommandBuffer::Op> ops, EncodeState& state, uint32_t depth);

		bool IsTiled(const vkd::Image* image)
		{
			return image && static_cast<const amd::Image*>(image)->IsTiled();
		}

		// Resolves address images row by row in the linear layout (no tiled path: they need a
		// multisampled source, which Vkd doesn't create).
		bool TouchesTiledImage(const vkd::CommandBuffer::Op& op)
		{
			if (const auto* resolve = std::get_if<vkd::Image::OpResolve>(&op))
				return IsTiled(resolve->src) || IsTiled(resolve->dst);
			return false;
		}

		VkResult EncodeOp(Pm4Stream& stream, const vkd::CommandBuffer::Op& op, EncodeState& state, uint32_t depth)
		{
			if (TouchesTiledImage(op))
			{
				cct::Logger::Warning("AmdCommandBuffer: transfer on a tiled depth/stencil image not supported yet, skipped");
				return VK_SUCCESS;
			}
			if (const auto* clear = std::get_if<vkd::Image::OpClearColorImage>(&op))
				return EncodeClearColorImage(stream, *clear);
			if (const auto* clearDs = std::get_if<vkd::Image::OpClearDepthStencilImage>(&op))
				return EncodeClearDepthStencilImage(stream, *clearDs, state);
			if (const auto* fill = std::get_if<vkd::Buffer::OpFill>(&op))
				return EncodeFillBuffer(stream, *fill);
			if (const auto* update = std::get_if<vkd::Buffer::OpUpdate>(&op))
				return EncodeUpdateBuffer(stream, *update);
			if (const auto* copy = std::get_if<vkd::Buffer::OpCopy>(&op))
				return EncodeCopyBuffer(stream, copy->src, copy->dst, copy->regions);
			if (const auto* copy2 = std::get_if<vkd::Buffer::OpCopy2>(&op))
				return EncodeCopyBuffer(stream, copy2->src, copy2->dst, copy2->regions);
			if (const auto* toImage = std::get_if<vkd::Buffer::OpCopyBufferToImage>(&op))
			{
				if (IsTiled(toImage->dst) && IsBound(toImage->src) && IsBound(toImage->dst))
					return EncodeTiledBufferImageCopy(stream, *toImage->src, *static_cast<const amd::Image*>(toImage->dst), toImage->regions, true, state);
				return EncodeCopyBufferToImage(stream, *toImage);
			}
			if (const auto* toBuffer = std::get_if<vkd::Buffer::OpCopyImageToBuffer>(&op))
			{
				if (IsTiled(toBuffer->src) && IsBound(toBuffer->src) && IsBound(toBuffer->dst))
					return EncodeTiledBufferImageCopy(stream, *toBuffer->dst, *static_cast<const amd::Image*>(toBuffer->src), toBuffer->regions, false, state);
				return EncodeCopyImageToBuffer(stream, *toBuffer);
			}
			if (const auto* imageCopy = std::get_if<vkd::Image::OpCopy>(&op))
			{
				if ((IsTiled(imageCopy->src) || IsTiled(imageCopy->dst)) && IsBound(imageCopy->src) && IsBound(imageCopy->dst))
					return EncodeTiledImageCopy(stream, *imageCopy, state);
				return EncodeCopyImage(stream, *imageCopy);
			}
			if (const auto* resolve = std::get_if<vkd::Image::OpResolve>(&op))
				return EncodeResolveImage(stream, *resolve);
			if (const auto* blit = std::get_if<vkd::Image::OpBlit>(&op))
				return EncodeBlitImage(stream, *blit, state, *state.device);
			if (const auto* bind = std::get_if<vkd::OpBindPipeline>(&op))
			{
				if (bind->BindPoint == VK_PIPELINE_BIND_POINT_COMPUTE)
					state.computePipeline = static_cast<const amd::Pipeline*>(bind->PipelineObject);
				else if (bind->BindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS)
					state.graphicsPipeline = static_cast<const amd::Pipeline*>(bind->PipelineObject);
				return VK_SUCCESS;
			}
			if (const auto* draw = std::get_if<vkd::OpDraw>(&op))
				return EncodeDraw(stream, DrawParams{draw->VertexCount, draw->InstanceCount, static_cast<int32_t>(draw->FirstVertex), draw->FirstInstance, false, 0},
								  state);
			if (const auto* drawIndexed = std::get_if<vkd::OpDrawIndexed>(&op))
				return EncodeDraw(stream,
								  DrawParams{drawIndexed->IndexCount, drawIndexed->InstanceCount, drawIndexed->VertexOffset, drawIndexed->FirstInstance, true,
											 drawIndexed->FirstIndex},
								  state);
			if (const auto* bindVertex = std::get_if<vkd::OpBindVertexBuffer>(&op))
			{
				for (size_t i = 0; i < bindVertex->Buffers.size() && i < bindVertex->Offsets.size(); ++i)
				{
					const size_t binding = bindVertex->FirstBinding + i;
					if (binding < GraphicsPipelineState::kMaxVertexBindings)
						state.vertexBuffers[binding] = {bindVertex->Buffers[i], bindVertex->Offsets[i]};
				}
				return VK_SUCCESS;
			}
			if (const auto* bindIndex = std::get_if<vkd::OpBindIndexBuffer>(&op))
			{
				state.indexBuffer = bindIndex->BufferHandle;
				state.indexOffset = bindIndex->Offset;
				state.indexType = bindIndex->IndexType;
				return VK_SUCCESS;
			}
			if (const auto* bindSets = std::get_if<vkd::OpBindDescriptorSets>(&op))
			{
				const bool compute = bindSets->BindPoint == VK_PIPELINE_BIND_POINT_COMPUTE;
				const vkd::DescriptorSet** sets = compute ? state.computeSets : state.graphicsSets;
				std::vector<vkd::DynamicOffsetEntry>& dynamicOffsets = compute ? state.computeDynamicOffsets : state.graphicsDynamicOffsets;
				const uint32_t lastSet = bindSets->FirstSet + static_cast<uint32_t>(bindSets->DescriptorSets.size());
				std::erase_if(dynamicOffsets, [&](const vkd::DynamicOffsetEntry& entry)
							  { return entry.SetIndex >= bindSets->FirstSet && entry.SetIndex < lastSet; });
				for (const vkd::DynamicOffsetEntry& entry : bindSets->DynamicOffsets)
				{
					vkd::DynamicOffsetEntry absolute = entry;
					absolute.SetIndex += bindSets->FirstSet;
					dynamicOffsets.push_back(absolute);
				}
				for (size_t i = 0; i < bindSets->DescriptorSets.size(); ++i)
				{
					const size_t set = bindSets->FirstSet + i;
					if (set < kMaxPrecompiledSets)
						sets[set] = bindSets->DescriptorSets[i];
				}
				return VK_SUCCESS;
			}
			if (const auto* setViewport = std::get_if<vkd::OpSetViewport>(&op))
			{
				if (setViewport->FirstViewport == 0 && !setViewport->Viewports.empty())
					state.viewport = setViewport->Viewports[0];
				return VK_SUCCESS;
			}
			if (const auto* setScissor = std::get_if<vkd::OpSetScissor>(&op))
			{
				if (setScissor->FirstScissor == 0 && !setScissor->Scissors.empty())
					state.scissor = setScissor->Scissors[0];
				return VK_SUCCESS;
			}
			if (const auto* blend = std::get_if<vkd::OpSetBlendConstants>(&op))
			{
				std::copy_n(blend->Constants, 4, state.blendConstants);
				return VK_SUCCESS;
			}
			if (const auto* bias = std::get_if<vkd::OpSetDepthBias>(&op))
			{
				state.depthBias = {bias->ConstantFactor, bias->Clamp, bias->SlopeFactor};
				return VK_SUCCESS;
			}
			if (const auto* bounds = std::get_if<vkd::OpSetDepthBounds>(&op))
			{
				state.depthBounds[0] = bounds->Min;
				state.depthBounds[1] = bounds->Max;
				return VK_SUCCESS;
			}
			if (const auto* stencil = std::get_if<vkd::OpSetStencilValue>(&op))
			{
				for (int face = 0; face < 2; ++face)
				{
					if (!(stencil->FaceMask & (face == 0 ? VK_STENCIL_FACE_FRONT_BIT : VK_STENCIL_FACE_BACK_BIT)))
						continue;
					StencilFaceValues& values = state.stencil[face];
					(stencil->Which == vkd::OpSetStencilValue::Field::CompareMask ? values.compareMask
					 : stencil->Which == vkd::OpSetStencilValue::Field::WriteMask ? values.writeMask
																				  : values.reference) = stencil->Value;
				}
				return VK_SUCCESS;
			}
			if (const auto* push = std::get_if<vkd::OpPushConstants>(&op))
			{
				if (push->Offset < sizeof(state.pushConstants))
					std::memcpy(state.pushConstants + push->Offset, push->Data.data(),
								std::min<size_t>(push->Data.size(), sizeof(state.pushConstants) - push->Offset));
				return VK_SUCCESS;
			}
			if (const auto* dispatch = std::get_if<vkd::OpDispatch>(&op))
				return EncodeDispatch(stream, *dispatch, state);
			if (const auto* begin = std::get_if<vkd::OpBeginQuery>(&op))
			{
				if (!begin->QueryPoolObject || begin->QueryPoolObject->GetQueryType() != VK_QUERY_TYPE_OCCLUSION || state.computeEngine)
					return VK_SUCCESS;
				if (state.occlusionCount >= state.occlusionCapacity || !stream.HasRoom(16))
					return VK_ERROR_OUT_OF_DEVICE_MEMORY;
				if (!state.pixelPipeStatControl)
				{
					stream.PixelPipeStatControl();
					state.pixelPipeStatControl = true;
				}
				const uint32_t slot = state.occlusionCount++;
				stream.PixelPipeStatDump(state.occlusionVa + uint64_t{slot} * Pm4Stream::kOcclusionSlotBytes);
				state.activeOcclusion.push_back({begin->QueryPoolObject, begin->Query, slot});
				return VK_SUCCESS;
			}
			if (const auto* end = std::get_if<vkd::OpEndQuery>(&op))
			{
				for (auto it = state.activeOcclusion.begin(); it != state.activeOcclusion.end(); ++it)
				{
					if (it->pool != end->QueryPoolObject || it->query != end->Query)
						continue;
					if (!stream.HasRoom(8))
						return VK_ERROR_OUT_OF_DEVICE_MEMORY;
					stream.PixelPipeStatDump(state.occlusionVa + uint64_t{it->slot} * Pm4Stream::kOcclusionSlotBytes + 8);
					state.activeOcclusion.erase(it);
					break;
				}
				return VK_SUCCESS;
			}
			if (std::holds_alternative<vkd::OpWriteTimestamp>(op))
			{
				if (state.timestampCount >= state.timestampCapacity)
					return VK_ERROR_OUT_OF_DEVICE_MEMORY;
				if (!stream.HasRoom(8))
					return VK_ERROR_OUT_OF_DEVICE_MEMORY;
				stream.ReleaseMemTimestamp(state.timestampVa + uint64_t{state.timestampCount} * 8);
				++state.timestampCount;
				return VK_SUCCESS;
			}
			if (const auto* beginPass = std::get_if<vkd::OpBeginRenderPass>(&op))
			{
				state.renderPass = beginPass;
				return EncodeBeginRenderPass(stream, *beginPass, state);
			}
			if (std::holds_alternative<vkd::OpEndRenderPass>(op))
			{
				state.renderPass = nullptr;
				return VK_SUCCESS;
			}
			if (const auto* clearAttachments = std::get_if<vkd::OpClearAttachments>(&op))
				return EncodeClearAttachments(stream, *clearAttachments, state);
			if (const auto* execute = std::get_if<vkd::OpExecuteCommands>(&op))
			{
				// Secondary command buffers are encoded inline, inheriting the render pass.
				for (const vkd::CommandBuffer* secondary : execute->CommandBuffers)
				{
					if (!secondary)
						continue;
					if (const VkResult r = EncodeOps(stream, secondary->GetOps(), state, depth + 1); r != VK_SUCCESS)
						return r;
				}
				return VK_SUCCESS;
			}
			return VK_SUCCESS; // not encoded yet (needs shaders), state-only, or run on the CPU (events)
		}

		VkResult EncodeOps(Pm4Stream& stream, std::span<const vkd::CommandBuffer::Op> ops, EncodeState& state, uint32_t depth)
		{
			if (depth > 4)
				return VK_ERROR_INITIALIZATION_FAILED;
			for (const vkd::CommandBuffer::Op& op : ops)
			{
				if (const VkResult r = EncodeOp(stream, op, state, depth); r != VK_SUCCESS)
					return r;
			}
			return VK_SUCCESS;
		}
	} // namespace

	bool NeedsAttributeRing(std::span<const vkd::CommandBuffer::Op> ops, uint32_t depth)
	{
		if (depth > 4)
			return false;
		for (const vkd::CommandBuffer::Op& op : ops)
		{
			if (std::holds_alternative<vkd::OpDraw>(op) || std::holds_alternative<vkd::OpDrawIndexed>(op))
				return true;
			if (const auto* execute = std::get_if<vkd::OpExecuteCommands>(&op))
			{
				for (const vkd::CommandBuffer* secondary : execute->CommandBuffers)
					if (secondary && NeedsAttributeRing(secondary->GetOps(), depth + 1))
						return true;
			}
		}
		return false;
	}

	// Upper bound of the descriptor tables a command buffer writes: four per dispatch/draw (sets,
	// vertex buffers), one per blitted layer.
	// RPM dispatches an op runs on tiled images (one embedded table or slow-constant block each).
	uint64_t TiledRpmDispatchCount(const vkd::CommandBuffer::Op& op)
	{
		uint64_t count = 0;
		auto layers = [](const vkd::Image& image, uint32_t base, uint32_t n)
		{ return uint64_t{vkd::ResolveLayerCount(image, base, n)}; };
		if (const auto* toImage = std::get_if<vkd::Buffer::OpCopyBufferToImage>(&op); toImage && IsTiled(toImage->dst))
			for (const VkBufferImageCopy& r : toImage->regions)
				count += layers(*toImage->dst, r.imageSubresource.baseArrayLayer, r.imageSubresource.layerCount);
		if (const auto* toBuffer = std::get_if<vkd::Buffer::OpCopyImageToBuffer>(&op); toBuffer && IsTiled(toBuffer->src))
			for (const VkBufferImageCopy& r : toBuffer->regions)
				count += layers(*toBuffer->src, r.imageSubresource.baseArrayLayer, r.imageSubresource.layerCount);
		if (const auto* imageCopy = std::get_if<vkd::Image::OpCopy>(&op); imageCopy && (IsTiled(imageCopy->src) || IsTiled(imageCopy->dst)))
			for (const VkImageCopy& r : imageCopy->regions)
				count += 2 * std::max<uint64_t>(r.extent.depth, layers(*imageCopy->src, r.srcSubresource.baseArrayLayer, r.srcSubresource.layerCount));
		if (const auto* clearDs = std::get_if<vkd::Image::OpClearDepthStencilImage>(&op); clearDs && IsTiled(clearDs->image))
			for (const VkImageSubresourceRange& r : clearDs->ranges)
				count += 2 * clearDs->image->GetMipLevels() * layers(*clearDs->image, r.baseArrayLayer, r.layerCount);
		if (const auto* pass = std::get_if<vkd::OpBeginRenderPass>(&op); pass && pass->FramebufferObject)
			count += 2 * uint64_t{std::max(pass->FramebufferObject->GetLayers(), 1u)};
		if (const auto* clear = std::get_if<vkd::OpClearAttachments>(&op))
			for (const VkClearRect& r : clear->Rects)
				count += 2 * uint64_t{r.layerCount} * clear->Attachments.size();
		return count;
	}

	uint64_t DescriptorTableCount(std::span<const vkd::CommandBuffer::Op> ops, uint32_t depth)
	{
		if (depth > 4)
			return 0;
		uint64_t count = 0;
		for (const vkd::CommandBuffer::Op& op : ops)
		{
			if (std::holds_alternative<vkd::OpDispatch>(op))
				count += kMaxPrecompiledSets + 1;
			else if (std::holds_alternative<vkd::OpDraw>(op) || std::holds_alternative<vkd::OpDrawIndexed>(op))
				count += kMaxPrecompiledSets;
			else if (const uint64_t tiled = TiledRpmDispatchCount(op))
				count += tiled;
			else if (const auto* blit = std::get_if<vkd::Image::OpBlit>(&op))
			{
				for (const VkImageBlit& r : blit->regions)
					count += blit->src ? vkd::ResolveLayerCount(*blit->src, r.srcSubresource.baseArrayLayer, r.srcSubresource.layerCount) : 1;
			}
			else if (const auto* execute = std::get_if<vkd::OpExecuteCommands>(&op))
			{
				for (const vkd::CommandBuffer* secondary : execute->CommandBuffers)
					count += secondary ? DescriptorTableCount(secondary->GetOps(), depth + 1) : 0;
			}
		}
		return count;
	}

	bool NeedsDescriptorArena(std::span<const vkd::CommandBuffer::Op> ops, uint32_t depth)
	{
		if (depth > 4)
			return false;
		for (const vkd::CommandBuffer::Op& op : ops)
		{
			if (std::holds_alternative<vkd::OpDispatch>(op) || std::holds_alternative<vkd::OpDraw>(op) ||
				std::holds_alternative<vkd::OpDrawIndexed>(op) || std::holds_alternative<vkd::Image::OpBlit>(op))
				return true;
			// Tiled images are copied and cleared by RPM shaders.
			if (TiledRpmDispatchCount(op) != 0)
				return true;
			if (const auto* execute = std::get_if<vkd::OpExecuteCommands>(&op))
			{
				for (const vkd::CommandBuffer* secondary : execute->CommandBuffers)
					if (secondary && NeedsDescriptorArena(secondary->GetOps(), depth + 1))
						return true;
			}
		}
		return false;
	}

	namespace
	{
		bool IsOcclusionBegin(const vkd::CommandBuffer::Op& op)
		{
			const auto* begin = std::get_if<vkd::OpBeginQuery>(&op);
			return begin && begin->QueryPoolObject && begin->QueryPoolObject->GetQueryType() == VK_QUERY_TYPE_OCCLUSION;
		}

		// How many ops of a command buffer (and its secondaries) match.
		template<typename Match>
		uint32_t CountOps(std::span<const vkd::CommandBuffer::Op> ops, Match&& match, uint32_t depth = 0)
		{
			if (depth > 4)
				return 0;
			uint32_t count = 0;
			for (const vkd::CommandBuffer::Op& op : ops)
			{
				count += match(op) ? 1 : 0;
				if (const auto* execute = std::get_if<vkd::OpExecuteCommands>(&op))
				{
					for (const vkd::CommandBuffer* secondary : execute->CommandBuffers)
						count += secondary ? CountOps(secondary->GetOps(), match, depth + 1) : 0;
				}
			}
			return count;
		}

		// Samples that passed between an occlusion query's begin and end dumps: the sum over the RBs
		// whose two counters carry the valid bit (63).
		uint64_t OcclusionResult(const uint64_t* slot)
		{
			constexpr uint64_t kValid = 1ull << 63;
			uint64_t samples = 0;
			for (uint32_t rb = 0; rb < Pm4Stream::kOcclusionRbCount; ++rb)
			{
				const uint64_t begin = slot[rb * 2];
				const uint64_t end = slot[rb * 2 + 1];
				if ((begin & kValid) && (end & kValid))
					samples += (end & ~kValid) - (begin & ~kValid);
			}
			return samples;
		}

		// vkCmdCopyQueryPoolResults on the host: the destination memory is host-visible.
		void CopyQueryResults(const vkd::OpCopyQueryPoolResults& op)
		{
			if (!op.QueryPoolObject || !IsBound(op.DstBuffer) || op.DstOffset >= op.DstBuffer->GetSize())
				return;
			auto* memory = static_cast<amd::DeviceMemory*>(op.DstBuffer->GetMemory());
			auto* base = static_cast<uint8_t*>(memory->MapHost());
			if (!base)
				return;
			const VkDeviceSize available = op.DstBuffer->GetSize() - op.DstOffset;
			op.QueryPoolObject->GetResults(op.FirstQuery, op.QueryCount, static_cast<std::size_t>(available),
										   base + op.DstBuffer->GetMemoryOffset() + op.DstOffset, op.Stride,
										   op.Flags & ~VkQueryResultFlags{VK_QUERY_RESULT_WAIT_BIT});
		}

		struct HostOpsCursor
		{
			const CommandBuffer& cmd;
			uint32_t timestampIndex = 0;
			uint32_t occlusionIndex = 0;
			struct ActiveQuery
			{
				const vkd::QueryPool* pool;
				uint32_t query;
				uint32_t slot;
			};
			std::vector<ActiveQuery> activeOcclusion;
		};

		void ApplyHostOps(std::span<const vkd::CommandBuffer::Op> ops, HostOpsCursor& cursor, uint32_t depth)
		{
			if (depth > 4)
				return;
			for (const vkd::CommandBuffer::Op& op : ops)
			{
				if (const auto* setEvent = std::get_if<vkd::OpSetEvent>(&op))
				{
					if (setEvent->EventObject)
						setEvent->Signaled ? setEvent->EventObject->Set() : setEvent->EventObject->Reset();
				}
				else if (const auto* reset = std::get_if<vkd::OpResetQueryPool>(&op))
				{
					if (reset->QueryPoolObject)
						reset->QueryPoolObject->Reset(reset->FirstQuery, reset->QueryCount);
				}
				else if (const auto* begin = std::get_if<vkd::OpBeginQuery>(&op))
				{
					if (begin->QueryPoolObject)
						begin->QueryPoolObject->Begin(begin->Query, begin->Flags);
					if (IsOcclusionBegin(op) && !cursor.cmd.IsComputeEngine())
						cursor.activeOcclusion.push_back({begin->QueryPoolObject, begin->Query, cursor.occlusionIndex++});
				}
				else if (const auto* end = std::get_if<vkd::OpEndQuery>(&op))
				{
					uint64_t value = 0;
					for (auto it = cursor.activeOcclusion.begin(); it != cursor.activeOcclusion.end(); ++it)
					{
						if (it->pool != end->QueryPoolObject || it->query != end->Query)
							continue;
						const uint64_t* data = cursor.cmd.GetOcclusionData();
						if (data && it->slot < cursor.cmd.GetOcclusionCount())
							value = OcclusionResult(data + uint64_t{it->slot} * (Pm4Stream::kOcclusionSlotBytes / 8));
						cursor.activeOcclusion.erase(it);
						break;
					}
					if (end->QueryPoolObject)
						end->QueryPoolObject->End(end->Query, value);
				}
				else if (const auto* timestamp = std::get_if<vkd::OpWriteTimestamp>(&op))
				{
					const uint64_t* timestamps = cursor.cmd.GetTimestamps();
					const uint64_t value = timestamps && cursor.timestampIndex < cursor.cmd.GetTimestampCount() ? timestamps[cursor.timestampIndex] : 0;
					++cursor.timestampIndex;
					if (timestamp->QueryPoolObject)
						timestamp->QueryPoolObject->WriteTimestamp(timestamp->Query, value);
				}
				else if (const auto* copy = std::get_if<vkd::OpCopyQueryPoolResults>(&op))
					CopyQueryResults(*copy);
				else if (const auto* execute = std::get_if<vkd::OpExecuteCommands>(&op))
				{
					for (const vkd::CommandBuffer* secondary : execute->CommandBuffers)
					{
						if (secondary)
							ApplyHostOps(secondary->GetOps(), cursor, depth + 1);
					}
				}
			}
		}
	} // namespace

	void ApplyHostOps(const CommandBuffer& cmd)
	{
		HostOpsCursor cursor{cmd};
		ApplyHostOps(cmd.GetOps(), cursor, 0);
	}

	bool CommandBuffer::IsComputeEngine() const
	{
		return GetOwner()->GetQueueFamilyIndex() != 0;
	}

	VkResult CommandBuffer::Create(vkd::CommandPool& owner, VkCommandBufferLevel level)
	{
		VkResult result = vkd::CommandBuffer::Create(owner, level);
		if (result != VK_SUCCESS)
			return result;

		auto* amdDevice = static_cast<AmdDevice*>(owner.GetOwner());
		if (!amdDevice->AllocateGpuMemory(0x10000, kSurfaceFlagsInitB, m_ib))
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;
		if (m_ib.GetGpuVa() == 0)
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;
		return VK_SUCCESS;
	}

	VkResult CommandBuffer::Encode()
	{
		void* ptr = m_ib.MapToHost();
		if (!ptr)
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;
		m_mappedIb = ptr;

		// Guard against a CPU mapping smaller than the allocation. That used to happen because the
		// allocation private data declared 4KB while 64KB was mapped (fixed in PrivateData.hpp
		// SetAllocBlobSize); keep the check rather than trusting m_ib.GetSize() blindly.
		MEMORY_BASIC_INFORMATION mbi{};
		size_t mappedBytes = 0;
		if (VirtualQuery(ptr, &mbi, sizeof(mbi)))
			mappedBytes = static_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize - static_cast<uint8_t*>(ptr);
		if (mappedBytes < 32)
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;

		Pm4Stream stream;
		stream.dw = static_cast<uint32_t*>(ptr);
		stream.capacity = static_cast<uint32_t>(std::min<size_t>(mappedBytes, m_ib.GetSize()) / sizeof(uint32_t));
		stream.count = 0;
		stream.gpuVa = m_ib.GetGpuVa();

		// Descriptor tables for dispatches and draws, written while encoding. Allocated on first use:
		// most command buffers never dispatch nor draw.
		DescriptorArena descriptors;
		if (NeedsDescriptorArena(GetOps(), 0))
		{
			// 64KB, more when the command buffer's tables need it (a 256-byte slot per table).
			const uint64_t needed = std::max<uint64_t>(DescriptorTableCount(GetOps()) * 256 + 4096, 0x10000);
			if (m_descriptorTables.GetSize() < needed)
			{
				auto* amdDevice = static_cast<AmdDevice*>(GetOwner()->GetOwner());
				wddm::WddmAllocation tables;
				if (!amdDevice->AllocateShaderWindowMemory(needed, kSurfaceFlagsHostVisible, tables))
					return VK_ERROR_OUT_OF_DEVICE_MEMORY;
				m_descriptorTables = std::move(tables);
			}
			descriptors.allocation = &m_descriptorTables;
			descriptors.cpu = static_cast<uint8_t*>(m_descriptorTables.MapToHost());
		}

		EncodeState state;
		state.descriptors = &descriptors;
		state.device = static_cast<AmdDevice*>(GetOwner()->GetOwner());
		// Query destinations, allocated on first use: the timestamps, then the occlusion slots.
		state.computeEngine = IsComputeEngine();
		const uint32_t timestamps = CountOps(GetOps(), [](const vkd::CommandBuffer::Op& op)
											 { return std::holds_alternative<vkd::OpWriteTimestamp>(op); });
		const uint32_t occlusions = state.computeEngine ? 0 : CountOps(GetOps(), IsOcclusionBegin);
		if (timestamps > 0 || occlusions > 0)
		{
			const uint64_t occlusionOffset = (uint64_t{timestamps} * 8 + 31) & ~uint64_t{31};
			const uint64_t used = occlusionOffset + uint64_t{occlusions} * Pm4Stream::kOcclusionSlotBytes;
			const uint64_t bytes = std::max<uint64_t>(used, 0x10000);
			if (m_queryData.GetSize() < bytes)
			{
				auto* amdDevice = static_cast<AmdDevice*>(GetOwner()->GetOwner());
				wddm::WddmAllocation allocation;
				if (!amdDevice->AllocateGpuMemory(bytes, kSurfaceFlagsHostVisible, allocation) || allocation.GetGpuVa() == 0)
					return VK_ERROR_OUT_OF_DEVICE_MEMORY;
				m_queryData = std::move(allocation);
			}
			auto* data = static_cast<uint8_t*>(m_queryData.MapToHost());
			if (!data)
				return VK_ERROR_OUT_OF_DEVICE_MEMORY;
			std::memset(data, 0, static_cast<size_t>(used));
			m_timestampValues = reinterpret_cast<const uint64_t*>(data);
			m_occlusionValues = reinterpret_cast<const uint64_t*>(data + occlusionOffset);
			state.timestampVa = m_queryData.GetGpuVa();
			state.timestampCapacity = timestamps;
			state.occlusionVa = m_queryData.GetGpuVa() + occlusionOffset;
			state.occlusionCapacity = occlusions;
		}
		if (NeedsAttributeRing(GetOps(), 0))
		{
			auto* amdDevice = static_cast<AmdDevice*>(GetOwner()->GetOwner());
			state.attributeRingVa = amdDevice->GetAttributeRingVa();
			state.shaderRingTable = amdDevice->GetShaderRingTable();
		}
		if (const VkResult r = EncodeOps(stream, GetOps(), state, 0); r != VK_SUCCESS)
			return r;
		m_timestampCount = state.timestampCount;
		m_occlusionCount = state.occlusionCount;

		// PKT3 NOP padded to 8 DWORDs, the shape of every IB that completed on this hw queue (no PKT2).
		if (!stream.HasRoom(2 + 9))
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;
		stream.Nop();
		stream.PadTo8Dw();
		m_commandLength = stream.Bytes();
		return VK_SUCCESS;
	}

	VkResult CommandBuffer::Bake()
	{
		for (;;)
		{
			const VkResult r = Encode();
			if (r == VK_SUCCESS)
			{
				// Leave the IB CPU-mapped for submit (PAL does this for cmd streams).
				m_baked = true;
				return VK_SUCCESS;
			}
			// Out of IB room: grow the IB and re-encode from scratch.
			const uint64_t grown = m_ib.GetSize() * 2;
			if (r != VK_ERROR_OUT_OF_DEVICE_MEMORY || grown > kMaxIbBytes)
			{
				cct::Logger::Error("AmdCommandBuffer: failed to encode command buffer (VkResult {}, IB {} bytes)",
								   static_cast<int>(r), m_ib.GetSize());
				return r;
			}
			auto* amdDevice = static_cast<AmdDevice*>(GetOwner()->GetOwner());
			wddm::WddmAllocation bigger;
			if (!amdDevice->AllocateGpuMemory(grown, kSurfaceFlagsInitB, bigger) || bigger.GetGpuVa() == 0)
				return VK_ERROR_OUT_OF_DEVICE_MEMORY;
			m_ib = std::move(bigger);
		}
	}
} // namespace vkd::amd
