#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm
{
	class Module;
	class TargetMachine;
} // namespace llvm

namespace amdcomp::backend
{
	inline constexpr const char* kTriple = "amdgcn--amdpal";
	inline constexpr const char* kCpu = "gfx1151";

	inline constexpr uint32_t kComputeDispatchInitiator = 0x2E00;
	inline constexpr uint32_t kComputePgmRsrc1 = 0x2E12;
	inline constexpr uint32_t kComputePgmRsrc2 = 0x2E13;

	struct Output
	{
		std::vector<uint32_t> code;
		std::map<uint32_t, uint32_t> registers;
		uint64_t scratchSize = 0;
		std::string metadata;
	};

	std::unique_ptr<llvm::TargetMachine> CreateTargetMachine(bool wave64, std::string& error);

	std::optional<Output> Generate(llvm::Module& module, llvm::TargetMachine& tm, std::string& error, std::string* optimizedIr = nullptr);
} // namespace amdcomp::backend
