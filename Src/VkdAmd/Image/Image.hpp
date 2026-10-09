#pragma once

#include "Vkd/Image/Image.hpp"

#include "AmdAddr/Gfx11Tiling.hpp"

namespace vkd::amd
{
	// Images keep the shared linear layout, except 2D OPTIMAL depth/stencil images: the DB only reads
	// and writes swizzled surfaces, so those are tiled (SW_64KB_Z_X, AMDVLK's mode) -- a depth plane,
	// then a stencil plane at the next 64KB boundary, without HTILE. Their copies and clears run RPM
	// shaders through T#s, which also splits combined formats into their aspects.
	class Image : public vkd::Image
	{
	public:
		using vkd::Image::Image;

		VkResult Create(Device& owner, const VkImageCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks) override;
		void GetMemoryRequirements(VkMemoryRequirements& memoryRequirements) const override;

		[[nodiscard]] bool IsTiled() const
		{
			return m_tiled;
		}
		// Tiled images only; a plane the format doesn't have has bytesPerElement == 0.
		[[nodiscard]] const amdaddr::gfx11::SurfaceLayout& DepthPlane() const
		{
			return m_depthPlane;
		}
		[[nodiscard]] const amdaddr::gfx11::SurfaceLayout& StencilPlane() const
		{
			return m_stencilPlane;
		}
		[[nodiscard]] VkDeviceSize StencilPlaneOffset() const
		{
			return m_stencilPlaneOffset;
		}

		// Bytes per element of a format's depth and stencil planes (0 = none); false for the formats
		// the DB can't render (24-bit depth).
		static bool GetDepthStencilPlanes(VkFormat format, uint32_t& depthBytes, uint32_t& stencilBytes);

	private:
		bool m_tiled = false;
		amdaddr::gfx11::SurfaceLayout m_depthPlane;
		amdaddr::gfx11::SurfaceLayout m_stencilPlane;
		VkDeviceSize m_stencilPlaneOffset = 0;
	};
} // namespace vkd::amd
