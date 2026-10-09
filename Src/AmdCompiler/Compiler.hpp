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
		StorageBuffer
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
	};

	struct CompileOptions
	{
		std::string_view entryPoint = "main";
		std::span<const SpecializationConstant> specialization = {};
		std::string* llvmIr = nullptr;
	};

	std::optional<ComputeShader> CompileComputeShader(std::span<const uint32_t> spirv, const CompileOptions& options, std::string& error);

	std::string Disassemble(std::span<const uint32_t> code, bool wave64);
} // namespace amdcomp
