/**
 * @file main.cpp
 * @brief Vulkan Hello Triangle with vulkan.hpp
 * @date 2025-11-18
 *
 * Hello Triangle example using Vulkan C++ bindings for the Vkd software driver.
 */

#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_ASSERT_ON_RESULT
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

#include <Concerto/Core/DynLib/DynLib.hpp>
#include <Concerto/Core/Logger/Logger.hpp>

#include <NZSL/Ast/Cloner.hpp>
#include <NZSL/Ast/ReflectVisitor.hpp>
#include <NZSL/Ast/Transformations/BindingResolverTransformer.hpp>
#include <NZSL/Ast/Transformations/ResolveTransformer.hpp>
#include <NZSL/Ast/Transformations/ValidationTransformer.hpp>
#include <NZSL/Ast/TransformerExecutor.hpp>
#include <NZSL/Parser.hpp>
#include <NZSL/SpirvWriter.hpp>
#include <vulkan/vulkan.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;

const char* vertexShaderCode = R"(
[nzsl_version("1.1")]
module;

const positions = array[vec2[f32]](
	vec2[f32](0.0, -0.5),
	vec2[f32](0.5, 0.5),
	vec2[f32](-0.5, 0.5)
);

const colors = array[vec3[f32]](
	vec3[f32](1.0, 0.0, 0.0),
	vec3[f32](0.0, 1.0, 0.0),
	vec3[f32](0.0, 0.0, 1.0)
);

struct VertInput
{
	[builtin(vertex_index)] vertexIndex: i32
}

struct VertOutput
{
	[builtin(position)] position: vec4[f32],
	[location(0)] color: vec3[f32]
}

[entry(vert)]
fn main(input: VertInput) -> VertOutput
{
	let output: VertOutput;
	output.position = vec4[f32](positions[input.vertexIndex], 0.0, 1.0);
	output.color = colors[input.vertexIndex];
	return output;
}
)";

const char* fragmentShaderCode = R"(
[nzsl_version("1.1")]
module;

struct FragInput
{
	[location(0)] color: vec3[f32]
}

struct FragOutput
{
	[location(0)] color: vec4[f32]
}

[entry(frag)]
fn main(input: FragInput) -> FragOutput
{
	let output: FragOutput;
	output.color = vec4[f32](input.color, 1.0);
	return output;
}
)";

std::vector<cct::UInt32> compileShaderToSPIRV(const char* source)
{
	try
	{
		nzsl::Ast::ModulePtr shaderModule = nzsl::Parse(source);
		if (!shaderModule)
		{
			cct::Logger::Error("Failed to parse NZSL shader");
			return {};
		}

		nzsl::Ast::ReflectVisitor reflectVisitor;
		nzsl::Ast::TransformerExecutor executor;
		executor.AddPass<nzsl::Ast::ResolveTransformer>();
		executor.AddPass<nzsl::Ast::BindingResolverTransformer>({.forceAutoBindingResolve = true});
		executor.AddPass<nzsl::Ast::ValidationTransformer>();

		nzsl::Ast::TransformerContext context;
		context.partialCompilation = true;

		nzsl::Ast::ModulePtr resolvedModule = nzsl::Ast::Clone(*shaderModule);
		executor.Transform(*resolvedModule, context);
		nzsl::Ast::ModulePtr sanitizedModule = resolvedModule;
		nzsl::Ast::ReflectVisitor::Callbacks callbacks;
		reflectVisitor.Reflect(*sanitizedModule, callbacks);

		nzsl::SpirvWriter spirvWriter;

		return spirvWriter.Generate(*sanitizedModule);
	}
	catch (const std::exception& e)
	{
		cct::Logger::Error("NZSL compilation error: {}", e.what());
		return {};
	}
}

void saveImageToPPM(const char* filename, uint32_t width, uint32_t height, const uint8_t* data)
{
	std::ofstream file(filename, std::ios::binary);
	file << "P6\n"
		 << width << " " << height << "\n255\n";
	for (uint32_t y = 0; y < height; ++y)
	{
		for (uint32_t x = 0; x < width; ++x)
		{
			size_t idx = (y * width + x) * 4;
			file.put(data[idx + 0]);
			file.put(data[idx + 1]);
			file.put(data[idx + 2]);
		}
	}
	cct::Logger::Info("Saved triangle to {}", filename);
}

