#define VOLK_IMPLEMENTATION

#include <array>
#include <iostream>
#include <vector>
#include <volk.h>

#include <Concerto/Core/DynLib/DynLib.hpp>

int main(int argc, const char** argv, const char** env)
{
	auto vkResult = volkInitialize();


	VkApplicationInfo applicationInfo = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pNext = nullptr,
		.pApplicationName = "vk-test",
		.applicationVersion = VK_MAKE_VERSION(0, 1, 0),
		.pEngineName = "vk-test-engine",
		.engineVersion = VK_MAKE_VERSION(0, 1, 1),
		.apiVersion = VK_HEADER_VERSION_COMPLETE,
	};

	cct::DynLib driver;
	if (driver.Load("./vkd-Software" CONCERTO_DYNLIB_EXTENSION) == false)
	{
		return EXIT_FAILURE;
	}

	VkDirectDriverLoadingInfoLUNARG directLoadingInfo = {};
	directLoadingInfo.sType = VK_STRUCTURE_TYPE_DIRECT_DRIVER_LOADING_INFO_LUNARG;
	directLoadingInfo.pfnGetInstanceProcAddr = static_cast<PFN_vkGetInstanceProcAddrLUNARG>(driver.GetSymbol("vk_icdGetInstanceProcAddr"));

	VkDirectDriverLoadingListLUNARG directDriverList = {};
	directDriverList.sType = VK_STRUCTURE_TYPE_DIRECT_DRIVER_LOADING_LIST_LUNARG;
	directDriverList.mode = VK_DIRECT_DRIVER_LOADING_MODE_EXCLUSIVE_LUNARG;
	directDriverList.driverCount = 1;
	directDriverList.pDrivers = &directLoadingInfo;

	constexpr std::array<const char*, 1> extensions = { VK_LUNARG_DIRECT_DRIVER_LOADING_EXTENSION_NAME };

	VkInstanceCreateInfo instanceCreateInfo = {};
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.pApplicationInfo = &applicationInfo;
	instanceCreateInfo.enabledExtensionCount = extensions.size();
	instanceCreateInfo.ppEnabledExtensionNames = extensions.data();
	instanceCreateInfo.pNext = &directDriverList;	

	VkInstance vkInstance;
	vkResult = vkCreateInstance(&instanceCreateInfo, nullptr, &vkInstance);


	volkLoadInstanceOnly(vkInstance);

	uint32_t physicalDeviceCount = 0;
	vkResult = vkEnumeratePhysicalDevices(vkInstance, &physicalDeviceCount, nullptr);

	std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);

	vkResult = vkEnumeratePhysicalDevices(vkInstance, &physicalDeviceCount, physicalDevices.data());

	for (auto physicalDevice : physicalDevices)
	{
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(physicalDevice, &properties);
		cct::Logger::Info("Device name: {}", properties.deviceName);
		cct::Logger::Info("API Version: {}.{}.{}", VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion));
		cct::Logger::Info("Driver Version: {}.{}.{}", VK_VERSION_MAJOR(properties.driverVersion), VK_VERSION_MINOR(properties.driverVersion), VK_VERSION_PATCH(properties.driverVersion));
	}

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[0], &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilyProperties;
	queueFamilyProperties.resize(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[0], &queueFamilyCount, queueFamilyProperties.data());

	VkDeviceCreateInfo deviceCreateInfo = {};
	VkDevice vkDevice;
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	vkResult = vkCreateDevice(physicalDevices[0], &deviceCreateInfo, nullptr, &vkDevice);

	return EXIT_SUCCESS;

}