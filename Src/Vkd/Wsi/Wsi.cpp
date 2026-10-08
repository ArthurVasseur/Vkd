/**
 * @file Wsi.cpp
 * @brief Win32 surfaces and CPU-presented swapchains
 * @date 2026-10-07
 */

#include "Vkd/Wsi/Wsi.hpp"

#include <algorithm>
#include <cstring>

#include "Vkd/Device/Device.hpp"
#include "Vkd/Memory/Memory.hpp"
#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"
#include "Vkd/Queue/Queue.hpp"
#include "Vkd/Synchronization/Fence/Fence.hpp"
#include "Vkd/Synchronization/Semaphore/Semaphore.hpp"

#include <vulkan/utility/vk_format_utils.h>
#include <vulkan/vk_icd.h>

namespace vkd
{
	namespace
	{
		// Formats GDI can show: 32-bit DIBs are BGRA in memory; RGBA images are swizzled while
		// presenting. RGBA first: it's the format the AMD driver's replayed pipelines render to.
		constexpr VkSurfaceFormatKHR kSurfaceFormats[] = {
			{VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
			{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
			{VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
			{VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
		};

		// Presentation copies synchronously, so every mode behaves like FIFO.
		constexpr VkPresentModeKHR kPresentModes[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR};

		constexpr uint32_t kMinImageCount = 2;
		constexpr uint32_t kMaxImageCount = 3;

		template<typename T>
		VkResult WriteArray(std::span<const T> values, uint32_t* pCount, T* pValues)
		{
			if (!pValues)
			{
				*pCount = static_cast<uint32_t>(values.size());
				return VK_SUCCESS;
			}
			const uint32_t written = std::min(*pCount, static_cast<uint32_t>(values.size()));
			std::copy_n(values.begin(), written, pValues);
			*pCount = written;
			return written < values.size() ? VK_INCOMPLETE : VK_SUCCESS;
		}

		bool IsBgra(VkFormat format)
		{
			return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
		}

#ifdef CCT_PLATFORM_WINDOWS
		// vk_icd.h's VkIcdSurfaceWin32, which it only declares under VK_USE_PLATFORM_WIN32_KHR.
		struct IcdSurfaceWin32
		{
			VkIcdSurfaceBase base;
			HINSTANCE hinstance;
			HWND hwnd;
		};

		HWND GetWindow(VkSurfaceKHR surface)
		{
			auto* base = reinterpret_cast<VkIcdSurfaceBase*>(surface);
			if (!base || base->platform != VK_ICD_WSI_PLATFORM_WIN32)
				return nullptr;
			return reinterpret_cast<IcdSurfaceWin32*>(surface)->hwnd;
		}

		VkExtent2D GetClientExtent(HWND window)
		{
			RECT rect{};
			if (!window || !GetClientRect(window, &rect))
				return {0, 0};
			return {static_cast<uint32_t>(rect.right - rect.left), static_cast<uint32_t>(rect.bottom - rect.top)};
		}
#endif
	} // namespace

#ifdef CCT_PLATFORM_WINDOWS
	VkResult Wsi::CreateWin32SurfaceKHR(VkInstance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator,
										VkSurfaceKHR* pSurface)
	{
		VKD_AUTO_PROFILER_SCOPE();
		VKD_CHECK(pCreateInfo && pSurface);

		const VkAllocationCallbacks& callbacks = pAllocator ? *pAllocator : mem::GetDefaultAllocationCallbacks();
		void* storage = callbacks.pfnAllocation(callbacks.pUserData, sizeof(IcdSurfaceWin32), alignof(IcdSurfaceWin32),
												VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!storage)
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		auto* surface = static_cast<IcdSurfaceWin32*>(storage);
		surface->base.platform = VK_ICD_WSI_PLATFORM_WIN32;
		surface->hinstance = pCreateInfo->hinstance;
		surface->hwnd = pCreateInfo->hwnd;
		*pSurface = reinterpret_cast<VkSurfaceKHR>(surface);
		return VK_SUCCESS;
	}

	VkBool32 Wsi::GetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice, uint32_t)
	{
		return VK_TRUE;
	}
#endif

	void Wsi::DestroySurfaceKHR(VkInstance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();
		if (surface == VK_NULL_HANDLE)
			return;
		const VkAllocationCallbacks& callbacks = pAllocator ? *pAllocator : mem::GetDefaultAllocationCallbacks();
		callbacks.pfnFree(callbacks.pUserData, reinterpret_cast<void*>(surface));
	}

	VkResult Wsi::GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice, uint32_t, VkSurfaceKHR surface, VkBool32* pSupported)
	{
		VKD_CHECK(pSupported);
#ifdef CCT_PLATFORM_WINDOWS
		*pSupported = GetWindow(surface) ? VK_TRUE : VK_FALSE;
#else
		(void)surface;
		*pSupported = VK_FALSE;
#endif
		return VK_SUCCESS;
	}

	VkResult Wsi::GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
														  VkSurfaceCapabilitiesKHR* pSurfaceCapabilities)
	{
		VKD_CHECK(pSurfaceCapabilities);
		VkPhysicalDeviceProperties properties{};
		PhysicalDevice::GetPhysicalDeviceProperties(physicalDevice, &properties);
		const uint32_t maxDimension = properties.limits.maxImageDimension2D;

		VkSurfaceCapabilitiesKHR& caps = *pSurfaceCapabilities;
		caps = {};
		caps.minImageCount = kMinImageCount;
		caps.maxImageCount = kMaxImageCount;
#ifdef CCT_PLATFORM_WINDOWS
		const VkExtent2D client = GetClientExtent(GetWindow(surface));
		caps.currentExtent = {std::min(client.width, maxDimension), std::min(client.height, maxDimension)};
#else
		(void)surface;
#endif
		caps.minImageExtent = {1, 1};
		caps.maxImageExtent = {maxDimension, maxDimension};
		caps.maxImageArrayLayers = 1;
		caps.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		caps.currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		caps.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		caps.supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
								   VK_IMAGE_USAGE_SAMPLED_BIT;
		return VK_SUCCESS;
	}

