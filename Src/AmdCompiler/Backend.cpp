#include "AmdCompiler/Backend.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>

#include <llvm/BinaryFormat/ELF.h>
#include <llvm/BinaryFormat/MsgPackDocument.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/DiagnosticPrinter.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Object/ELFObjectFile.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

namespace amdcomp::backend
{
	namespace
	{
		void InitializeAmdgpuTarget()
		{
			static std::once_flag once;
			std::call_once(once, []
						   {
				LLVMInitializeAMDGPUTargetInfo();
				LLVMInitializeAMDGPUTarget();
				LLVMInitializeAMDGPUTargetMC();
				LLVMInitializeAMDGPUAsmPrinter();
				LLVMInitializeAMDGPUDisassembler(); });
		}

		struct Diagnostics
		{
			std::string errors;
		};

		void OnDiagnostic(const llvm::DiagnosticInfo* info, void* context)
		{
			if (info->getSeverity() != llvm::DS_Error)
				return;
			auto& diagnostics = *static_cast<Diagnostics*>(context);
			llvm::raw_string_ostream os(diagnostics.errors);
			llvm::DiagnosticPrinterRawOStream printer(os);
			info->print(printer);
			os << '\n';
		}

		bool ReadPalMetadata(llvm::ArrayRef<uint8_t> blob, Output& output, std::string& error)
		{
			llvm::msgpack::Document document;
			if (!document.readFromBlob(llvm::StringRef(reinterpret_cast<const char*>(blob.data()), blob.size()), false))
			{
				error = "unreadable PAL metadata";
				return false;
			}
			{
				llvm::raw_string_ostream os(output.metadata);
				document.toYAML(os);
			}

			llvm::msgpack::DocNode& root = document.getRoot();
			if (!root.isMap())
				return true;
			auto pipelines = root.getMap().find("amdpal.pipelines");
			if (pipelines == root.getMap().end() || !pipelines->second.isArray() || pipelines->second.getArray().size() == 0)
				return true;
			llvm::msgpack::DocNode& pipeline = pipelines->second.getArray()[0];
			if (!pipeline.isMap())
				return true;

			auto registers = pipeline.getMap().find(".registers");
			if (registers != pipeline.getMap().end() && registers->second.isMap())
			{
				for (auto& [key, value] : registers->second.getMap())
				{
					if (key.getKind() == llvm::msgpack::Type::UInt && value.getKind() == llvm::msgpack::Type::UInt)
						output.registers[static_cast<uint32_t>(key.getUInt())] = static_cast<uint32_t>(value.getUInt());
				}
			}

			auto stages = pipeline.getMap().find(".hardware_stages");
			if (stages != pipeline.getMap().end() && stages->second.isMap())
			{
				for (auto& [stageName, stage] : stages->second.getMap())
				{
					if (!stage.isMap())
						continue;
					auto scratch = stage.getMap().find(".scratch_memory_size");
					if (scratch != stage.getMap().end() && scratch->second.getKind() == llvm::msgpack::Type::UInt)
						output.scratchSize = std::max<uint64_t>(output.scratchSize, scratch->second.getUInt());
				}
			}
			return true;
		}