int main()
{
	cct::Logger::Info("Vulkan Hello Triangle with vulkan.hpp");

	cct::DynLib driver;
	if (!driver.Load(
			"./"
#ifdef CCT_PLATFORM_POSIX
			"lib"
#endif
			"vkd-Software" CONCERTO_DYNLIB_EXTENSION))
	{
		cct::Logger::Error("Failed to load driver");
		return EXIT_FAILURE;
	}

	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
		reinterpret_cast<PFN_vkGetInstanceProcAddr>(driver.GetSymbol("vk_icdGetInstanceProcAddr"));
	if (!vkGetInstanceProcAddr)
	{
		cct::Logger::Error("Failed to get vkGetInstanceProcAddr");
		return EXIT_FAILURE;
	}

	VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

	VkDirectDriverLoadingInfoLUNARG directLoadingInfo{};
	directLoadingInfo.sType = VK_STRUCTURE_TYPE_DIRECT_DRIVER_LOADING_INFO_LUNARG;
	directLoadingInfo.pfnGetInstanceProcAddr =
		reinterpret_cast<PFN_vkGetInstanceProcAddrLUNARG>(vkGetInstanceProcAddr);

	VkDirectDriverLoadingListLUNARG directDriverList{};
	directDriverList.sType = VK_STRUCTURE_TYPE_DIRECT_DRIVER_LOADING_LIST_LUNARG;
	directDriverList.mode = VK_DIRECT_DRIVER_LOADING_MODE_EXCLUSIVE_LUNARG;
	directDriverList.driverCount = 1;
	directDriverList.pDrivers = &directLoadingInfo;

	vk::ApplicationInfo appInfo("Vulkan Hello Triangle", 1, "Vkd", 1, VK_API_VERSION_1_1);

	std::vector<const char*> extensions = {VK_LUNARG_DIRECT_DRIVER_LOADING_EXTENSION_NAME};

	vk::InstanceCreateInfo instanceInfo({}, &appInfo, {}, extensions);
	instanceInfo.pNext = &directDriverList;

	auto instanceResult = vk::createInstance(instanceInfo);
	if (instanceResult.result != vk::Result::eSuccess)
	{
		cct::Logger::Error("Failed to create instance");
		return EXIT_FAILURE;
	}

	vk::Instance instance = instanceResult.value;
	VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);

	auto physicalDevices = instance.enumeratePhysicalDevices().value;
	if (physicalDevices.empty())
	{
		cct::Logger::Error("No physical devices found");
		return EXIT_FAILURE;
	}

	vk::PhysicalDevice physicalDevice = physicalDevices[0];
	auto deviceProps = physicalDevice.getProperties();
	cct::Logger::Info("Using device: {}", deviceProps.deviceName.data());

	auto queueFamilyProps = physicalDevice.getQueueFamilyProperties();
	uint32_t graphicsQueueFamily = 0;
	for (uint32_t i = 0; i < queueFamilyProps.size(); ++i)
	{
		if (queueFamilyProps[i].queueFlags & vk::QueueFlagBits::eGraphics)
		{
			graphicsQueueFamily = i;
			break;
		}
	}

	float queuePriority = 1.0f;
	vk::DeviceQueueCreateInfo queueInfo({}, graphicsQueueFamily, 1, &queuePriority);

	vk::DeviceCreateInfo deviceInfo({}, queueInfo);

	auto deviceResult = physicalDevice.createDevice(deviceInfo);
	if (deviceResult.result != vk::Result::eSuccess)
	{
		cct::Logger::Error("Failed to create device");
		return EXIT_FAILURE;
	}

	vk::Device device = deviceResult.value;
	VULKAN_HPP_DEFAULT_DISPATCHER.init(device);

	vk::Queue queue = device.getQueue(graphicsQueueFamily, 0);

	vk::ImageCreateInfo imageInfo(
		{},
		vk::ImageType::e2D,
		vk::Format::eR8G8B8A8Unorm,
		vk::Extent3D(WIDTH, HEIGHT, 1),
		1,
		1,
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eLinear,
		vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
		vk::SharingMode::eExclusive);

	auto imageResult = device.createImage(imageInfo);
	vk::Image renderImage = imageResult.value;

	auto memReqs = device.getImageMemoryRequirements(renderImage);
	auto memProps = physicalDevice.getMemoryProperties();

	uint32_t memoryTypeIndex = 0;
	for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
	{
		if ((memReqs.memoryTypeBits & (1 << i)) &&
			(memProps.memoryTypes[i].propertyFlags &
			 (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)))
		{
			memoryTypeIndex = i;
			break;
		}
	}

	vk::MemoryAllocateInfo allocInfo(memReqs.size, memoryTypeIndex);
	auto memoryResult = device.allocateMemory(allocInfo);
	vk::DeviceMemory imageMemory = memoryResult.value;

	device.bindImageMemory(renderImage, imageMemory, 0);

	vk::ImageViewCreateInfo viewInfo(
		{},
		renderImage,
		vk::ImageViewType::e2D,
		vk::Format::eR8G8B8A8Unorm,
		{},
		vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

	auto viewResult = device.createImageView(viewInfo);
	vk::ImageView imageView = viewResult.value;

	vk::AttachmentDescription colorAttachment(
		{},
		vk::Format::eR8G8B8A8Unorm,
		vk::SampleCountFlagBits::e1,
		vk::AttachmentLoadOp::eClear,
		vk::AttachmentStoreOp::eStore,
		vk::AttachmentLoadOp::eDontCare,
		vk::AttachmentStoreOp::eDontCare,
		vk::ImageLayout::eUndefined,
		vk::ImageLayout::eTransferSrcOptimal);

	vk::AttachmentReference colorAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal);

	vk::SubpassDescription subpass(
		{}, vk::PipelineBindPoint::eGraphics, {}, colorAttachmentRef);

	vk::RenderPassCreateInfo renderPassInfo({}, colorAttachment, subpass);

	auto renderPassResult = device.createRenderPass(renderPassInfo);
	vk::RenderPass renderPass = renderPassResult.value;

	vk::FramebufferCreateInfo framebufferInfo(
		{}, renderPass, imageView, WIDTH, HEIGHT, 1);

	auto framebufferResult = device.createFramebuffer(framebufferInfo);
	vk::Framebuffer framebuffer = framebufferResult.value;

	cct::Logger::Info("Compiling vertex shader...");
	auto vertSpirv = compileShaderToSPIRV(vertexShaderCode);
	if (vertSpirv.empty())
	{
		cct::Logger::Error("Failed to compile vertex shader");
		return EXIT_FAILURE;
	}

	cct::Logger::Info("Compiling fragment shader...");
	auto fragSpirv = compileShaderToSPIRV(fragmentShaderCode);
	if (fragSpirv.empty())
	{
		cct::Logger::Error("Failed to compile fragment shader");
		return EXIT_FAILURE;
	}

	cct::Logger::Info("Creating vertex shader module...");
	vk::ShaderModuleCreateInfo vertShaderInfo({}, vertSpirv.size() * sizeof(uint32_t), vertSpirv.data());
	auto vertModuleResult = device.createShaderModule(vertShaderInfo);
	if (vertModuleResult.result != vk::Result::eSuccess)
	{
		cct::Logger::Error("Failed to create vertex shader module");
		return EXIT_FAILURE;
	}
	vk::ShaderModule vertShaderModule = vertModuleResult.value;

	cct::Logger::Info("Creating fragment shader module...");
	vk::ShaderModuleCreateInfo fragShaderInfo({}, fragSpirv.size() * sizeof(uint32_t), fragSpirv.data());
	auto fragModuleResult = device.createShaderModule(fragShaderInfo);
	if (fragModuleResult.result != vk::Result::eSuccess)
	{
		cct::Logger::Error("Failed to create fragment shader module");
		return EXIT_FAILURE;
	}
	vk::ShaderModule fragShaderModule = fragModuleResult.value;

	vk::PipelineShaderStageCreateInfo vertStageInfo(
		{}, vk::ShaderStageFlagBits::eVertex, vertShaderModule, "main");
	vk::PipelineShaderStageCreateInfo fragStageInfo(
		{}, vk::ShaderStageFlagBits::eFragment, fragShaderModule, "main");

	std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {vertStageInfo, fragStageInfo};

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo;

	vk::PipelineInputAssemblyStateCreateInfo inputAssembly(
		{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE);

	vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 0.0f, 1.0f);
	vk::Rect2D scissor({0, 0}, {WIDTH, HEIGHT});

	vk::PipelineViewportStateCreateInfo viewportState({}, viewport, scissor);

	vk::PipelineRasterizationStateCreateInfo rasterizer(
		{},
		VK_FALSE,
		VK_FALSE,
		vk::PolygonMode::eFill,
		vk::CullModeFlagBits::eBack,
		vk::FrontFace::eClockwise,
		VK_FALSE,
		0.0f,
		0.0f,
		0.0f,
		1.0f);

	vk::PipelineMultisampleStateCreateInfo multisampling(
		{}, vk::SampleCountFlagBits::e1, VK_FALSE);

	vk::PipelineColorBlendAttachmentState colorBlendAttachment(
		VK_FALSE,
		vk::BlendFactor::eOne,
		vk::BlendFactor::eZero,
		vk::BlendOp::eAdd,
		vk::BlendFactor::eOne,
		vk::BlendFactor::eZero,
		vk::BlendOp::eAdd,
		vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

	vk::PipelineColorBlendStateCreateInfo colorBlending(
		{}, VK_FALSE, vk::LogicOp::eCopy, colorBlendAttachment);

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
	auto layoutResult = device.createPipelineLayout(pipelineLayoutInfo);
	vk::PipelineLayout pipelineLayout = layoutResult.value;

	vk::GraphicsPipelineCreateInfo pipelineInfo(
		{},
		shaderStages,
		&vertexInputInfo,
		&inputAssembly,
		nullptr,
		&viewportState,
		&rasterizer,
		&multisampling,
		nullptr,
		&colorBlending,
		nullptr,
		pipelineLayout,
		renderPass,
		0);

	auto pipelineResult = device.createGraphicsPipeline({}, pipelineInfo);
	vk::Pipeline graphicsPipeline = pipelineResult.value;

	vk::CommandPoolCreateInfo poolInfo({}, graphicsQueueFamily);
	auto poolResult = device.createCommandPool(poolInfo);
	vk::CommandPool commandPool = poolResult.value;

	vk::CommandBufferAllocateInfo allocInfoCmd(commandPool, vk::CommandBufferLevel::ePrimary, 1);
	auto cmdBuffers = device.allocateCommandBuffers(allocInfoCmd).value;
	vk::CommandBuffer commandBuffer = cmdBuffers[0];

	vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	commandBuffer.begin(beginInfo);

	vk::ClearValue clearColor(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
	vk::RenderPassBeginInfo renderPassBeginInfo(
		renderPass, framebuffer, vk::Rect2D({0, 0}, {WIDTH, HEIGHT}), clearColor);

	commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
	commandBuffer.draw(3, 1, 0, 0);
	commandBuffer.endRenderPass();

	commandBuffer.end();

	vk::SubmitInfo submitInfo({}, {}, commandBuffer);
	queue.submit(submitInfo);
	queue.waitIdle();

	cct::Logger::Info("Triangle rendered successfully!");

	auto mappedMemory = device.mapMemory(imageMemory, 0, memReqs.size);
	if (mappedMemory.result == vk::Result::eSuccess)
	{
		saveImageToPPM("triangle.ppm", WIDTH, HEIGHT, static_cast<const uint8_t*>(mappedMemory.value));
		device.unmapMemory(imageMemory);
	}

	device.destroyPipeline(graphicsPipeline);
	device.destroyPipelineLayout(pipelineLayout);
	device.destroyShaderModule(fragShaderModule);
	device.destroyShaderModule(vertShaderModule);
	device.destroyFramebuffer(framebuffer);
	device.destroyRenderPass(renderPass);
	device.destroyImageView(imageView);
	device.destroyImage(renderImage);
	device.freeMemory(imageMemory);
	device.destroyCommandPool(commandPool);
	device.destroy();
	instance.destroy();

	cct::Logger::Info("Vulkan Hello Triangle completed!");
	return 0;
}
