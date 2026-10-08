#include "VkdAmd/Image/Image.hpp"

namespace vkd::amd
{
	bool Image::GetDepthStencilPlanes(VkFormat format, uint32_t& depthBytes, uint32_t& stencilBytes)
	{
		depthBytes = 0;
		stencilBytes = 0;
		switch (format)
		{
			case VK_FORMAT_D16_UNORM:
				depthBytes = 2;
				return true;
			case VK_FORMAT_D32_SFLOAT:
				depthBytes = 4;
				return true;
			case VK_FORMAT_S8_UINT:
				stencilBytes = 1;
				return true;
			case VK_FORMAT_D16_UNORM_S8_UINT:
				depthBytes = 2;
				stencilBytes = 1;
				return true;
			case VK_FORMAT_D32_SFLOAT_S8_UINT:
				depthBytes = 4;
				stencilBytes = 1;
				return true;
			default:
				return false;
		}
	}

	VkResult Image::Create(Device& owner, const VkImageCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		const VkResult result = vkd::Image::Create(owner, info, allocationCallbacks);
		if (result != VK_SUCCESS)
			return result;

		uint32_t depthBytes = 0, stencilBytes = 0;
		m_tiled = info.tiling == VK_IMAGE_TILING_OPTIMAL && info.imageType == VK_IMAGE_TYPE_2D && info.samples == VK_SAMPLE_COUNT_1_BIT &&
				  GetDepthStencilPlanes(info.format, depthBytes, stencilBytes);
		if (!m_tiled)
			return VK_SUCCESS;

		VkDeviceSize planeEnd = 0;
		if (depthBytes)
		{
			m_depthPlane = gfx11::Compute64KbZLayout(depthBytes, info.extent.width, info.extent.height, info.mipLevels, info.arrayLayers);
			planeEnd = m_depthPlane.size;
		}
		if (stencilBytes)
		{
			m_stencilPlane = gfx11::Compute64KbZLayout(stencilBytes, info.extent.width, info.extent.height, info.mipLevels, info.arrayLayers);
			m_stencilPlaneOffset = (planeEnd + gfx11::kBlock64Kb - 1) & ~(gfx11::kBlock64Kb - 1);
		}
		return VK_SUCCESS;
	}

	void Image::GetMemoryRequirements(VkMemoryRequirements& memoryRequirements) const
	{
		vkd::Image::GetMemoryRequirements(memoryRequirements);
		if (!m_tiled)
			return;
		memoryRequirements.size = m_stencilPlane.bytesPerElement ? m_stencilPlaneOffset + m_stencilPlane.size : m_depthPlane.size;
		memoryRequirements.alignment = gfx11::kBlock64Kb; // swizzles XOR address bits up to the block size
	}
} // namespace vkd::amd