		bool ReadObject(llvm::StringRef object, Output& output, std::string& error)
		{
			auto file = llvm::object::ObjectFile::createELFObjectFile(llvm::MemoryBufferRef(object, "shader"));
			if (!file)
			{
				error = "unreadable ELF: " + llvm::toString(file.takeError());
				return false;
			}
			const auto* elf = llvm::dyn_cast<llvm::object::ELF64LEObjectFile>(file->get());
			if (!elf)
			{
				error = "unexpected ELF class";
				return false;
			}

			bool foundText = false;
			for (const llvm::object::SectionRef& section : elf->sections())
			{
				llvm::Expected<llvm::StringRef> name = section.getName();
				if (!name)
				{
					llvm::consumeError(name.takeError());
					continue;
				}
				if (*name == ".text")
				{
					if (section.relocations().begin() != section.relocations().end())
					{
						error = "the code has relocations (constant data or calls), not supported yet";
						return false;
					}
					llvm::Expected<llvm::StringRef> contents = section.getContents();
					if (!contents)
					{
						error = llvm::toString(contents.takeError());
						return false;
					}
					output.code.resize((contents->size() + 3) / 4);
					std::memcpy(output.code.data(), contents->data(), contents->size());
					foundText = true;
				}
				else if (section.isData() && section.getSize() > 0 && name->starts_with(".rodata"))
				{
					error = "the code reads constant data (" + name->str() + "), not supported yet";
					return false;
				}
			}
			if (!foundText)
			{
				error = "no .text in the generated object";
				return false;
			}

			const auto& elfFile = elf->getELFFile();
			auto sections = elfFile.sections();
			if (!sections)
			{
				error = llvm::toString(sections.takeError());
				return false;
			}
			for (const auto& header : *sections)
			{
				if (header.sh_type != llvm::ELF::SHT_NOTE)
					continue;
				llvm::Error noteError = llvm::Error::success();
				for (const auto& note : elfFile.notes(header, noteError))
				{
					if (note.getName() == "AMDGPU" && note.getType() == llvm::ELF::NT_AMDGPU_METADATA)
					{
						if (!ReadPalMetadata(note.getDesc(header.sh_addralign), output, error))
							return false;
					}
				}
				if (noteError)
				{
					error = llvm::toString(std::move(noteError));
					return false;
				}
			}
			return true;
		}
	} // namespace

	std::unique_ptr<llvm::TargetMachine> CreateTargetMachine(bool wave64, std::string& error)
	{
		InitializeAmdgpuTarget();
		const llvm::Target* target = llvm::TargetRegistry::lookupTarget(kTriple, error);
		if (!target)
			return nullptr;
		llvm::TargetOptions options;
		return std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(kTriple, kCpu, wave64 ? "+wavefrontsize64" : "+wavefrontsize32", options,
																				std::nullopt, std::nullopt, llvm::CodeGenOptLevel::Default));
	}

	std::optional<Output> Generate(llvm::Module& module, llvm::TargetMachine& tm, std::string& error)
	{
		Diagnostics diagnostics;
		module.getContext().setDiagnosticHandlerCallBack(OnDiagnostic, &diagnostics);

		{
			llvm::raw_string_ostream os(error);
			if (llvm::verifyModule(module, &os))
			{
				error = "invalid LLVM IR: " + error;
				return std::nullopt;
			}
		}

		{
			llvm::LoopAnalysisManager lam;
			llvm::FunctionAnalysisManager fam;
			llvm::CGSCCAnalysisManager cgam;
			llvm::ModuleAnalysisManager mam;
			llvm::PassBuilder builder(&tm);
			builder.registerModuleAnalyses(mam);
			builder.registerCGSCCAnalyses(cgam);
			builder.registerFunctionAnalyses(fam);
			builder.registerLoopAnalyses(lam);
			builder.crossRegisterProxies(lam, fam, cgam, mam);
			llvm::ModulePassManager passes = builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
			passes.run(module, mam);
		}

		llvm::SmallVector<char, 0> object;
		{
			llvm::raw_svector_ostream os(object);
			llvm::legacy::PassManager passes;
			if (tm.addPassesToEmitFile(passes, os, nullptr, llvm::CodeGenFileType::ObjectFile))
			{
				error = "the AMDGPU target can't emit an object file";
				return std::nullopt;
			}
			passes.run(module);
		}
		if (!diagnostics.errors.empty())
		{
			error = diagnostics.errors;
			return std::nullopt;
		}

		Output output;
		if (!ReadObject(llvm::StringRef(object.data(), object.size()), output, error))
			return std::nullopt;
		return output;
	}
} // namespace amdcomp::backend
