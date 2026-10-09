#include <format>

#include "AmdCompiler/Backend.hpp"
#include "AmdCompiler/Compiler.hpp"
#include <llvm/MC/MCAsmInfo.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCDisassembler/MCDisassembler.h>
#include <llvm/MC/MCInst.h>
#include <llvm/MC/MCInstPrinter.h>
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCRegisterInfo.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/MC/MCTargetOptions.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>

namespace amdcomp
{
	std::string Disassemble(std::span<const uint32_t> code, bool wave64)
	{
		std::string error;
		if (!backend::CreateTargetMachine(wave64, error))
			return error;
		const llvm::Triple triple(backend::kTriple);
		const llvm::Target* target = llvm::TargetRegistry::lookupTarget(backend::kTriple, error);
		if (!target)
			return error;

		std::unique_ptr<llvm::MCRegisterInfo> registers(target->createMCRegInfo(backend::kTriple));
		llvm::MCTargetOptions options;
		std::unique_ptr<llvm::MCAsmInfo> asmInfo(target->createMCAsmInfo(*registers, backend::kTriple, options));
		std::unique_ptr<llvm::MCSubtargetInfo> subtarget(
			target->createMCSubtargetInfo(backend::kTriple, backend::kCpu, wave64 ? "+wavefrontsize64" : "+wavefrontsize32"));
		std::unique_ptr<llvm::MCInstrInfo> instructions(target->createMCInstrInfo());
		llvm::MCContext context(triple, asmInfo.get(), registers.get(), subtarget.get());
		std::unique_ptr<llvm::MCDisassembler> disassembler(target->createMCDisassembler(*subtarget, context));
		std::unique_ptr<llvm::MCInstPrinter> printer(
			target->createMCInstPrinter(triple, asmInfo->getAssemblerDialect(), *asmInfo, *instructions, *registers));
		if (!disassembler || !printer)
			return "no AMDGPU disassembler";

		const llvm::ArrayRef<uint8_t> bytes(reinterpret_cast<const uint8_t*>(code.data()), code.size_bytes());
		std::string text;
		llvm::raw_string_ostream os(text);
		uint64_t offset = 0;
		while (offset < bytes.size())
		{
			llvm::MCInst instruction;
			uint64_t size = 0;
			const auto status = disassembler->getInstruction(instruction, size, bytes.slice(offset), offset, llvm::nulls());
			os << std::format("{:04x}:", offset);
			if (status != llvm::MCDisassembler::Success || size == 0)
			{
				os << std::format(" .long 0x{:08x}\n", code[offset / 4]);
				offset += 4;
				continue;
			}
			printer->printInst(&instruction, offset, "", *subtarget, os);
			os << '\n';
			offset += size;
		}
		return text;
	}
} // namespace amdcomp
