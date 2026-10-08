/**
 * @file PhysicalDevice.cpp
 * @brief Implementation of AMD physical device
 * @date 2026-08-31
 *
 * Enumeration flow reversed from amdvlk64.dll Instance_InitPhysicalDevices
 * (0x18023ec40): platform vtable+0x10 yields adapter LUIDs, then a 0x7fe8-byte
 * PhysicalDevice is constructed (FUN_18022ded0) and initialized (FUN_18022e9f0).
 *
 * FUN_18022e9f0 maps PAL GpuType 0/1/2 to VkPhysicalDeviceType
 * INTEGRATED/DISCRETE/VIRTUAL. Queue families match PAL Universal / Compute / DMA.
 */

#include "VkdAmd/PhysicalDevice/PhysicalDevice.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

#include "Vkd/Image/ColorPacking.hpp"
#include "Vkd/Memory/Memory.hpp"

#include "VkdAmd/Device/Device.hpp"
#include "VkdAmd/Shader/Gfx11Descriptors.hpp"

namespace vkd::amd
{
	static constexpr uint32_t kAmdVendorId = 0x1002;

	VkResult PhysicalDevice::Create(Instance& owner, const VkAllocationCallbacks& allocationCallbacks)
	{
		for (const auto& adapter : wddm::EnumerateAdapters())
		{
			if (adapter.vendorId != kAmdVendorId)
				continue;
			if (Create(owner, adapter, allocationCallbacks) == VK_SUCCESS)
				return VK_SUCCESS;
		}
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	VkResult PhysicalDevice::Create(Instance& owner, const wddm::AdapterInfo& adapter, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_adapter = adapter;

		VkPhysicalDeviceProperties props = {};
		props.apiVersion = VKD_VK_API_VERSION;
		props.driverVersion = VKD_DRIVER_VERSION;
		props.vendorID = kAmdVendorId;
		props.deviceID = adapter.deviceId;
		// STRIX_HALO / gfx1151 is an APU; PAL GpuType 0 -> INTEGRATED_GPU.
		props.deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;

		std::strncpy(props.deviceName, adapter.name, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);

		// Conservative GFX11 / RDNA3 limits
		props.limits = {
			.maxImageDimension1D = 16384,
			.maxImageDimension2D = 16384,
			.maxImageDimension3D = 2048,
			.maxImageDimensionCube = 16384,
			.maxImageArrayLayers = 2048,
			.maxTexelBufferElements = 134217728,
			.maxUniformBufferRange = 65536,
			.maxStorageBufferRange = 0xFFFFFFFF,
			.maxPushConstantsSize = 128,
			.maxMemoryAllocationCount = 4096,
			.maxSamplerAllocationCount = 4000,
			.bufferImageGranularity = 1,
			.sparseAddressSpaceSize = 0,
			.maxBoundDescriptorSets = 8,
			.maxPerStageDescriptorSamplers = 16,
			.maxPerStageDescriptorUniformBuffers = 14,
			.maxPerStageDescriptorStorageBuffers = 16,
			.maxPerStageDescriptorSampledImages = 128,
			.maxPerStageDescriptorStorageImages = 8,
			.maxPerStageDescriptorInputAttachments = 8,
			.maxPerStageResources = 128,
			.maxDescriptorSetSamplers = 96,
			.maxDescriptorSetUniformBuffers = 90,
			.maxDescriptorSetUniformBuffersDynamic = 8,
			.maxDescriptorSetStorageBuffers = 96,
			.maxDescriptorSetStorageBuffersDynamic = 8,
			.maxDescriptorSetSampledImages = 768,
			.maxDescriptorSetStorageImages = 48,
			.maxDescriptorSetInputAttachments = 8,
			.maxVertexInputAttributes = 32,
			.maxVertexInputBindings = 32,
			.maxVertexInputAttributeOffset = 2047,
			.maxVertexInputBindingStride = 2048,
			.maxVertexOutputComponents = 128,
			.maxComputeSharedMemorySize = 32768,
			.maxComputeWorkGroupCount = {65535, 65535, 65535},
			.maxComputeWorkGroupInvocations = 1024,
			.maxComputeWorkGroupSize = {1024, 1024, 1024},
			.subPixelPrecisionBits = 8,
			.subTexelPrecisionBits = 8,
			.mipmapPrecisionBits = 8,
			.maxDrawIndexedIndexValue = 0xFFFFFFFF,
			.maxDrawIndirectCount = 0xFFFFFFFF,
			.maxSamplerLodBias = 16.0f,
			.maxSamplerAnisotropy = 16.0f,
			.maxViewports = 16,
			.maxViewportDimensions = {16384, 16384},
			.viewportBoundsRange = {-32768.0f, 32767.0f},
			.viewportSubPixelBits = 8,
			.minMemoryMapAlignment = 64,
			.minTexelBufferOffsetAlignment = 4,
			.minUniformBufferOffsetAlignment = 16,
			.minStorageBufferOffsetAlignment = 4,
			.minTexelOffset = -8,
			.maxTexelOffset = 7,
			.minTexelGatherOffset = -8,
			.maxTexelGatherOffset = 7,
			.minInterpolationOffset = -0.5f,
			.maxInterpolationOffset = 0.4375f,
			.subPixelInterpolationOffsetBits = 4,
			.maxFramebufferWidth = 16384,
			.maxFramebufferHeight = 16384,
			.maxFramebufferLayers = 2048,
			// The hardware (and AMDVLK) does 1/2/4/8 samples, but Vkd can neither render nor resolve
			// MSAA on this GPU yet: advertise single-sampled only until draws exist.
			.framebufferColorSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.framebufferDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.framebufferStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.framebufferNoAttachmentsSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.maxColorAttachments = 8,
			.sampledImageColorSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.sampledImageIntegerSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.sampledImageDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.sampledImageStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.maxSampleMaskWords = 1,
			// The GPU clock RELEASE_MEM writes: 100 MHz, 64 bits (what AMDVLK reports).
			.timestampComputeAndGraphics = VK_TRUE,
			.timestampPeriod = 10.0f,
			.maxClipDistances = 8,
			.maxCullDistances = 8,
			.maxCombinedClipAndCullDistances = 8,
			.discreteQueuePriorities = 2,
			.pointSizeRange = {1.0f, 8191.875f},
			.lineWidthRange = {0.0f, 8191.875f},
			.pointSizeGranularity = 0.125f,
			.lineWidthGranularity = 0.125f,
			.strictLines = VK_FALSE,
			.standardSampleLocations = VK_TRUE,
			.optimalBufferCopyOffsetAlignment = 1,
			.optimalBufferCopyRowPitchAlignment = 1,
			.nonCoherentAtomSize = 256,
		};

		// PAL queue engines: Universal / Compute (ACE) / DMA (SDMA)
		std::array queueFamilies = {
			VkQueueFamilyProperties{
				.queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
				.queueCount = 1,
				.timestampValidBits = 64,
				.minImageTransferGranularity = {1, 1, 1}},
			VkQueueFamilyProperties{
				.queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
				.queueCount = 1,
				.timestampValidBits = 64,
				.minImageTransferGranularity = {1, 1, 1}},
			VkQueueFamilyProperties{
				.queueFlags = VK_QUEUE_TRANSFER_BIT,
				.queueCount = 1,
				.timestampValidBits = 64,
				.minImageTransferGranularity = {1, 1, 1}},
		};

		return vkd::PhysicalDevice::Create(owner, props, queueFamilies, allocationCallbacks);
	}

	namespace
	{
		// Formats whose copies and clears are byte moves of whole texels (CP DMA): the colors
		// PackColor encodes, and the single-aspect depth/stencil formats. Combined depth/stencil
		// formats need per-aspect copies inside a texel, which DMA can't do.
		bool IsTransferFormat(VkFormat format)
		{
			switch (format)
			{
				case VK_FORMAT_D16_UNORM:
				case VK_FORMAT_X8_D24_UNORM_PACK32:
				case VK_FORMAT_D32_SFLOAT:
				case VK_FORMAT_S8_UINT:
					return true;
				default:
					return vkd::IsSupportedColorFormat(format);
			}
		}
	} // namespace

	void PhysicalDevice::GetFormatProperties(VkFormat format, VkFormatProperties& properties) const
	{
		vkd::PhysicalDevice::GetFormatProperties(format, properties);
		if (!IsTransferFormat(format))
			return;
		VkFormatFeatureFlags features = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;

		// Blits run PAL's ScaledCopyImage RPM shaders: the source is sampled (any format the texture
		// unit reads), the destination written with image stores through its UNORM twin when it's
		// sRGB (neither the shared-exponent, the 96-bit nor R4G4 can be stored). Not for USCALED /
		// SSCALED: that shader writes saturated values for them -- AMDVLK too, measured
		// ([blit][vkd][scaled]); AMDVLK only gets 1:1 blits right, by turning them into raw copies.
		gfx11::ImageFormat imageFormat{};
		if (gfx11::GetImageFormat(format, imageFormat) && !vkuFormatIsUSCALED(format) && !vkuFormatIsSSCALED(format))
		{
			features |= VK_FORMAT_FEATURE_BLIT_SRC_BIT;
			if (!vkuFormatIsUINT(format) && !vkuFormatIsSINT(format) && !vkuFormatIsDepthOrStencil(format))
				features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
			if (format != VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 && format != VK_FORMAT_R4G4_UNORM_PACK8 && vkuFormatTexelBlockSize(format) != 12)
				features |= VK_FORMAT_FEATURE_BLIT_DST_BIT;
		}
		properties.linearTilingFeatures |= features;
		properties.optimalTilingFeatures |= features;
	}

	VkResult PhysicalDevice::GetImageFormatProperties(VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage,
													  VkImageCreateFlags flags, VkImageFormatProperties& properties) const
	{
		// RGBA8 keeps the base (attachment/sampled) support the test app relies on, single-sampled.
		if (vkd::PhysicalDevice::GetImageFormatProperties(format, type, tiling, usage, flags, properties) == VK_SUCCESS)
		{
			properties.sampleCounts = VK_SAMPLE_COUNT_1_BIT;
			return VK_SUCCESS;
		}

		properties = {};
		constexpr VkImageUsageFlags kTransferUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		constexpr VkImageCreateFlags kSupportedFlags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT |
													   VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
		if (!IsTransferFormat(format) || usage == 0 || (usage & ~kTransferUsage) != 0 || (flags & ~kSupportedFlags) != 0 ||
			(tiling != VK_IMAGE_TILING_OPTIMAL && tiling != VK_IMAGE_TILING_LINEAR))
			return VK_ERROR_FORMAT_NOT_SUPPORTED;
		if ((flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) && type != VK_IMAGE_TYPE_2D)
			return VK_ERROR_FORMAT_NOT_SUPPORTED;
		if ((flags & VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT) && type != VK_IMAGE_TYPE_3D)
			return VK_ERROR_FORMAT_NOT_SUPPORTED;

		const VkPhysicalDeviceLimits& limits = GetPhysicalDeviceProperties().limits;
		switch (type)
		{
			case VK_IMAGE_TYPE_1D:
				properties.maxExtent = {limits.maxImageDimension1D, 1, 1};
				properties.maxArrayLayers = limits.maxImageArrayLayers;
				break;
			case VK_IMAGE_TYPE_2D:
				properties.maxExtent = {limits.maxImageDimension2D, limits.maxImageDimension2D, 1};
				properties.maxArrayLayers = limits.maxImageArrayLayers;
				break;
			case VK_IMAGE_TYPE_3D:
				properties.maxExtent = {limits.maxImageDimension3D, limits.maxImageDimension3D, limits.maxImageDimension3D};
				properties.maxArrayLayers = 1;
				break;
			default:
				return VK_ERROR_FORMAT_NOT_SUPPORTED;
		}
		properties.maxMipLevels = std::bit_width(std::max({properties.maxExtent.width, properties.maxExtent.height, properties.maxExtent.depth}));
		properties.sampleCounts = VK_SAMPLE_COUNT_1_BIT;
		// One VkDeviceMemory is one WDDM allocation, kept under 4GB (AllocateGpuMemory).
		properties.maxResourceSize = 1ull << 31;
		return VK_SUCCESS;
	}

	DispatchableObjectResult<vkd::Device> PhysicalDevice::CreateDevice()
	{
		auto* amdDevice = mem::NewDeviceDispatchable<AmdDevice>(GetAllocationCallbacks());
		if (!amdDevice)
		{
			CCT_ASSERT_FALSE("Could not allocate AmdDevice");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return reinterpret_cast<DispatchableObject<vkd::Device>*>(amdDevice);
	}
} // namespace vkd::amd

namespace vkd
{
	DispatchableObject<PhysicalDevice>* VkdCreatePlatformPhysicalDevice(const VkAllocationCallbacks& allocationCallbacks)
	{
		DispatchableObject<amd::PhysicalDevice>* amdPhysicalDevice = mem::NewInstanceDispatchable<amd::PhysicalDevice>(allocationCallbacks);
		return reinterpret_cast<DispatchableObject<PhysicalDevice>*>(amdPhysicalDevice);
	}
} // namespace vkd
