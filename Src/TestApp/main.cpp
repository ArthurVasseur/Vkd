/**
 * @file main.cpp
 * @brief Vulkan driver integration test
 * @date 2025-04-23
 *
 * Integration test for the Vkd software Vulkan driver implementation.
 */

#define VOLK_IMPLEMENTATION

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <volk.h>

#include <Concerto/Core/DynLib/DynLib.hpp>
#include <Concerto/Core/Logger/Logger.hpp>

#include <vulkan/vulkan.h>

#define VK_CHECK(x)                                                                                          \
	do                                                                                                       \
	{                                                                                                        \
		VkResult err = (x);                                                                                  \
		if (err != VK_SUCCESS)                                                                               \
		{                                                                                                    \
			cct::Logger::Error("VK_CHECK failed at {}:{} -> {}", __FILE__, __LINE__, static_cast<int>(err)); \
			std::abort();                                                                                    \
		}                                                                                                    \
	} while (0)

static uint32_t findQueueFamily(VkPhysicalDevice pd)
{
	uint32_t count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, nullptr);
	std::vector<VkQueueFamilyProperties> props(count);
	vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, props.data());
	for (uint32_t i = 0; i < count; ++i)
	{
		if (props[i].queueCount > 0 &&
			(props[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)))
			return i;
	}
	return 0;
}

static uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t typeBits, VkMemoryPropertyFlags required)
{
	VkPhysicalDeviceMemoryProperties mp{};
	vkGetPhysicalDeviceMemoryProperties(pd, &mp);
	for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
	{
		if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & required) == required)
		{
			return i;
		}
	}
	for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
	{
		if (typeBits & (1u << i))
			return i;
	}
	return 0;
}

