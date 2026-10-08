/**
 * @file main.cpp
 * @brief Windowed demo: a triangle presented through VK_KHR_swapchain
 * @date 2026-10-07
 *
 * Loads a Vkd ICD directly (argv[1] or VKD_ICD, default vkd-Software), opens a Win32 window and
 * draws a triangle into each swapchain image -- its viewport moves every frame, the clear color too
 * -- then presents. After the last frame it reads the window back through GDI and checks that the
 * triangle's color reached the screen.
 */

#include <Windows.h>

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace
{
	constexpr uint32_t kWidth = 640;
	constexpr uint32_t kHeight = 480;
	constexpr uint32_t kFrames = 180;

	// glslc -O. VS: positions from gl_VertexIndex -- (0,-0.5) (0.5,0.5) (-0.5,0.5); FS: vec4(0.25, 0.5, 0.75, 1.0).
	constexpr uint32_t kTriVertSpirv[] = {
		0x07230203, 0x00010600, 0x000D000B, 0x0000002F, 0x00000000, 0x00020011, 0x00000001, 0x0006000B,
		0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001,
		0x0007000F, 0x00000000, 0x00000004, 0x6E69616D, 0x00000000, 0x0000000B, 0x00000027, 0x00040047,
		0x0000000B, 0x0000000B, 0x0000002A, 0x00030047, 0x00000025, 0x00000002, 0x00050048, 0x00000025,
		0x00000000, 0x0000000B, 0x00000000, 0x00050048, 0x00000025, 0x00000001, 0x0000000B, 0x00000001,
		0x00050048, 0x00000025, 0x00000002, 0x0000000B, 0x00000003, 0x00050048, 0x00000025, 0x00000003,
		0x0000000B, 0x00000004, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
		0x00000006, 0x00000020, 0x00040015, 0x00000009, 0x00000020, 0x00000001, 0x00040020, 0x0000000A,
		0x00000001, 0x00000009, 0x0004003B, 0x0000000A, 0x0000000B, 0x00000001, 0x0004002B, 0x00000009,
		0x0000000D, 0x00000001, 0x00020014, 0x0000000E, 0x0004002B, 0x00000006, 0x00000013, 0x3F000000,
		0x0004002B, 0x00000009, 0x00000016, 0x00000002, 0x0004002B, 0x00000006, 0x00000018, 0xBF000000,
		0x0004002B, 0x00000006, 0x00000019, 0x00000000, 0x0004002B, 0x00000009, 0x0000001E, 0x00000000,
		0x00040017, 0x00000021, 0x00000006, 0x00000004, 0x00040015, 0x00000022, 0x00000020, 0x00000000,
		0x0004002B, 0x00000022, 0x00000023, 0x00000001, 0x0004001C, 0x00000024, 0x00000006, 0x00000023,
		0x0006001E, 0x00000025, 0x00000021, 0x00000006, 0x00000024, 0x00000024, 0x00040020, 0x00000026,
		0x00000003, 0x00000025, 0x0004003B, 0x00000026, 0x00000027, 0x00000003, 0x0004002B, 0x00000006,
		0x0000002A, 0x3F800000, 0x00040020, 0x0000002C, 0x00000003, 0x00000021, 0x00050036, 0x00000002,
		0x00000004, 0x00000000, 0x00000003, 0x000200F8, 0x00000005, 0x0004003D, 0x00000009, 0x0000000C,
		0x0000000B, 0x000500AA, 0x0000000E, 0x0000000F, 0x0000000C, 0x0000000D, 0x000300F7, 0x00000012,
		0x00000000, 0x000400FA, 0x0000000F, 0x00000011, 0x00000014, 0x000200F8, 0x00000011, 0x000200F9,
		0x00000012, 0x000200F8, 0x00000014, 0x000500AA, 0x0000000E, 0x00000017, 0x0000000C, 0x00000016,
		0x000600A9, 0x00000006, 0x0000001A, 0x00000017, 0x00000018, 0x00000019, 0x000200F9, 0x00000012,
		0x000200F8, 0x00000012, 0x000700F5, 0x00000006, 0x0000002E, 0x00000013, 0x00000011, 0x0000001A,
		0x00000014, 0x000500AA, 0x0000000E, 0x0000001F, 0x0000000C, 0x0000001E, 0x000600A9, 0x00000006,
		0x00000020, 0x0000001F, 0x00000018, 0x00000013, 0x00070050, 0x00000021, 0x0000002B, 0x0000002E,
		0x00000020, 0x00000019, 0x0000002A, 0x00050041, 0x0000002C, 0x0000002D, 0x00000027, 0x0000001E,
		0x0003003E, 0x0000002D, 0x0000002B, 0x000100FD, 0x00010038};

	constexpr uint32_t kTriFragSpirv[] = {
		0x07230203, 0x00010600, 0x000D000B, 0x0000000F, 0x00000000, 0x00020011, 0x00000001, 0x0006000B,
		0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001,
		0x0006000F, 0x00000004, 0x00000004, 0x6E69616D, 0x00000000, 0x00000009, 0x00030010, 0x00000004,
		0x00000007, 0x00040047, 0x00000009, 0x0000001E, 0x00000000, 0x00020013, 0x00000002, 0x00030021,
		0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006,
		0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003B, 0x00000008, 0x00000009,
		0x00000003, 0x0004002B, 0x00000006, 0x0000000A, 0x3E800000, 0x0004002B, 0x00000006, 0x0000000B,
		0x3F000000, 0x0004002B, 0x00000006, 0x0000000C, 0x3F400000, 0x0004002B, 0x00000006, 0x0000000D,
		0x3F800000, 0x0007002C, 0x00000007, 0x0000000E, 0x0000000A, 0x0000000B, 0x0000000C, 0x0000000D,
		0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200F8, 0x00000005, 0x0003003E,
		0x00000009, 0x0000000E, 0x000100FD, 0x00010038};

	bool g_closed = false;

	LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_CLOSE)
		{
			g_closed = true;
			return 0;
		}
		return DefWindowProcW(window, message, wParam, lParam);
	}

	struct Vk
	{
		PFN_vkGetInstanceProcAddr gipa = nullptr;
		VkInstance instance = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;

		template<typename T>
		T I(const char* name) const
		{
			return reinterpret_cast<T>(gipa(instance, name));
		}
		template<typename T>
		T D(const char* name) const
		{
			return reinterpret_cast<T>(gipa(instance, name));
		}
	};