	VkResult Wsi::GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t* pSurfaceFormatCount,
													 VkSurfaceFormatKHR* pSurfaceFormats)
	{
		VKD_CHECK(pSurfaceFormatCount);
		return WriteArray(std::span<const VkSurfaceFormatKHR>(kSurfaceFormats), pSurfaceFormatCount, pSurfaceFormats);
	}

	VkResult Wsi::GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t* pPresentModeCount,
														  VkPresentModeKHR* pPresentModes)
	{
		VKD_CHECK(pPresentModeCount);
		return WriteArray(std::span<const VkPresentModeKHR>(kPresentModes), pPresentModeCount, pPresentModes);
	}

	VkResult Wsi::CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator,
									 VkSwapchainKHR* pSwapchain)
	{
		VKD_AUTO_PROFILER_SCOPE();
		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_CHECK(pCreateInfo && pSwapchain);
		const VkAllocationCallbacks& callbacks = pAllocator ? *pAllocator : deviceObj->GetAllocationCallbacks();

		auto* swapchain = mem::NewObject<Swapchain>(callbacks);
		if (!swapchain)
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		const VkResult result = swapchain->Create(device, *pCreateInfo);
		if (result != VK_SUCCESS)
		{
			mem::Delete(callbacks, swapchain);
			return result;
		}
		// The old swapchain stays valid until destroyed; its images aren't presented anymore.
		*pSwapchain = reinterpret_cast<VkSwapchainKHR>(swapchain);
		return VK_SUCCESS;
	}

	void Wsi::DestroySwapchainKHR(VkDevice, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();
		if (swapchain == VK_NULL_HANDLE)
			return;
		Swapchain* swapchainObj = Swapchain::FromHandle(swapchain);
		mem::Delete(pAllocator ? *pAllocator : swapchainObj->GetAllocationCallbacks(), swapchainObj);
	}

	VkResult Wsi::GetSwapchainImagesKHR(VkDevice, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages)
	{
		VKD_FROM_HANDLE(Swapchain, swapchainObj, swapchain);
		VKD_CHECK(pSwapchainImageCount);
		return swapchainObj->GetImages(pSwapchainImageCount, pSwapchainImages);
	}

	// Images are presented synchronously, so the next one is always available: the semaphore and
	// fence are signaled right away.
	VkResult Wsi::AcquireNextImageKHR(VkDevice, VkSwapchainKHR swapchain, uint64_t, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
	{
		VKD_AUTO_PROFILER_SCOPE();
		VKD_FROM_HANDLE(Swapchain, swapchainObj, swapchain);
		VKD_CHECK(pImageIndex);
		*pImageIndex = swapchainObj->AcquireNextImage();
		if (semaphore != VK_NULL_HANDLE)
		{
			VKD_FROM_HANDLE(Semaphore, semaphoreObj, semaphore);
			semaphoreObj->Signal();
		}
		if (fence != VK_NULL_HANDLE)
		{
			VKD_FROM_HANDLE(Fence, fenceObj, fence);
			fenceObj->Signal();
		}
		return VK_SUCCESS;
	}

	VkResult Wsi::QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
	{
		VKD_AUTO_PROFILER_SCOPE();
		VKD_FROM_HANDLE(Queue, queueObj, queue);
		VKD_CHECK(pPresentInfo);

		for (uint32_t i = 0; i < pPresentInfo->waitSemaphoreCount; ++i)
		{
			VKD_FROM_HANDLE(Semaphore, semaphoreObj, pPresentInfo->pWaitSemaphores[i]);
			semaphoreObj->Wait();
		}
		const VkResult idle = Queue::QueueWaitIdle(queue);
		if (idle != VK_SUCCESS)
			return idle;

		VkResult overall = VK_SUCCESS;
		for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i)
		{
			VKD_FROM_HANDLE(Swapchain, swapchainObj, pPresentInfo->pSwapchains[i]);
			const VkResult result = swapchainObj->Present(pPresentInfo->pImageIndices[i]);
			if (pPresentInfo->pResults)
				pPresentInfo->pResults[i] = result;
			if (result != VK_SUCCESS && overall == VK_SUCCESS)
				overall = result;
		}
		return overall;
	}

	Swapchain::Swapchain(const VkAllocationCallbacks& allocationCallbacks) :
		ObjectBase(ObjectType, allocationCallbacks),
		m_device(VK_NULL_HANDLE),
		m_surface(VK_NULL_HANDLE),
		m_format(VK_FORMAT_UNDEFINED),
		m_extent{0, 0},
		m_images(allocator<SwapchainImage>(allocationCallbacks)),
		m_staging(allocator<uint32_t>(allocationCallbacks)),
		m_nextImage(0)
	{
	}

	Swapchain::~Swapchain()
	{
		for (const SwapchainImage& image : m_images)
		{
			if (image.pixels)
				Device::UnmapMemory(m_device, image.memory);
			Device::DestroyImage(m_device, image.image, nullptr);
			Device::FreeMemory(m_device, image.memory, nullptr);
		}
	}

	VkResult Swapchain::Create(VkDevice device, const VkSwapchainCreateInfoKHR& info)
	{
		m_device = device;
		m_surface = info.surface;
		m_format = info.imageFormat;
		m_extent = info.imageExtent;
		if (m_extent.width == 0 || m_extent.height == 0)
			return Error(VK_ERROR_INITIALIZATION_FAILED, "Swapchain: zero-sized extent");
		if (vkuFormatElementSize(m_format) != 4)
			return Error(VK_ERROR_FORMAT_NOT_SUPPORTED, "Swapchain: format {} is not a 32-bit color format", static_cast<int>(m_format));

		const uint32_t count = std::clamp(info.minImageCount, kMinImageCount, kMaxImageCount);
		m_images.resize(count);
		for (SwapchainImage& image : m_images)
		{
			VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.format = m_format;
			imageInfo.extent = {m_extent.width, m_extent.height, 1};
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = std::max(info.imageArrayLayers, 1u);
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageInfo.usage = info.imageUsage;
			imageInfo.sharingMode = info.imageSharingMode;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			if (const VkResult r = Device::CreateImage(m_device, &imageInfo, nullptr, &image.image); r != VK_SUCCESS)
				return r;

			VkMemoryRequirements requirements{};
			Device::GetImageMemoryRequirements(m_device, image.image, &requirements);
			// Memory type 0 is device-local, host-visible and coherent on every Vkd driver.
			const VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size, 0};
			if (const VkResult r = Device::AllocateMemory(m_device, &allocateInfo, nullptr, &image.memory); r != VK_SUCCESS)
				return r;
			if (const VkResult r = Device::BindImageMemory(m_device, image.image, image.memory, 0); r != VK_SUCCESS)
				return r;

			void* mapped = nullptr;
			if (const VkResult r = Device::MapMemory(m_device, image.memory, 0, VK_WHOLE_SIZE, 0, &mapped); r != VK_SUCCESS)
				return r;
			VkSubresourceLayout layout{};
			const VkImageSubresource subresource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
			Device::GetImageSubresourceLayout(m_device, image.image, &subresource, &layout);
			image.pixels = static_cast<const uint8_t*>(mapped) + layout.offset;
			image.rowPitch = layout.rowPitch;
		}
		if (!IsBgra(m_format))
			m_staging.resize(static_cast<std::size_t>(m_extent.width) * m_extent.height);

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	VkResult Swapchain::GetImages(uint32_t* pCount, VkImage* pImages) const
	{
		VkImage images[kMaxImageCount] = {};
		for (std::size_t i = 0; i < m_images.size(); ++i)
			images[i] = m_images[i].image;
		return WriteArray(std::span<const VkImage>(images, m_images.size()), pCount, pImages);
	}

	uint32_t Swapchain::AcquireNextImage()
	{
		const uint32_t index = m_nextImage;
		m_nextImage = (m_nextImage + 1) % static_cast<uint32_t>(m_images.size());
		return index;
	}

	VkResult Swapchain::Present(uint32_t imageIndex)
	{
		VKD_AUTO_PROFILER_SCOPE();
		if (imageIndex >= m_images.size())
			return VK_ERROR_OUT_OF_DATE_KHR;
#ifdef CCT_PLATFORM_WINDOWS
		const HWND window = GetWindow(m_surface);
		if (!window)
			return VK_ERROR_SURFACE_LOST_KHR;
		const SwapchainImage& image = m_images[imageIndex];

		// A 32-bit top-down DIB over the image rows: as is for BGRA (DIB width = row pitch in
		// texels), through a swizzled copy for RGBA.
		const void* bits = image.pixels;
		LONG dibWidth = static_cast<LONG>(image.rowPitch / 4);
		if (!IsBgra(m_format))
		{
			for (uint32_t y = 0; y < m_extent.height; ++y)
			{
				const auto* row = reinterpret_cast<const uint32_t*>(image.pixels + y * image.rowPitch);
				uint32_t* out = m_staging.data() + static_cast<std::size_t>(y) * m_extent.width;
				for (uint32_t x = 0; x < m_extent.width; ++x)
				{
					const uint32_t rgba = row[x];
					out[x] = (rgba & 0xFF00FF00u) | ((rgba & 0xFFu) << 16) | ((rgba >> 16) & 0xFFu);
				}
			}
			bits = m_staging.data();
			dibWidth = static_cast<LONG>(m_extent.width);
		}

		BITMAPINFO bitmapInfo{};
		bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bitmapInfo.bmiHeader.biWidth = dibWidth;
		bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(m_extent.height); // top-down
		bitmapInfo.bmiHeader.biPlanes = 1;
		bitmapInfo.bmiHeader.biBitCount = 32;
		bitmapInfo.bmiHeader.biCompression = BI_RGB;

		const VkExtent2D client = GetClientExtent(window);
		HDC dc = GetDC(window);
		if (!dc)
			return VK_ERROR_SURFACE_LOST_KHR;
		const int lines = StretchDIBits(dc, 0, 0, static_cast<int>(client.width), static_cast<int>(client.height), 0, 0,
										static_cast<int>(m_extent.width), static_cast<int>(m_extent.height), bits, &bitmapInfo,
										DIB_RGB_COLORS, SRCCOPY);
		ReleaseDC(window, dc);
		if (lines == 0)
			return VK_ERROR_SURFACE_LOST_KHR;
		return client.width == m_extent.width && client.height == m_extent.height ? VK_SUCCESS : VK_SUBOPTIMAL_KHR;
#else
		return VK_ERROR_SURFACE_LOST_KHR;
#endif
	}
} // namespace vkd
