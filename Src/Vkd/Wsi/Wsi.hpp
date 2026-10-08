/**
 * @file Wsi.hpp
 * @brief VK_KHR_surface, VK_KHR_win32_surface and VK_KHR_swapchain
 * @date 2026-10-07
 *
 * Presentation is a CPU copy: vkQueuePresentKHR waits for the rendering, then blits the swapchain
 * image to the window with GDI. Vkd images are linear and their memory host-visible, so this works
 * the same for every driver (the software rasterizer, the AMD driver).
 */

#pragma once

#include <span>

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>
#ifdef CCT_PLATFORM_WINDOWS
#include <vulkan/vulkan_win32.h>
#endif

namespace vkd
{
	class Device;

	// Surface entry points. A VkSurfaceKHR is a VkIcdSurfaceBase* (vulkan/vk_icd.h): the loader
	// creates those itself, and vkCreateWin32SurfaceKHR creates the same struct when the ICD is
	// called directly.
	class Wsi
	{
	public:
#ifdef CCT_PLATFORM_WINDOWS
		static VkResult VKAPI_CALL CreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
														 const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
		static VkBool32 VKAPI_CALL GetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex);
#endif
		static void VKAPI_CALL DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator);
		static VkResult VKAPI_CALL GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
																	  VkSurfaceKHR surface, VkBool32* pSupported);
		static VkResult VKAPI_CALL GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
																		   VkSurfaceCapabilitiesKHR* pSurfaceCapabilities);
		static VkResult VKAPI_CALL GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
																	  uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats);
		static VkResult VKAPI_CALL GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
																		   uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes);

		static VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
													  const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);
		static void VKAPI_CALL DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator);
		static VkResult VKAPI_CALL GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount,
														 VkImage* pSwapchainImages);
		static VkResult VKAPI_CALL AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
													   VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex);
		static VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
	};

	// The images of a swapchain are ordinary Vkd images created through the device's own entry
	// points, each with its own host-visible memory, kept mapped for presentation.
	class Swapchain : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_SWAPCHAIN_KHR;
		VKD_CLASSNAME_METHOD(Swapchain)
		static Swapchain* FromHandle(VkSwapchainKHR handle)
		{
			return reinterpret_cast<Swapchain*>(handle);
		}

		explicit Swapchain(const VkAllocationCallbacks& allocationCallbacks);
		~Swapchain() override;

		VkResult Create(VkDevice device, const VkSwapchainCreateInfoKHR& info);
		VkResult GetImages(uint32_t* pCount, VkImage* pImages) const;
		uint32_t AcquireNextImage();
		VkResult Present(uint32_t imageIndex);

	private:
		struct SwapchainImage
		{
			VkImage image = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			const uint8_t* pixels = nullptr; // first texel, through the persistent mapping
			VkDeviceSize rowPitch = 0;
		};

		VkDevice m_device;
		VkSurfaceKHR m_surface;
		VkFormat m_format;
		VkExtent2D m_extent;
		vkd::vector<SwapchainImage> m_images;
		vkd::vector<uint32_t> m_staging; // BGRA copy of an RGBA image for GDI
		uint32_t m_nextImage;
	};
} // namespace vkd
