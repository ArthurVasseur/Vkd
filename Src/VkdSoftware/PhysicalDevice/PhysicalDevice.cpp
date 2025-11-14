/**
 * @file PhysicalDevice.cpp
 * @brief Implementation of software renderer physical device
 * @date 2025-10-25
 */

#include "VkdSoftware/PhysicalDevice/PhysicalDevice.hpp"
#include "VkdSoftware/Device/Device.hpp"

namespace vkd::software
{
	VkResult PhysicalDevice::Create(Instance& owner, const VkAllocationCallbacks& allocationCallbacks)
	{
		VkPhysicalDeviceProperties physicalDeviceProperties = {
			.apiVersion = VKD_VK_API_VERSION,
			.driverVersion = VKD_DRIVER_VERSION,
			.vendorID = 0x0601,
			.deviceID = 0x060103,
			.deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU,
			.deviceName = {},
			.pipelineCacheUUID = {},
			.limits = {
			.maxImageDimension1D = 4096,
			.maxImageDimension2D = 4096,
			.maxImageDimension3D = 256,
			.maxImageDimensionCube = 4096,
			.maxImageArrayLayers = 256,
			.maxTexelBufferElements = 65536,
			.maxUniformBufferRange = 16384,
			.maxStorageBufferRange = 134217728,
			.maxPushConstantsSize = 128,
			.maxMemoryAllocationCount = 4096,
			.maxSamplerAllocationCount = 4000,
			.bufferImageGranularity = 131072,
			.sparseAddressSpaceSize = 0,
			.maxBoundDescriptorSets = 4,
			.maxPerStageDescriptorSamplers = 16,
			.maxPerStageDescriptorUniformBuffers = 12,
			.maxPerStageDescriptorStorageBuffers = 4,
			.maxPerStageDescriptorSampledImages = 16,
			.maxPerStageDescriptorStorageImages = 4,
			.maxPerStageDescriptorInputAttachments = 4,
			.maxPerStageResources = 128,
			.maxDescriptorSetSamplers = 96,
			.maxDescriptorSetUniformBuffers = 72,
			.maxDescriptorSetUniformBuffersDynamic = 8,
			.maxDescriptorSetStorageBuffers = 24,
			.maxDescriptorSetStorageBuffersDynamic = 4,
			.maxDescriptorSetSampledImages = 96,
			.maxDescriptorSetStorageImages = 24,
			.maxDescriptorSetInputAttachments = 4,
			.maxVertexInputAttributes = 16,
			.maxVertexInputBindings = 16,
			.maxVertexInputAttributeOffset = 2047,
			.maxVertexInputBindingStride = 2048,
			.maxVertexOutputComponents = 64,
			.maxTessellationGenerationLevel = 0,
			.maxTessellationPatchSize = 0,
			.maxTessellationControlPerVertexInputComponents = 0,
			.maxTessellationControlPerVertexOutputComponents = 0,
			.maxTessellationControlPerPatchOutputComponents = 0,
			.maxTessellationControlTotalOutputComponents = 0,
			.maxTessellationEvaluationInputComponents = 0,
			.maxTessellationEvaluationOutputComponents = 0,
			.maxGeometryShaderInvocations = 0,
			.maxGeometryInputComponents = 0,
			.maxGeometryOutputComponents = 0,
			.maxGeometryOutputVertices = 0,
			.maxGeometryTotalOutputComponents = 0,
			.maxFragmentInputComponents = 64,
			.maxFragmentOutputAttachments = 4,
			.maxFragmentDualSrcAttachments = 0,
			.maxFragmentCombinedOutputResources = 4,
			.maxComputeSharedMemorySize = 16384,
			.maxComputeWorkGroupCount = {65535, 65535, 65535},
			.maxComputeWorkGroupInvocations = 128,
			.maxComputeWorkGroupSize = {128, 128, 64},
			.subPixelPrecisionBits = 4,
			.subTexelPrecisionBits = 4,
			.mipmapPrecisionBits = 4,
			.maxDrawIndexedIndexValue = 4294967295,
			.maxDrawIndirectCount = 65535,
			.maxSamplerLodBias = 2.0f,
			.maxSamplerAnisotropy = 1.0f,
			.maxViewports = 1,
			.maxViewportDimensions = {4096, 4096},
			.viewportBoundsRange = {-8192.0f, 8191.0f},
			.viewportSubPixelBits = 0,
			.minMemoryMapAlignment = 64,
			.minTexelBufferOffsetAlignment = 256,
			.minUniformBufferOffsetAlignment = 256,
			.minStorageBufferOffsetAlignment = 256,
			.minTexelOffset = -8,
			.maxTexelOffset = 7,
			.minTexelGatherOffset = 0,
			.maxTexelGatherOffset = 0,
			.minInterpolationOffset = 0.0f,
			.maxInterpolationOffset = 0.0f,
			.subPixelInterpolationOffsetBits = 0,
			.maxFramebufferWidth = 4096,
			.maxFramebufferHeight = 4096,
			.maxFramebufferLayers = 256,
			.framebufferColorSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.framebufferDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.framebufferStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.framebufferNoAttachmentsSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.maxColorAttachments = 4,
			.sampledImageColorSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.sampledImageIntegerSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.sampledImageDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.sampledImageStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT,
			.maxSampleMaskWords = 1,
			.timestampComputeAndGraphics = VK_FALSE,
			.timestampPeriod = 1.0f,
			.maxClipDistances = 0,
			.maxCullDistances = 0,
			.maxCombinedClipAndCullDistances = 0,
			.discreteQueuePriorities = 2,
			.pointSizeRange = {1.0f, 1.0f},
			.lineWidthRange = {1.0f, 1.0f},
			.pointSizeGranularity = 0.0f,
			.lineWidthGranularity = 0.0f,
			.strictLines = VK_FALSE,
			.standardSampleLocations = VK_TRUE,
			.optimalBufferCopyOffsetAlignment = 1,
			.optimalBufferCopyRowPitchAlignment = 1,
			.nonCoherentAtomSize = 256,
		},
			.sparseProperties = {},
		};

		using namespace std::string_view_literals;
		constexpr std::string_view deviceName = "Vkd software device"sv;
		std::memcpy(physicalDeviceProperties.deviceName, deviceName.data(), deviceName.size());

		std::array queueFamilyProperties = {
			VkQueueFamilyProperties
			{
				.queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
				.queueCount = 1,
				.timestampValidBits = 0,
				.minImageTransferGranularity = {1, 1, 1}
			},
			VkQueueFamilyProperties
			{
				.queueFlags = VK_QUEUE_GRAPHICS_BIT,
				.queueCount = 1,
				.timestampValidBits = 0,
				.minImageTransferGranularity = {1, 1, 1},
			},
			VkQueueFamilyProperties
			{
				.queueFlags = VK_QUEUE_TRANSFER_BIT,
				.queueCount = 1,
				.timestampValidBits = 0,
				.minImageTransferGranularity = {1, 1, 1},
			}
		};

		return vkd::PhysicalDevice::Create(owner, std::move(physicalDeviceProperties), std::move(queueFamilyProperties), allocationCallbacks);
	}

	DispatchableObjectResult<vkd::Device> PhysicalDevice::CreateDevice()
	{
		VKD_AUTO_PROFILER_SCOPE();

		DispatchableObject<SoftwareDevice>* softwareDevice = mem::NewDispatchable<SoftwareDevice>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
		if (!softwareDevice)
		{
			CCT_ASSERT_FALSE("Could not allocate new SoftwareDevice");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return reinterpret_cast<DispatchableObject<Device>*>(softwareDevice);
	}
}