int main()
{
	volkInitialize();
	VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
	app.pApplicationName = "Vkd MVP Test";
	app.apiVersion = VK_API_VERSION_1_1;

	cct::DynLib driver;
	if (driver.Load(
			"./"
#ifdef CCT_PLATFORM_POSIX
			"lib"
#endif
			"vkd-Software" CONCERTO_DYNLIB_EXTENSION) == false)
		return EXIT_FAILURE;

	VkDirectDriverLoadingInfoLUNARG directLoadingInfo = {};
	directLoadingInfo.sType = VK_STRUCTURE_TYPE_DIRECT_DRIVER_LOADING_INFO_LUNARG;
	directLoadingInfo.pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddrLUNARG>(driver.GetSymbol("vk_icdGetInstanceProcAddr"));

	VkDirectDriverLoadingListLUNARG directDriverList = {};
	directDriverList.sType = VK_STRUCTURE_TYPE_DIRECT_DRIVER_LOADING_LIST_LUNARG;
	directDriverList.mode = VK_DIRECT_DRIVER_LOADING_MODE_EXCLUSIVE_LUNARG;
	directDriverList.driverCount = 1;
	directDriverList.pDrivers = &directLoadingInfo;

	constexpr std::array<const char*, 1> extensions = {VK_LUNARG_DIRECT_DRIVER_LOADING_EXTENSION_NAME};

	VkInstanceCreateInfo instanceCreateInfo = {};
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.pApplicationInfo = nullptr;
	instanceCreateInfo.enabledExtensionCount = extensions.size();
	instanceCreateInfo.ppEnabledExtensionNames = extensions.data();
	instanceCreateInfo.pNext = &directDriverList;

	VkInstance instance = VK_NULL_HANDLE;
	VK_CHECK(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));
	volkLoadInstance(instance);
	uint32_t pdCount = 0;
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &pdCount, nullptr));
	if (pdCount == 0)
	{
		cct::Logger::Error("No physical devices found.");
		return 1;
	}
	std::vector<VkPhysicalDevice> pds(pdCount);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &pdCount, pds.data()));
	VkPhysicalDevice pysicalDevice = pds[0];

	uint32_t qFamily = findQueueFamily(pysicalDevice);
	float priority = 1.0f;
	VkDeviceQueueCreateInfo deviceQueueCreateInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
	deviceQueueCreateInfo.queueFamilyIndex = qFamily;
	deviceQueueCreateInfo.queueCount = 1;
	deviceQueueCreateInfo.pQueuePriorities = &priority;

	VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &deviceQueueCreateInfo;

	VkDevice device = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDevice(pysicalDevice, &dci, nullptr, &device));
	volkLoadDevice(device);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(device, qFamily, 0, &queue);

	VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
	cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cpci.queueFamilyIndex = qFamily;

	VkCommandPool pool = VK_NULL_HANDLE;
	VK_CHECK(vkCreateCommandPool(device, &cpci, nullptr, &pool));

	VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	cbai.commandPool = pool;
	cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbai.commandBufferCount = 1;

	VkCommandBuffer cmd = VK_NULL_HANDLE;
	VK_CHECK(vkAllocateCommandBuffers(device, &cbai, &cmd));

	constexpr VkDeviceSize BufferSize = 64 * sizeof(cct::UInt32);

	auto createBuffer = [&](VkBuffer& buf)
	{
		VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		bci.size = BufferSize;
		bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK(vkCreateBuffer(device, &bci, nullptr, &buf));
	};

	VkBuffer bufA = VK_NULL_HANDLE, bufB = VK_NULL_HANDLE;
	createBuffer(bufA);
	createBuffer(bufB);

	auto allocAndBind = [&](VkBuffer buf, VkDeviceMemory& mem)
	{
		VkMemoryRequirements req{};
		vkGetBufferMemoryRequirements(device, buf, &req);
		VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		mai.allocationSize = req.size;
		uint32_t typeIndex = findMemoryType(pysicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		mai.memoryTypeIndex = typeIndex;
		VK_CHECK(vkAllocateMemory(device, &mai, nullptr, &mem));
		VK_CHECK(vkBindBufferMemory(device, buf, mem, 0));
	};

	VkDeviceMemory memA = VK_NULL_HANDLE;
	VkDeviceMemory memB = VK_NULL_HANDLE;
	allocAndBind(bufA, memA);
	allocAndBind(bufB, memB);

	VkCommandBufferBeginInfo cmdBufferBeginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBufferBeginInfo));
	{
		vkCmdFillBuffer(cmd, bufA, 0, BufferSize, 0x7F);

		VkBufferCopy region{};
		region.srcOffset = 0;
		region.dstOffset = 0;
		region.size = BufferSize;
		vkCmdCopyBuffer(cmd, bufA, bufB, 1, &region);

		std::vector<cct::UInt32> updateData(16, 0xDEADBEEF);
		vkCmdUpdateBuffer(cmd, bufB, 0, updateData.size() * sizeof(cct::UInt32), updateData.data());

		VkBufferCopy2 region2{VK_STRUCTURE_TYPE_BUFFER_COPY_2};
		region2.srcOffset = 0;
		region2.dstOffset = BufferSize / 2;
		region2.size = BufferSize / 2;

		VkCopyBufferInfo2 copyInfo2{VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2};
		copyInfo2.srcBuffer = bufB;
		copyInfo2.dstBuffer = bufA;
		copyInfo2.regionCount = 1;
		copyInfo2.pRegions = &region2;
		vkCmdCopyBuffer2(cmd, &copyInfo2);
	}
	VK_CHECK(vkEndCommandBuffer(cmd));

	VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	VkFence fence = VK_NULL_HANDLE;
	VK_CHECK(vkCreateFence(device, &fci, nullptr, &fence));

	VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
	si.commandBufferCount = 1;
	si.pCommandBuffers = &cmd;

	VK_CHECK(vkQueueSubmit(queue, 1, &si, fence));
	vkWaitForFences(device, 1, &fence, VK_TRUE, std::numeric_limits<cct::UInt64>::max());

	size_t totalErrors = 0;

	void* mappedB = nullptr;
	VK_CHECK(vkMapMemory(device, memB, 0, BufferSize, 0, &mappedB));
	cct::UInt32* bytesB = static_cast<cct::UInt32*>(mappedB);

	for (size_t i = 0; i < 16; ++i)
	{
		if (bytesB[i] != 0xDEADBEEF)
		{
			cct::Logger::Error("UpdateBuffer: bufB[{}] = 0x{:x}, expected 0xDEADBEEF", i, bytesB[i]);
			totalErrors++;
			break;
		}
	}

	for (size_t i = 16; i < static_cast<size_t>(BufferSize) / sizeof(cct::UInt32); ++i)
	{
		if (bytesB[i] != 0x7F)
		{
			cct::Logger::Error("CopyBuffer: bufB[{}] = 0x{:x}, expected 0x7F", i, bytesB[i]);
			totalErrors++;
			break;
		}
	}
	vkUnmapMemory(device, memB);

	if (totalErrors == 0)
		cct::Logger::Info("UpdateBuffer succeeded: bufB[0..16] = 0xDEADBEEF, bufB[16..64] = 0x7F");

	void* mappedA = nullptr;
	VK_CHECK(vkMapMemory(device, memA, 0, BufferSize, 0, &mappedA));
	cct::UInt32* bytesA = static_cast<cct::UInt32*>(mappedA);

	for (size_t i = 0; i < static_cast<size_t>(BufferSize) / sizeof(cct::UInt32) / 2; ++i)
	{
		if (bytesA[i] != 0x7F)
		{
			cct::Logger::Error("FillBuffer: bufA[{}] = 0x{:x}, expected 0x7F", i, bytesA[i]);
			totalErrors++;
			break;
		}
	}

	for (size_t i = static_cast<size_t>(BufferSize) / sizeof(cct::UInt32) / 2; i < static_cast<size_t>(BufferSize) / sizeof(cct::UInt32) / 2 + 16; ++i)
	{
		if (bytesA[i] != 0xDEADBEEF)
		{
			cct::Logger::Error("CopyBuffer2: bufA[{}] = 0x{:x}, expected 0xDEADBEEF", i, bytesA[i]);
			totalErrors++;
			break;
		}
	}

	for (size_t i = static_cast<size_t>(BufferSize) / sizeof(cct::UInt32) / 2 + 16; i < static_cast<size_t>(BufferSize) / sizeof(cct::UInt32); ++i)
	{
		if (bytesA[i] != 0x7F)
		{
			cct::Logger::Error("CopyBuffer2: bufA[{}] = 0x{:x}, expected 0x7F", i, bytesA[i]);
			totalErrors++;
			break;
		}
	}
	vkUnmapMemory(device, memA);

	if (totalErrors == 0)
		cct::Logger::Info("CopyBuffer2 succeeded: bufA[0..32] = 0x7F, bufA[32..48] = 0xDEADBEEF, bufA[48..64] = 0x7F");
	else
		cct::Logger::Error("Total verification errors: {}", totalErrors);

	cct::Logger::Info("All buffer tests completed successfully!");

	cct::Logger::Info("Starting image tests...");
	VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = VK_FORMAT_R8G8B8A8_UNORM;
	ici.extent = {16, 16, 1};
	ici.mipLevels = 1;
	ici.arrayLayers = 1;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_LINEAR;
	ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkImage imageA = VK_NULL_HANDLE;
	VkImage imageB = VK_NULL_HANDLE;
	VK_CHECK(vkCreateImage(device, &ici, nullptr, &imageA));
	VK_CHECK(vkCreateImage(device, &ici, nullptr, &imageB));

	auto allocAndBindImage = [&](VkImage img, VkDeviceMemory& mem)
	{
		VkMemoryRequirements req{};
		vkGetImageMemoryRequirements(device, img, &req);
		VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		mai.allocationSize = req.size;
		uint32_t typeIndex = findMemoryType(pysicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		mai.memoryTypeIndex = typeIndex;
		VK_CHECK(vkAllocateMemory(device, &mai, nullptr, &mem));
		VK_CHECK(vkBindImageMemory(device, img, mem, 0));
	};

	VkDeviceMemory memImageA = VK_NULL_HANDLE;
	VkDeviceMemory memImageB = VK_NULL_HANDLE;
	allocAndBindImage(imageA, memImageA);
	allocAndBindImage(imageB, memImageB);

	constexpr VkDeviceSize ImageBufferSize = 16 * 16 * 4;
	VkBuffer bufImage = VK_NULL_HANDLE;
	VkBufferCreateInfo bciImage{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	bciImage.size = ImageBufferSize;
	bciImage.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bciImage.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK(vkCreateBuffer(device, &bciImage, nullptr, &bufImage));

	VkDeviceMemory memBufImage = VK_NULL_HANDLE;
	allocAndBind(bufImage, memBufImage);

	void* mappedBufImage = nullptr;
	VK_CHECK(vkMapMemory(device, memBufImage, 0, ImageBufferSize, 0, &mappedBufImage));
	cct::UInt32* bufImageData = static_cast<cct::UInt32*>(mappedBufImage);
	for (size_t i = 0; i < ImageBufferSize / sizeof(cct::UInt32); ++i)
		bufImageData[i] = 0xAABBCCDD;
	vkUnmapMemory(device, memBufImage);

	VkCommandBuffer cmdImage = VK_NULL_HANDLE;
	VK_CHECK(vkAllocateCommandBuffers(device, &cbai, &cmdImage));

	VK_CHECK(vkBeginCommandBuffer(cmdImage, &cmdBufferBeginInfo));
	{
		VkClearColorValue clearColor{};
		clearColor.uint32[0] = 0xFF;
		clearColor.uint32[1] = 0x00;
		clearColor.uint32[2] = 0xFF;
		clearColor.uint32[3] = 0xFF;

		VkImageSubresourceRange range{};
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = 0;
		range.levelCount = 1;
		range.baseArrayLayer = 0;
		range.layerCount = 1;

		vkCmdClearColorImage(cmdImage, imageA, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);

		VkBufferImageCopy copyRegion{};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;
		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageOffset = {0, 0, 0};
		copyRegion.imageExtent = {16, 16, 1};

		vkCmdCopyBufferToImage(cmdImage, bufImage, imageB, VK_IMAGE_LAYOUT_GENERAL, 1, &copyRegion);

		VkImageCopy imageCopyRegion{};
		imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageCopyRegion.srcSubresource.mipLevel = 0;
		imageCopyRegion.srcSubresource.baseArrayLayer = 0;
		imageCopyRegion.srcSubresource.layerCount = 1;
		imageCopyRegion.srcOffset = {0, 0, 0};
		imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageCopyRegion.dstSubresource.mipLevel = 0;
		imageCopyRegion.dstSubresource.baseArrayLayer = 0;
		imageCopyRegion.dstSubresource.layerCount = 1;
		imageCopyRegion.dstOffset = {0, 0, 0};
		imageCopyRegion.extent = {8, 8, 1};

		vkCmdCopyImage(cmdImage, imageA, VK_IMAGE_LAYOUT_GENERAL, imageB, VK_IMAGE_LAYOUT_GENERAL, 1, &imageCopyRegion);
	}
	VK_CHECK(vkEndCommandBuffer(cmdImage));

	VkBuffer bufImageResult = VK_NULL_HANDLE;
	VK_CHECK(vkCreateBuffer(device, &bciImage, nullptr, &bufImageResult));
	VkDeviceMemory memBufImageResult = VK_NULL_HANDLE;
	allocAndBind(bufImageResult, memBufImageResult);

	VkCommandBuffer cmdImageCopy = VK_NULL_HANDLE;
	VK_CHECK(vkAllocateCommandBuffers(device, &cbai, &cmdImageCopy));
	VK_CHECK(vkBeginCommandBuffer(cmdImageCopy, &cmdBufferBeginInfo));
	{
		VkBufferImageCopy copyRegion{};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;
		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageOffset = {0, 0, 0};
		copyRegion.imageExtent = {16, 16, 1};
		vkCmdCopyImageToBuffer(cmdImageCopy, imageB, VK_IMAGE_LAYOUT_GENERAL, bufImageResult, 1, &copyRegion);
	}
	VK_CHECK(vkEndCommandBuffer(cmdImageCopy));

	VkFence fenceImage = VK_NULL_HANDLE;
	VK_CHECK(vkCreateFence(device, &fci, nullptr, &fenceImage));

	VkSubmitInfo siImage{VK_STRUCTURE_TYPE_SUBMIT_INFO};
	siImage.commandBufferCount = 1;
	siImage.pCommandBuffers = &cmdImage;
	VK_CHECK(vkQueueSubmit(queue, 1, &siImage, fenceImage));
	vkWaitForFences(device, 1, &fenceImage, VK_TRUE, std::numeric_limits<cct::UInt64>::max());

	vkResetFences(device, 1, &fenceImage);
	siImage.pCommandBuffers = &cmdImageCopy;
	VK_CHECK(vkQueueSubmit(queue, 1, &siImage, fenceImage));
	vkWaitForFences(device, 1, &fenceImage, VK_TRUE, std::numeric_limits<cct::UInt64>::max());

	void* mappedResult = nullptr;
	VK_CHECK(vkMapMemory(device, memBufImageResult, 0, ImageBufferSize, 0, &mappedResult));
	cct::UInt32* resultData = static_cast<cct::UInt32*>(mappedResult);

	for (size_t y = 0; y < 8; ++y)
	{
		for (size_t x = 0; x < 8; ++x)
		{
			size_t idx = y * 16 + x;
			if (resultData[idx] != 0xFFFF00FF)
			{
				cct::Logger::Error("CopyImage: imageB[{},{}] = 0x{:x}, expected 0xFFFF00FF", x, y, resultData[idx]);
				totalErrors++;
				break;
			}
		}
		if (totalErrors > 0)
			break;
	}

	for (size_t y = 0; y < 16; ++y)
	{
		for (size_t x = 8; x < 16; ++x)
		{
			if (y < 8)
				continue;
			size_t idx = y * 16 + x;
			if (resultData[idx] != 0xAABBCCDD)
			{
				cct::Logger::Error("CopyBufferToImage: imageB[{},{}] = 0x{:x}, expected 0xAABBCCDD", x, y, resultData[idx]);
				totalErrors++;
				break;
			}
		}
		if (totalErrors > 0)
			break;
	}

	vkUnmapMemory(device, memBufImageResult);

	if (totalErrors == 0)
		cct::Logger::Info("Image operations succeeded: ClearColorImage, CopyBufferToImage, CopyImage, CopyImageToBuffer");

	vkDestroyFence(device, fenceImage, nullptr);
	vkFreeCommandBuffers(device, pool, 1, &cmdImage);
	vkFreeCommandBuffers(device, pool, 1, &cmdImageCopy);
	vkDestroyBuffer(device, bufImageResult, nullptr);
	vkFreeMemory(device, memBufImageResult, nullptr);
	vkDestroyImage(device, imageA, nullptr);
	vkDestroyImage(device, imageB, nullptr);
	vkFreeMemory(device, memImageA, nullptr);
	vkFreeMemory(device, memImageB, nullptr);
	vkDestroyBuffer(device, bufImage, nullptr);
	vkFreeMemory(device, memBufImage, nullptr);

	vkDestroyFence(device, fence, nullptr);
	vkFreeCommandBuffers(device, pool, 1, &cmd);
	vkDestroyCommandPool(device, pool, nullptr);
	vkDestroyBuffer(device, bufA, nullptr);
	vkDestroyBuffer(device, bufB, nullptr);
	vkFreeMemory(device, memA, nullptr);
	vkFreeMemory(device, memB, nullptr);
	vkDestroyDevice(device, nullptr);
	vkDestroyInstance(instance, nullptr);

	std::this_thread::sleep_for(std::chrono::seconds(1));
	return (totalErrors == 0) ? 0 : 2;
}
