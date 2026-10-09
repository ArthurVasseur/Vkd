#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace amdcomp
{
	inline constexpr uint32_t kMaxDescriptorSets = 8;

	struct SpecializationConstant
	{
		uint32_t id;
		uint64_t value;
		uint32_t size;
	};

	enum class DescriptorKind
	{
		UniformBuffer,
		StorageBuffer,
		SampledImage,
		StorageImage,
		Sampler,
		CombinedImageSampler
	};

	struct Descriptor
	{
		uint32_t set;
		uint32_t binding;
		DescriptorKind kind;
		uint32_t offset;
		uint32_t count;
	};

	struct ComputeShader
	{
		std::vector<uint32_t> code;
		uint32_t pgmRsrc1 = 0;
		uint32_t pgmRsrc2 = 0;
		uint32_t pgmRsrc3 = 0;
		uint32_t numThreadX = 1;
		uint32_t numThreadY = 1;
		uint32_t numThreadZ = 1;
		uint32_t dispatchInitiator = 0;
		uint32_t pushConstantUserData = 0;
		uint32_t pushConstantDwords = 0;
		uint32_t setTableUserData[kMaxDescriptorSets] = {};
		std::vector<Descriptor> descriptors;
		uint32_t numWorkgroupsUserData = 0;
	};

	struct CompileOptions
	{
		std::string_view entryPoint = "main";
		std::span<const SpecializationConstant> specialization = {};
		std::string* llvmIr = nullptr;
		std::string* optimizedLlvmIr = nullptr;
	};

	std::optional<ComputeShader> CompileComputeShader(std::span<const uint32_t> spirv, const CompileOptions& options, std::string& error);

	enum class ColorExportFormat
	{
		Float16,
		Float32
	};

	inline constexpr uint8_t kVertexSwizzleZero = 4;
	inline constexpr uint8_t kVertexSwizzleOne = 5;

	struct VertexAttribute
	{
		uint32_t location = 0;
		uint32_t binding = 0;
		uint32_t offset = 0;
		uint32_t format = 0;
		uint32_t channels = 4;
		uint8_t swizzle[4] = {0, 1, 2, 3};
		bool perInstance = false;
	};

	struct GraphicsStageCode
	{
		std::vector<uint32_t> code;
		uint32_t pgmRsrc1 = 0;
		uint32_t pgmRsrc2 = 0;
	};

	struct GraphicsShaders
	{
		GraphicsStageCode vertex;
		GraphicsStageCode fragment;
		bool instanceIndex = false;
		uint32_t vertexBufferTableUserData = 0;
		uint32_t baseVertexUserData = 0;
		uint32_t baseInstanceUserData = 0;
		uint32_t vertexSetTableUserData[kMaxDescriptorSets] = {};
		uint32_t vertexPushConstantUserData = 0;
		uint32_t vertexPushConstantDwords = 0;
		uint32_t vsOutConfig = 0;
		uint32_t numInterpolants = 0;
		std::vector<uint32_t> psInputCntl;
		uint32_t psInputEna = 0;
		uint32_t psInputAddr = 0;
		uint32_t colorFormat = 0;
		uint32_t shaderMask = 0;
		uint32_t fragmentSetTableUserData[kMaxDescriptorSets] = {};
		uint32_t fragmentPushConstantUserData = 0;
		uint32_t fragmentPushConstantDwords = 0;
		std::vector<Descriptor> descriptors;
	};

	struct GraphicsOptions
	{
		CompileOptions vertex;
		CompileOptions fragment;
		std::span<const ColorExportFormat> colorExports = {};
		std::span<const VertexAttribute> vertexAttributes = {};
	};

	std::optional<GraphicsShaders> CompileGraphicsShaders(std::span<const uint32_t> vertexSpirv, std::span<const uint32_t> fragmentSpirv,
														  const GraphicsOptions& options, std::string& error);

	std::string Disassemble(std::span<const uint32_t> code, bool wave64);
} // namespace amdcomp
