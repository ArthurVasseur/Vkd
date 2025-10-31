/**
 * @file main.cpp
 * @brief Vulkan driver integration test
 * @date 2025-04-23
 *
 * Integration test for the Vkd software Vulkan driver implementation.
 */

#define VOLK_IMPLEMENTATION

#include <vulkan/vulkan.h>
#include <Concerto/Core/DynLib/DynLib.hpp>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <vector>
#include <cstring>
#include <iostream>
#include <volk.h>
#include <thread>

#define VK_CHECK(x) do { VkResult err = (x); if (err != VK_SUCCESS) { \
    std::cerr << "VK_CHECK failed at " << __FILE__ << ":" << __LINE__ << " -> " << err << std::endl; std::abort(); } } while(0)

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
		if (typeBits & (1u << i)) return i;
	}
	return 0;
}

int main()
{
	volkInitialize();
	VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app.pApplicationName = "Vkd MVP Test";
	app.apiVersion = VK_API_VERSION_1_1;

	//cct::DynLib driver;
	auto lib = LoadLibraryA("./vkd-Software.dll");
	//if (driver.Load("./vkd-Software" CONCERTO_DYNLIB_EXTENSION) == false)
	//{
	//	return EXIT_FAILURE;
	//}

	VkDirectDriverLoadingInfoLUNARG directLoadingInfo = {};
	directLoadingInfo.sType = VK_STRUCTURE_TYPE_DIRECT_DRIVER_LOADING_INFO_LUNARG;
	directLoadingInfo.pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddrLUNARG>(GetProcAddress(lib, "vk_icdGetInstanceProcAddr"));

	VkDirectDriverLoadingListLUNARG directDriverList = {};
	directDriverList.sType = VK_STRUCTURE_TYPE_DIRECT_DRIVER_LOADING_LIST_LUNARG;
	directDriverList.mode = VK_DIRECT_DRIVER_LOADING_MODE_EXCLUSIVE_LUNARG;
	directDriverList.driverCount = 1;
	directDriverList.pDrivers = &directLoadingInfo;

	constexpr std::array<const char*, 1> extensions = { VK_LUNARG_DIRECT_DRIVER_LOADING_EXTENSION_NAME };

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
		std::cerr << "No physical devices found." << std::endl;
		return 1;
	}
	std::vector<VkPhysicalDevice> pds(pdCount);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &pdCount, pds.data()));
	VkPhysicalDevice pysicalDevice = pds[0];

	uint32_t qFamily = findQueueFamily(pysicalDevice);
	float priority = 1.0f;
	VkDeviceQueueCreateInfo deviceQueueCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	deviceQueueCreateInfo.queueFamilyIndex = qFamily;
	deviceQueueCreateInfo.queueCount = 1;
	deviceQueueCreateInfo.pQueuePriorities = &priority;

	VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &deviceQueueCreateInfo;

	VkDevice device = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDevice(pysicalDevice, &dci, nullptr, &device));
	volkLoadDevice(device);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(device, qFamily, 0, &queue);

	VkCommandPoolCreateInfo cpci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cpci.queueFamilyIndex = qFamily;

	VkCommandPool pool = VK_NULL_HANDLE;
	VK_CHECK(vkCreateCommandPool(device, &cpci, nullptr, &pool));

	VkCommandBufferAllocateInfo cbai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	cbai.commandPool = pool;
	cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbai.commandBufferCount = 1;

	VkCommandBuffer cmd = VK_NULL_HANDLE;
	VK_CHECK(vkAllocateCommandBuffers(device, &cbai, &cmd));

	constexpr VkDeviceSize BufferSize = 64 * sizeof(cct::UInt32);

	auto createBuffer = [&](VkBuffer& buf)
	{
		VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
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
		VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
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

	VkCommandBufferBeginInfo cmdBufferBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBufferBeginInfo));
	{
		vkCmdFillBuffer(cmd, bufA, 0, BufferSize, 0x7F);

		VkBufferCopy region{};
		region.srcOffset = 0;
		region.dstOffset = 0;
		region.size = BufferSize;
		vkCmdCopyBuffer(cmd, bufA, bufB, 1, &region);
	}
	VK_CHECK(vkEndCommandBuffer(cmd));


	VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkFence fence = VK_NULL_HANDLE;
	VK_CHECK(vkCreateFence(device, &fci, nullptr, &fence));

	VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	si.commandBufferCount = 1;
	si.pCommandBuffers = &cmd;

	VK_CHECK(vkQueueSubmit(queue, 1, &si, fence));
	vkWaitForFences(device, 1, &fence, VK_TRUE, 5'000'000'000ull);

	void* mapped = nullptr;
	VK_CHECK(vkMapMemory(device, memB, 0, BufferSize, 0, &mapped));
	cct::UInt32* bytes = static_cast<cct::UInt32*>(mapped);
	size_t bad = 0;
	for (size_t i = 0; i < static_cast<size_t>(BufferSize) / sizeof(cct::UInt32); ++i)
	{
		if (bytes[i] != 0x7F)
		{
			bad++;
			break;
		}
	}
	vkUnmapMemory(device, memB);

	if (bad == 0)
	{
		std::cout << "[OK] Fill+Copy succeeded: all " << BufferSize << " bytes are 0x7F" << std::endl;
	}
	else
	{
		std::cout << "[FAIL] Verification failed" << std::endl;
	}

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
	return (bad == 0) ? 0 : 2;
}