#define VK_CALL(expr)                                                              \
	do                                                                             \
	{                                                                              \
		const VkResult vkCallResult = (expr);                                      \
		if (vkCallResult != VK_SUCCESS)                                            \
		{                                                                          \
			std::fprintf(stderr, "%s failed: VkResult %d\n", #expr, vkCallResult); \
			return EXIT_FAILURE;                                                   \
		}                                                                          \
	} while (false)
} // namespace

int main(int argc, char** argv)
{
	std::string driver = "vkd-Software";
	if (const char* env = std::getenv("VKD_ICD"))
		driver = env;
	if (argc > 1)
		driver = argv[1];
	const HMODULE icd = LoadLibraryA((driver + ".dll").c_str());
	if (!icd)
	{
		std::fprintf(stderr, "can't load %s.dll (error %lu)\n", driver.c_str(), GetLastError());
		return EXIT_FAILURE;
	}
	Vk vk;
	vk.gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(icd, "vk_icdGetInstanceProcAddr"));
	if (!vk.gipa)
		return EXIT_FAILURE;

	// Window
	const HINSTANCE hinstance = GetModuleHandleW(nullptr);
	WNDCLASSW windowClass{};
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hinstance;
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	windowClass.lpszClassName = L"VkdWindowDemo";
	RegisterClassW(&windowClass);
	RECT rect{0, 0, static_cast<LONG>(kWidth), static_cast<LONG>(kHeight)};
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
	const std::wstring title = L"Vkd window demo - " + std::wstring(driver.begin(), driver.end());
	const HWND window = CreateWindowW(windowClass.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, CW_USEDEFAULT,
									  CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hinstance, nullptr);
	ShowWindow(window, SW_SHOW);
	SetForegroundWindow(window);

	// Instance, surface, device
	const char* instanceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
	VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
	app.apiVersion = VK_API_VERSION_1_3;
	VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
	instanceInfo.pApplicationInfo = &app;
	instanceInfo.enabledExtensionCount = 2;
	instanceInfo.ppEnabledExtensionNames = instanceExtensions;
	VK_CALL(vk.I<PFN_vkCreateInstance>("vkCreateInstance")(&instanceInfo, nullptr, &vk.instance));

	uint32_t physicalCount = 1;
	VkPhysicalDevice physical = VK_NULL_HANDLE;
	vk.I<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices")(vk.instance, &physicalCount, &physical);
	VkPhysicalDeviceProperties properties{};
	vk.I<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties")(physical, &properties);
	std::printf("device: %s\n", properties.deviceName);

	VkWin32SurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
	surfaceInfo.hinstance = hinstance;
	surfaceInfo.hwnd = window;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VK_CALL(vk.I<PFN_vkCreateWin32SurfaceKHR>("vkCreateWin32SurfaceKHR")(vk.instance, &surfaceInfo, nullptr, &surface));
	VkBool32 supported = VK_FALSE;
	vk.I<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>("vkGetPhysicalDeviceSurfaceSupportKHR")(physical, 0, surface, &supported);
	VkSurfaceCapabilitiesKHR caps{};
	VK_CALL(vk.I<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>("vkGetPhysicalDeviceSurfaceCapabilitiesKHR")(physical, surface, &caps));
	std::printf("surface: supported=%u extent=%ux%u images %u..%u\n", supported, caps.currentExtent.width, caps.currentExtent.height,
				caps.minImageCount, caps.maxImageCount);

	const float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;
	const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledExtensionCount = 1;
	deviceInfo.ppEnabledExtensionNames = deviceExtensions;
	VK_CALL(vk.I<PFN_vkCreateDevice>("vkCreateDevice")(physical, &deviceInfo, nullptr, &vk.device));
	VkQueue queue = VK_NULL_HANDLE;
	vk.D<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(vk.device, 0, 0, &queue);

	// Swapchain
	const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
	const VkExtent2D extent = caps.currentExtent.width ? caps.currentExtent : VkExtent2D{kWidth, kHeight};
	VkSwapchainCreateInfoKHR swapchainInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
	swapchainInfo.surface = surface;
	swapchainInfo.minImageCount = caps.minImageCount;
	swapchainInfo.imageFormat = format;
	swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	swapchainInfo.imageExtent = extent;
	swapchainInfo.imageArrayLayers = 1;
	swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	swapchainInfo.clipped = VK_TRUE;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VK_CALL(vk.D<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR")(vk.device, &swapchainInfo, nullptr, &swapchain));
	uint32_t imageCount = 0;
	vk.D<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR")(vk.device, swapchain, &imageCount, nullptr);
	std::vector<VkImage> images(imageCount);
	vk.D<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR")(vk.device, swapchain, &imageCount, images.data());

	VkAttachmentDescription attachment{};
	attachment.format = format;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;
	VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &attachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	VkRenderPass renderPass = VK_NULL_HANDLE;
	VK_CALL(vk.D<PFN_vkCreateRenderPass>("vkCreateRenderPass")(vk.device, &renderPassInfo, nullptr, &renderPass));

	std::vector<VkImageView> views(imageCount);
	std::vector<VkFramebuffer> framebuffers(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		viewInfo.image = images[i];
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		VK_CALL(vk.D<PFN_vkCreateImageView>("vkCreateImageView")(vk.device, &viewInfo, nullptr, &views[i]));
		VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &views[i];
		framebufferInfo.width = extent.width;
		framebufferInfo.height = extent.height;
		framebufferInfo.layers = 1;
		VK_CALL(vk.D<PFN_vkCreateFramebuffer>("vkCreateFramebuffer")(vk.device, &framebufferInfo, nullptr, &framebuffers[i]));
	}

	// Pipeline: viewport and scissor dynamic
	VkShaderModule modules[2] = {};
	const std::pair<const uint32_t*, size_t> code[2] = {{kTriVertSpirv, sizeof(kTriVertSpirv)}, {kTriFragSpirv, sizeof(kTriFragSpirv)}};
	for (int i = 0; i < 2; ++i)
	{
		VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		moduleInfo.codeSize = code[i].second;
		moduleInfo.pCode = code[i].first;
		VK_CALL(vk.D<PFN_vkCreateShaderModule>("vkCreateShaderModule")(vk.device, &moduleInfo, nullptr, &modules[i]));
	}
	VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VK_CALL(vk.D<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(vk.device, &layoutInfo, nullptr, &pipelineLayout));
	VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = modules[0];
	stages[0].pName = "main";
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = modules[1];
	stages[1].pName = "main";
	VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
	VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;
	VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
	rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization.cullMode = VK_CULL_MODE_NONE;
	rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterization.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineColorBlendAttachmentState blendAttachment{};
	blendAttachment.colorWriteMask = 0xF;
	VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
	blend.attachmentCount = 1;
	blend.pAttachments = &blendAttachment;
	const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamicStates;
	VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = stages;
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterization;
	pipelineInfo.pMultisampleState = &multisample;
	pipelineInfo.pColorBlendState = &blend;
	pipelineInfo.pDynamicState = &dynamic;
	pipelineInfo.layout = pipelineLayout;
	pipelineInfo.renderPass = renderPass;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VK_CALL(vk.D<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));

	// Frame resources
	VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VkCommandPool pool = VK_NULL_HANDLE;
	VK_CALL(vk.D<PFN_vkCreateCommandPool>("vkCreateCommandPool")(vk.device, &poolInfo, nullptr, &pool));
	VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	allocateInfo.commandPool = pool;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocateInfo.commandBufferCount = 1;
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	VK_CALL(vk.D<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(vk.device, &allocateInfo, &cmd));
	VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	VkSemaphore imageAvailable = VK_NULL_HANDLE;
	VkSemaphore renderFinished = VK_NULL_HANDLE;
	VK_CALL(vk.D<PFN_vkCreateSemaphore>("vkCreateSemaphore")(vk.device, &semaphoreInfo, nullptr, &imageAvailable));
	VK_CALL(vk.D<PFN_vkCreateSemaphore>("vkCreateSemaphore")(vk.device, &semaphoreInfo, nullptr, &renderFinished));
	VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	VkFence fence = VK_NULL_HANDLE;
	VK_CALL(vk.D<PFN_vkCreateFence>("vkCreateFence")(vk.device, &fenceInfo, nullptr, &fence));

	const auto acquire = vk.D<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR");
	const auto present = vk.D<PFN_vkQueuePresentKHR>("vkQueuePresentKHR");
	const auto submit = vk.D<PFN_vkQueueSubmit>("vkQueueSubmit");
	const auto waitFences = vk.D<PFN_vkWaitForFences>("vkWaitForFences");
	const auto resetFences = vk.D<PFN_vkResetFences>("vkResetFences");
	const auto resetCommandBuffer = vk.D<PFN_vkResetCommandBuffer>("vkResetCommandBuffer");
	const auto beginCommandBuffer = vk.D<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer");
	const auto endCommandBuffer = vk.D<PFN_vkEndCommandBuffer>("vkEndCommandBuffer");
	const auto beginRenderPass = vk.D<PFN_vkCmdBeginRenderPass>("vkCmdBeginRenderPass");
	const auto endRenderPass = vk.D<PFN_vkCmdEndRenderPass>("vkCmdEndRenderPass");
	const auto bindPipeline = vk.D<PFN_vkCmdBindPipeline>("vkCmdBindPipeline");
	const auto setViewport = vk.D<PFN_vkCmdSetViewport>("vkCmdSetViewport");
	const auto setScissor = vk.D<PFN_vkCmdSetScissor>("vkCmdSetScissor");
	const auto draw = vk.D<PFN_vkCmdDraw>("vkCmdDraw");

	// The last frame is static (full-window viewport, fixed clear color) so it can be checked.
	const VkClearColorValue finalClear{{0.1f, 0.1f, 0.1f, 1.0f}};
	uint32_t frame = 0;
	for (; frame < kFrames && !g_closed; ++frame)
	{
		MSG message;
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}

		uint32_t imageIndex = 0;
		VK_CALL(acquire(vk.device, swapchain, UINT64_MAX, imageAvailable, VK_NULL_HANDLE, &imageIndex));

		const bool last = frame + 1 == kFrames;
		const float t = static_cast<float>(frame) / 30.0f;
		VkClearValue clear{};
		clear.color = last ? finalClear : VkClearColorValue{{0.1f + 0.1f * std::sin(t), 0.1f, 0.2f + 0.1f * std::cos(t), 1.0f}};
		const float w = static_cast<float>(extent.width);
		const float h = static_cast<float>(extent.height);
		const VkViewport viewport = last ? VkViewport{0, 0, w, h, 0, 1} : VkViewport{w * 0.25f * std::sin(t), h * 0.15f * std::cos(t * 1.3f), w, h, 0, 1};
		const VkRect2D scissor{{0, 0}, extent};

		resetCommandBuffer(cmd, 0);
		VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CALL(beginCommandBuffer(cmd, &beginInfo));
		VkRenderPassBeginInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
		passInfo.renderPass = renderPass;
		passInfo.framebuffer = framebuffers[imageIndex];
		passInfo.renderArea = scissor;
		passInfo.clearValueCount = 1;
		passInfo.pClearValues = &clear;
		beginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
		bindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		setViewport(cmd, 0, 1, &viewport);
		setScissor(cmd, 0, 1, &scissor);
		draw(cmd, 3, 1, 0, 0);
		endRenderPass(cmd);
		VK_CALL(endCommandBuffer(cmd));

		const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &imageAvailable;
		submitInfo.pWaitDstStageMask = &waitStage;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmd;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &renderFinished;
		if (const VkResult submitted = submit(queue, 1, &submitInfo, fence); submitted != VK_SUCCESS)
		{
			std::fprintf(stderr, "vkQueueSubmit failed at frame %u: VkResult %d\n", frame, submitted);
			return EXIT_FAILURE;
		}
		VK_CALL(waitFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX));
		VK_CALL(resetFences(vk.device, 1, &fence));

		VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &renderFinished;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain;
		presentInfo.pImageIndices = &imageIndex;
		const VkResult presented = present(queue, &presentInfo);
		if (presented != VK_SUCCESS && presented != VK_SUBOPTIMAL_KHR)
		{
			std::fprintf(stderr, "vkQueuePresentKHR failed: VkResult %d\n", presented);
			return EXIT_FAILURE;
		}
		Sleep(16);
	}

	// What reached the window: the triangle's color inside it, the clear color outside.
	int status = EXIT_SUCCESS;
	if (frame == kFrames)
	{
		const HDC dc = GetDC(window);
		const COLORREF inside = GetPixel(dc, static_cast<int>(extent.width / 2), static_cast<int>(extent.height / 2 + extent.height / 8));
		const COLORREF outside = GetPixel(dc, 4, 4);
		ReleaseDC(window, dc);
		std::printf("window pixels: triangle (%u, %u, %u), background (%u, %u, %u) after %u frames\n", GetRValue(inside), GetGValue(inside),
					GetBValue(inside), GetRValue(outside), GetGValue(outside), GetBValue(outside), frame);
		// Within one unit: UNORM rounding of 0.25/0.5/0.75 and 0.1 may differ between drivers.
		auto withinOne = [](COLORREF c, int r, int g, int b)
		{
			return std::abs(GetRValue(c) - r) <= 1 && std::abs(GetGValue(c) - g) <= 1 && std::abs(GetBValue(c) - b) <= 1;
		};
		const bool triangleOk = withinOne(inside, 64, 128, 191);
		const bool backgroundOk = withinOne(outside, 26, 26, 26);
		std::printf(triangleOk && backgroundOk ? "presented frame validated\n" : "presented frame does NOT match\n");
		status = triangleOk && backgroundOk ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	vk.D<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(vk.device);
	vk.D<PFN_vkDestroyFence>("vkDestroyFence")(vk.device, fence, nullptr);
	vk.D<PFN_vkDestroySemaphore>("vkDestroySemaphore")(vk.device, renderFinished, nullptr);
	vk.D<PFN_vkDestroySemaphore>("vkDestroySemaphore")(vk.device, imageAvailable, nullptr);
	vk.D<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(vk.device, pool, nullptr);
	vk.D<PFN_vkDestroyPipeline>("vkDestroyPipeline")(vk.device, pipeline, nullptr);
	vk.D<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(vk.device, pipelineLayout, nullptr);
	for (VkShaderModule module : modules)
		vk.D<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(vk.device, module, nullptr);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		vk.D<PFN_vkDestroyFramebuffer>("vkDestroyFramebuffer")(vk.device, framebuffers[i], nullptr);
		vk.D<PFN_vkDestroyImageView>("vkDestroyImageView")(vk.device, views[i], nullptr);
	}
	vk.D<PFN_vkDestroyRenderPass>("vkDestroyRenderPass")(vk.device, renderPass, nullptr);
	vk.D<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(vk.device, swapchain, nullptr);
	vk.D<PFN_vkDestroyDevice>("vkDestroyDevice")(vk.device, nullptr);
	vk.I<PFN_vkDestroySurfaceKHR>("vkDestroySurfaceKHR")(vk.instance, surface, nullptr);
	vk.I<PFN_vkDestroyInstance>("vkDestroyInstance")(vk.instance, nullptr);
	DestroyWindow(window);
	return status;
}
