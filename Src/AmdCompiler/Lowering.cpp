#include "AmdCompiler/Lowering.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "See/Spirv/Module.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicsAMDGPU.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>

#define SPV_ENABLE_UTILITY_CODE
#include <spirv/unified1/GLSL.std.450.h>
#include <spirv/unified1/spirv.h>

namespace amdcomp
{
	namespace
	{
		namespace spirv = see::spirv;

		constexpr unsigned kGlobalAddressSpace = 1;
		constexpr unsigned kLdsAddressSpace = 3;
		constexpr unsigned kConstantAddressSpace = 4;
		constexpr unsigned kPrivateAddressSpace = 5;
		constexpr unsigned kBufferFatPointerAddressSpace = 7;
		constexpr unsigned kBufferResourceAddressSpace = 8;
		constexpr uint32_t kBufferDescriptorBytes = 16;

		struct Type
		{
			enum class Kind
			{
				Void,
				Bool,
				Int,
				Float,
				Vector,
				Matrix,
				Array,
				RuntimeArray,
				Struct,
				Pointer,
				Function,
				Image,
				Sampler,
				SampledImage,
				Opaque
			};

			Kind kind = Kind::Opaque;
			uint32_t width = 0;
			bool isSigned = false;
			uint32_t element = 0;
			uint32_t count = 0;
			uint32_t storageClass = 0;
			std::vector<uint32_t> members;
			uint32_t dim = 0;
			bool arrayed = false;
			bool multisampled = false;
			uint32_t sampled = 0;
			bool depth = false;
		};

		struct Layout
		{
			uint64_t size = 0;
			uint32_t align = 1;
		};

		struct PointerInfo
		{
			uint32_t matrixStride = 0;
			bool rowMajor = false;
			uint32_t componentStride = 0;
		};

		struct InvocationVariable
		{
			uint32_t id;
			uint32_t pointee;
			uint32_t storageClass;
			uint32_t initializer;
		};

		struct DescriptorVariable
		{
			uint32_t set;
			uint32_t binding;
			DescriptorKind kind;
			uint32_t block;
			uint32_t count;
			bool isArray;
			uint32_t offset = 0;
		};

		struct PendingPhi
		{
			llvm::PHINode* phi;
			const spirv::Instruction* instruction;
		};

		std::span<const uint32_t> Args(const spirv::Instruction& instruction)
		{
			const size_t skip = (instruction.m_typeId != 0 ? 1 : 0) + (instruction.m_resultId != 0 ? 1 : 0);
			return std::span<const uint32_t>(instruction.m_operands).subspan(std::min(skip, instruction.m_operands.size()));
		}

		std::string LiteralString(std::span<const uint32_t> words)
		{
			std::string result;
			for (const uint32_t word : words)
			{
				for (int i = 0; i < 4; ++i)
				{
					const char c = static_cast<char>((word >> (8 * i)) & 0xFF);
					if (c == '\0')
						return result;
					result.push_back(c);
				}
			}
			return result;
		}

		uint64_t AlignUp(uint64_t value, uint64_t align)
		{
			return (value + align - 1) / align * align;
		}

		class Lowering
		{
		public:
			Lowering(const spirv::Module& spirv, const CompileOptions& options, llvm::LLVMContext& context, llvm::TargetMachine& tm) :
				m_spirv(spirv),
				m_options(options),
				m_context(context),
				m_module(std::make_unique<llvm::Module>("spirv", context)),
				m_builder(context)
			{
				m_module->setTargetTriple(tm.getTargetTriple().str());
				m_module->setDataLayout(tm.createDataLayout());
			}

			std::unique_ptr<llvm::Module> LowerCompute(ComputeLayout& layout, std::string& error)
			{
				if (!RunCompute(layout))
				{
					error = m_error.empty() ? "SPIR-V lowering failed" : m_error;
					return nullptr;
				}
				return std::move(m_module);
			}

			std::unique_ptr<llvm::Module> LowerVertex(std::span<const Descriptor> descriptorLayout, std::span<const VertexAttribute> attributes, VertexLayout& layout,
													  std::string& error)
			{
				if (!Prepare(SpvExecutionModelVertex, descriptorLayout, layout.descriptors) || !CreateVertexEntry(attributes, layout))
				{
					error = m_error.empty() ? "SPIR-V lowering failed" : m_error;
					return nullptr;
				}
				return std::move(m_module);
			}

			std::unique_ptr<llvm::Module> LowerFragment(std::span<const Descriptor> descriptorLayout, std::span<const ColorExportFormat> colorExports,
														FragmentLayout& layout, std::string& error)
			{
				m_fragmentStage = true;
				if (!Prepare(SpvExecutionModelFragment, descriptorLayout, layout.descriptors) || !CreateFragmentEntry(colorExports, layout))
				{
					error = m_error.empty() ? "SPIR-V lowering failed" : m_error;
					return nullptr;
				}
				return std::move(m_module);
			}

			bool ScanDescriptors(std::vector<Descriptor>& descriptors, std::string& error)
			{
				if (!LowerGlobals())
				{
					error = m_error;
					return false;
				}
				for (const auto& [id, variable] : m_descriptorVariables)
					descriptors.push_back(Descriptor{variable.set, variable.binding, variable.kind, 0, variable.count});
				return true;
			}

		private:
			bool Fail(std::string message)
			{
				if (m_error.empty())
					m_error = std::move(message);
				return false;
			}

			bool Unsupported(const spirv::Instruction& instruction)
			{
				return Fail(std::format("unsupported SPIR-V instruction {}", SpvOpToString(static_cast<SpvOp>(instruction.m_opcode))));
			}

			bool Prepare(uint32_t executionModel, std::span<const Descriptor> descriptorLayout, std::vector<Descriptor>& descriptors)
			{
				for (const spirv::EntryPoint& candidate : m_spirv.m_entryPoints)
				{
					if (candidate.m_name == m_options.entryPoint && candidate.m_executionModel == executionModel)
						m_entryPoint = &candidate;
				}
				if (!m_entryPoint)
					return Fail(std::format("no {} entry point named {}", SpvExecutionModelToString(static_cast<SpvExecutionModel>(executionModel)),
											m_options.entryPoint));
				m_entryFunctionId = m_entryPoint->m_functionId;
				m_entryInterface = m_entryPoint->m_interfaceIds;

				if (!LowerGlobals() || !AssignDescriptorOffsets(descriptorLayout, descriptors) || !DeclareFunctions())
					return false;
				if (m_constantFolder)
				{
					m_constantFolder->eraseFromParent();
					m_constantFolder = nullptr;
				}
				for (const spirv::Function& function : m_spirv.m_functions)
				{
					if (!LowerFunction(function))
						return false;
				}
				return true;
			}

			bool RunCompute(ComputeLayout& layout)
			{
				return Prepare(SpvExecutionModelGLCompute, {}, layout.descriptors) && ReadLocalSize(*m_entryPoint, layout.localSize) &&
					   CreateComputeEntry(layout);
			}

			bool InEntryInterface(uint32_t id) const
			{
				return std::find(m_entryInterface.begin(), m_entryInterface.end(), id) != m_entryInterface.end();
			}

			std::optional<uint32_t> Decoration(uint32_t id, uint32_t kind) const
			{
				auto it = m_spirv.m_decorations.find(id);
				if (it == m_spirv.m_decorations.end())
					return std::nullopt;
				for (const spirv::Decoration& decoration : it->second)
				{
					if (decoration.m_kind == kind)
						return decoration.m_literals.empty() ? 0 : decoration.m_literals[0];
				}
				return std::nullopt;
			}

			std::optional<uint32_t> MemberDecoration(uint32_t structId, uint32_t member, uint32_t kind) const
			{
				auto it = m_spirv.m_memberDecorations.find(structId);
				if (it == m_spirv.m_memberDecorations.end())
					return std::nullopt;
				for (const spirv::MemberDecoration& decoration : it->second)
				{
					if (decoration.m_member == member && decoration.m_kind == kind)
						return decoration.m_literals.empty() ? 0 : decoration.m_literals[0];
				}
				return std::nullopt;
			}

			const Type* TypeOf(uint32_t typeId)
			{
				auto it = m_types.find(typeId);
				if (it == m_types.end())
				{
					Fail(std::format("unknown type %{}", typeId));
					return nullptr;
				}
				return &it->second;
			}

			std::optional<unsigned> AddressSpace(uint32_t storageClass)
			{
				switch (storageClass)
				{
					case SpvStorageClassFunction:
					case SpvStorageClassPrivate:
					case SpvStorageClassInput:
					case SpvStorageClassOutput:
					case SpvStorageClassPushConstant:
						return kPrivateAddressSpace;
					case SpvStorageClassWorkgroup:
						return kLdsAddressSpace;
					case SpvStorageClassPhysicalStorageBuffer:
					case SpvStorageClassCrossWorkgroup:
						return kGlobalAddressSpace;
					case SpvStorageClassUniform:
					case SpvStorageClassStorageBuffer:
						return kBufferFatPointerAddressSpace;
					case SpvStorageClassUniformConstant:
						return kConstantAddressSpace;
					default:
						Fail(std::format("storage class {} not supported yet", SpvStorageClassToString(static_cast<SpvStorageClass>(storageClass))));
						return std::nullopt;
				}
			}

			llvm::Type* ValueType(uint32_t typeId)
			{
				if (auto cached = m_llvmTypes.find(typeId); cached != m_llvmTypes.end())
					return cached->second;
				const Type* type = TypeOf(typeId);
				if (!type)
					return nullptr;

				llvm::Type* result = nullptr;
				switch (type->kind)
				{
					case Type::Kind::Void:
						result = m_builder.getVoidTy();
						break;
					case Type::Kind::Bool:
						result = m_builder.getInt1Ty();
						break;
					case Type::Kind::Int:
						result = m_builder.getIntNTy(type->width);
						break;
					case Type::Kind::Float:
						result = type->width == 16 ? m_builder.getHalfTy() : type->width == 64 ? m_builder.getDoubleTy()
																							   : m_builder.getFloatTy();
						break;
					case Type::Kind::Vector:
					{
						llvm::Type* element = ValueType(type->element);
						if (!element)
							return nullptr;
						result = llvm::FixedVectorType::get(element, type->count);
						break;
					}
					case Type::Kind::Matrix:
					case Type::Kind::Array:
					{
						llvm::Type* element = ValueType(type->element);
						if (!element)
							return nullptr;
						result = llvm::ArrayType::get(element, type->count);
						break;
					}
					case Type::Kind::Struct:
					{
						std::vector<llvm::Type*> members;
						for (const uint32_t member : type->members)
						{
							llvm::Type* memberType = ValueType(member);
							if (!memberType)
								return nullptr;
							members.push_back(memberType);
						}
						result = llvm::StructType::get(m_context, members);
						break;
					}
					case Type::Kind::Image:
						result = llvm::FixedVectorType::get(m_builder.getInt32Ty(), 8);
						break;
					case Type::Kind::Sampler:
						result = llvm::FixedVectorType::get(m_builder.getInt32Ty(), 4);
						break;
					case Type::Kind::SampledImage:
						result = llvm::StructType::get(m_context,
													   {llvm::FixedVectorType::get(m_builder.getInt32Ty(), 8), llvm::FixedVectorType::get(m_builder.getInt32Ty(), 4)});
						break;
					case Type::Kind::Pointer:
					{
						const std::optional<unsigned> addressSpace = AddressSpace(type->storageClass);
						if (!addressSpace)
							return nullptr;
						result = llvm::PointerType::get(m_context, *addressSpace);
						break;
					}
					default:
						Fail(std::format("type %{} has no value representation", typeId));
						return nullptr;
				}
				m_llvmTypes[typeId] = result;
				return result;
			}

			std::optional<Layout> LayoutOf(uint32_t typeId)
			{
				const Type* type = TypeOf(typeId);
				if (!type)
					return std::nullopt;
				switch (type->kind)
				{
					case Type::Kind::Bool:
						return Layout{4, 4};
					case Type::Kind::Int:
					case Type::Kind::Float:
						return Layout{type->width / 8, type->width / 8};
					case Type::Kind::Pointer:
						return Layout{8, 8};
					case Type::Kind::Image:
						return Layout{32, 16};
					case Type::Kind::Sampler:
						return Layout{16, 16};
					case Type::Kind::SampledImage:
						return Layout{48, 16};
					case Type::Kind::Vector:
					{
						const std::optional<Layout> element = LayoutOf(type->element);
						if (!element)
							return std::nullopt;
						return Layout{element->size * type->count, element->align * (type->count == 3 ? 4 : type->count)};
					}
					case Type::Kind::Matrix:
					{
						const std::optional<Layout> column = LayoutOf(type->element);
						if (!column)
							return std::nullopt;
						return Layout{AlignUp(column->size, column->align) * type->count, column->align};
					}
					case Type::Kind::Array:
					case Type::Kind::RuntimeArray:
					{
						const std::optional<Layout> element = LayoutOf(type->element);
						const std::optional<uint64_t> stride = ArrayStride(typeId);
						if (!element || !stride)
							return std::nullopt;
						return Layout{type->kind == Type::Kind::Array ? *stride * type->count : 0, element->align};
					}
					case Type::Kind::Struct:
					{
						Layout layout;
						for (uint32_t i = 0; i < type->members.size(); ++i)
						{
							const std::optional<Layout> member = LayoutOf(type->members[i]);
							const std::optional<uint64_t> offset = MemberOffset(typeId, i);
							if (!member || !offset)
								return std::nullopt;
							layout.size = std::max(layout.size, *offset + member->size);
							layout.align = std::max(layout.align, member->align);
						}
						layout.size = AlignUp(layout.size, layout.align);
						return layout;
					}
					default:
						Fail(std::format("type %{} has no memory layout", typeId));
						return std::nullopt;
				}
			}

			std::optional<uint64_t> ArrayStride(uint32_t arrayTypeId)
			{
				if (const std::optional<uint32_t> stride = Decoration(arrayTypeId, SpvDecorationArrayStride))
					return *stride;
				const std::optional<Layout> element = LayoutOf(m_types.at(arrayTypeId).element);
				if (!element)
					return std::nullopt;
				return AlignUp(element->size, element->align);
			}

			std::optional<uint64_t> MemberOffset(uint32_t structId, uint32_t member)
			{
				if (const std::optional<uint32_t> offset = MemberDecoration(structId, member, SpvDecorationOffset))
					return *offset;
				const Type& type = m_types.at(structId);
				uint64_t end = 0;
				for (uint32_t i = 0; i <= member; ++i)
				{
					const std::optional<Layout> layout = LayoutOf(type.members[i]);
					if (!layout)
						return std::nullopt;
					const std::optional<uint32_t> decorated = MemberDecoration(structId, i, SpvDecorationOffset);
					const uint64_t offset = decorated ? *decorated : AlignUp(end, layout->align);
					if (i == member)
						return offset;
					end = offset + layout->size;
				}
				return std::nullopt;
			}

			PointerInfo MemberPointerInfo(uint32_t structId, uint32_t member) const
			{
				PointerInfo info;
				if (const std::optional<uint32_t> stride = MemberDecoration(structId, member, SpvDecorationMatrixStride))
					info.matrixStride = *stride;
				info.rowMajor = MemberDecoration(structId, member, SpvDecorationRowMajor).has_value();
				return info;
			}

			std::optional<uint64_t> ColumnStride(uint32_t matrixTypeId, const PointerInfo& info)
			{
				if (info.matrixStride != 0)
					return info.matrixStride;
				const std::optional<Layout> column = LayoutOf(m_types.at(matrixTypeId).element);
				if (!column)
					return std::nullopt;
				return AlignUp(column->size, column->align);
			}

			bool Set(uint32_t id, llvm::Value* value)
			{
				if (!value)
					return false;
				m_values[id] = value;
				return true;
			}

			void SetGlobal(uint32_t id, uint32_t typeId, llvm::Value* value)
			{
				m_globalValues[id] = value;
				m_resultTypes[id] = typeId;
			}

			llvm::Value* Value(uint32_t id)
			{
				if (auto local = m_values.find(id); local != m_values.end())
					return local->second;
				if (auto global = m_globalValues.find(id); global != m_globalValues.end())
					return global->second;
				if (auto descriptor = m_descriptorVariables.find(id); descriptor != m_descriptorVariables.end())
				{
					if (!IsBuffer(descriptor->second.kind))
					{
						llvm::BasicBlock& entry = m_currentFunction->getEntryBlock();
						llvm::IRBuilder<> builder(&entry, entry.getFirstInsertionPt());
						llvm::Value* pointer = builder.CreateGEP(builder.getInt8Ty(), m_setTables.at(descriptor->second.set), builder.getInt32(descriptor->second.offset));
						m_values[id] = pointer;
						return pointer;
					}
					if (descriptor->second.isArray)
					{
						Fail("arrays of descriptors are only supported through access chains");
						return nullptr;
					}
					llvm::BasicBlock& entry = m_currentFunction->getEntryBlock();
					llvm::IRBuilder<> builder(&entry, entry.getFirstInsertionPt());
					llvm::Value* records = nullptr;
					llvm::Value* pointer = BufferPointer(builder, descriptor->second, nullptr, records);
					m_values[id] = pointer;
					m_bufferRecords[id] = records;
					return pointer;
				}
				if (auto unsupported = m_unsupportedVariables.find(id); unsupported != m_unsupportedVariables.end())
				{
					Fail(std::format("variables in storage class {} aren't supported yet",
									 SpvStorageClassToString(static_cast<SpvStorageClass>(unsupported->second))));
					return nullptr;
				}
				Fail(std::format("use of undefined id %{}", id));
				return nullptr;
			}

			uint32_t ResultType(uint32_t id) const
			{
				auto it = m_resultTypes.find(id);
				return it == m_resultTypes.end() ? 0 : it->second;
			}

			std::optional<uint64_t> ConstantValue(uint32_t id) const
			{
				auto it = m_globalValues.find(id);
				if (it == m_globalValues.end())
					return std::nullopt;
				if (const auto* constant = llvm::dyn_cast<llvm::ConstantInt>(it->second))
					return constant->getZExtValue();
				return std::nullopt;
			}

			bool LowerGlobals()
			{
				for (const spirv::Instruction& instruction : m_spirv.m_globalInstructions)
				{
					const std::span<const uint32_t> args = Args(instruction);
					const uint32_t id = instruction.m_resultId;
					switch (instruction.m_opcode)
					{
						case SpvOpCapability:
						case SpvOpExtension:
						case SpvOpMemoryModel:
						case SpvOpExecutionMode:
						case SpvOpExecutionModeId:
						case SpvOpSource:
						case SpvOpSourceContinued:
						case SpvOpSourceExtension:
						case SpvOpString:
						case SpvOpName:
						case SpvOpMemberName:
						case SpvOpModuleProcessed:
						case SpvOpLine:
						case SpvOpNoLine:
						case SpvOpTypeForwardPointer:
						case SpvOpDecorationGroup:
							break;
						case SpvOpExtInstImport:
							m_extInstSets[id] = LiteralString(args);
							break;
						case SpvOpTypeVoid:
							m_types[id] = Type{.kind = Type::Kind::Void};
							break;
						case SpvOpTypeBool:
							m_types[id] = Type{.kind = Type::Kind::Bool};
							break;
						case SpvOpTypeInt:
							m_types[id] = Type{.kind = Type::Kind::Int, .width = args[0], .isSigned = args[1] != 0};
							break;
						case SpvOpTypeFloat:
							m_types[id] = Type{.kind = Type::Kind::Float, .width = args[0]};
							break;
						case SpvOpTypeVector:
							m_types[id] = Type{.kind = Type::Kind::Vector, .element = args[0], .count = args[1]};
							break;
						case SpvOpTypeMatrix:
							m_types[id] = Type{.kind = Type::Kind::Matrix, .element = args[0], .count = args[1]};
							break;
						case SpvOpTypeArray:
						{
							const std::optional<uint64_t> length = ConstantValue(args[1]);
							if (!length)
								return Fail("array length isn't a constant");
							m_types[id] = Type{.kind = Type::Kind::Array, .element = args[0], .count = static_cast<uint32_t>(*length)};
							break;
						}
						case SpvOpTypeRuntimeArray:
							m_types[id] = Type{.kind = Type::Kind::RuntimeArray, .element = args[0]};
							break;
						case SpvOpTypeStruct:
							m_types[id] = Type{.kind = Type::Kind::Struct, .members = std::vector<uint32_t>(args.begin(), args.end())};
							break;
						case SpvOpTypePointer:
							m_types[id] = Type{.kind = Type::Kind::Pointer, .element = args[1], .storageClass = args[0]};
							break;
						case SpvOpTypeFunction:
							m_types[id] = Type{.kind = Type::Kind::Function, .members = std::vector<uint32_t>(args.begin(), args.end())};
							break;
						case SpvOpTypeImage:
							m_types[id] = Type{.kind = Type::Kind::Image,
											   .element = args[0],
											   .dim = args[1],
											   .arrayed = args[3] != 0,
											   .multisampled = args[4] != 0,
											   .sampled = args[5],
											   .depth = args[2] == 1};
							break;
						case SpvOpTypeSampler:
							m_types[id] = Type{.kind = Type::Kind::Sampler};
							break;
						case SpvOpTypeSampledImage:
							m_types[id] = Type{.kind = Type::Kind::SampledImage, .element = args[0]};
							break;
						case SpvOpConstantTrue:
						case SpvOpConstantFalse:
						case SpvOpConstant:
						case SpvOpConstantComposite:
						case SpvOpConstantNull:
						case SpvOpSpecConstantTrue:
						case SpvOpSpecConstantFalse:
						case SpvOpSpecConstant:
						case SpvOpSpecConstantComposite:
							if (!LowerConstant(instruction, args))
								return false;
							break;
						case SpvOpUndef:
						{
							llvm::Type* type = ValueType(instruction.m_typeId);
							if (!type)
								return false;
							SetGlobal(id, instruction.m_typeId, llvm::UndefValue::get(type));
							break;
						}
						case SpvOpVariable:
							if (!LowerGlobalVariable(instruction, args))
								return false;
							break;
						case SpvOpSpecConstantOp:
							if (!LowerSpecConstantOp(instruction, args))
								return false;
							break;
						default:
							return Unsupported(instruction);
					}
				}
				return true;
			}

			bool LowerSpecConstantOp(const spirv::Instruction& instruction, std::span<const uint32_t> args)
			{
				if (!m_constantFolder)
				{
					m_constantFolder = llvm::Function::Create(llvm::FunctionType::get(m_builder.getVoidTy(), false), llvm::GlobalValue::InternalLinkage,
															  "spirv.constants", *m_module);
					llvm::BasicBlock::Create(m_context, "", m_constantFolder);
				}
				spirv::Instruction operation{.m_opcode = args[0], .m_resultId = instruction.m_resultId, .m_typeId = instruction.m_typeId};
				operation.m_operands = {instruction.m_typeId, instruction.m_resultId};
				operation.m_operands.insert(operation.m_operands.end(), args.begin() + 1, args.end());

				llvm::Function* previous = m_currentFunction;
				m_currentFunction = m_constantFolder;
				m_builder.SetInsertPoint(&m_constantFolder->getEntryBlock());
				const bool lowered = LowerInstruction(operation);
				m_currentFunction = previous;
				if (!lowered)
					return false;
				auto* constant = llvm::dyn_cast<llvm::Constant>(m_values[instruction.m_resultId]);
				m_values.erase(instruction.m_resultId);
				if (!constant)
					return Fail(std::format("OpSpecConstantOp {} doesn't fold to a constant", SpvOpToString(static_cast<SpvOp>(args[0]))));
				SetGlobal(instruction.m_resultId, instruction.m_typeId, constant);
				return true;
			}

			std::optional<uint64_t> SpecializedValue(uint32_t id) const
			{
				const std::optional<uint32_t> specId = Decoration(id, SpvDecorationSpecId);
				if (!specId)
					return std::nullopt;
				for (const SpecializationConstant& constant : m_options.specialization)
				{
					if (constant.id == *specId)
						return constant.value;
				}
				return std::nullopt;
			}

			llvm::Constant* ScalarConstant(uint32_t typeId, uint64_t bits)
			{
				const Type* type = TypeOf(typeId);
				if (!type)
					return nullptr;
				if (type->width < 64)
					bits &= (uint64_t{1} << type->width) - 1;
				if (type->kind == Type::Kind::Float)
				{
					const llvm::fltSemantics& semantics = type->width == 16	  ? llvm::APFloat::IEEEhalf()
														  : type->width == 64 ? llvm::APFloat::IEEEdouble()
																			  : llvm::APFloat::IEEEsingle();
					return llvm::ConstantFP::get(m_context, llvm::APFloat(semantics, llvm::APInt(type->width, bits)));
				}
				if (type->kind != Type::Kind::Int)
				{
					Fail("scalar constant of a non-numeric type");
					return nullptr;
				}
				return llvm::ConstantInt::get(m_builder.getIntNTy(type->width), bits);
			}

			bool LowerConstant(const spirv::Instruction& instruction, std::span<const uint32_t> args)
			{
				llvm::Type* type = ValueType(instruction.m_typeId);
				if (!type)
					return false;
				const std::optional<uint64_t> specialized = SpecializedValue(instruction.m_resultId);
				llvm::Constant* value = nullptr;
				switch (instruction.m_opcode)
				{
					case SpvOpConstantTrue:
					case SpvOpConstantFalse:
						value = m_builder.getInt1(instruction.m_opcode == SpvOpConstantTrue);
						break;
					case SpvOpSpecConstantTrue:
					case SpvOpSpecConstantFalse:
						value = m_builder.getInt1(specialized ? *specialized != 0 : instruction.m_opcode == SpvOpSpecConstantTrue);
						break;
					case SpvOpConstant:
					case SpvOpSpecConstant:
					{
						uint64_t bits = args.empty() ? 0 : args[0];
						if (args.size() > 1)
							bits |= static_cast<uint64_t>(args[1]) << 32;
						if (instruction.m_opcode == SpvOpSpecConstant && specialized)
							bits = *specialized;
						value = ScalarConstant(instruction.m_typeId, bits);
						break;
					}
					case SpvOpConstantComposite:
					case SpvOpSpecConstantComposite:
					{
						std::vector<llvm::Constant*> constituents;
						for (const uint32_t constituent : args)
						{
							auto* constant = llvm::dyn_cast_or_null<llvm::Constant>(Value(constituent));
							if (!constant)
								return Fail("composite constant made of a non-constant");
							constituents.push_back(constant);
						}
						if (type->isVectorTy())
							value = llvm::ConstantVector::get(constituents);
						else if (auto* arrayType = llvm::dyn_cast<llvm::ArrayType>(type))
							value = llvm::ConstantArray::get(arrayType, constituents);
						else if (auto* structType = llvm::dyn_cast<llvm::StructType>(type))
							value = llvm::ConstantStruct::get(structType, constituents);
						break;
					}
					case SpvOpConstantNull:
						value = llvm::Constant::getNullValue(type);
						break;
					default:
						break;
				}
				if (!value)
					return Fail("unexpected constant");
				SetGlobal(instruction.m_resultId, instruction.m_typeId, value);
				return true;
			}

			bool LowerGlobalVariable(const spirv::Instruction& instruction, std::span<const uint32_t> args)
			{
				const uint32_t id = instruction.m_resultId;
				const uint32_t storageClass = args[0];
				const Type* pointerType = TypeOf(instruction.m_typeId);
				if (!pointerType)
					return false;
				m_resultTypes[id] = instruction.m_typeId;
				switch (storageClass)
				{
					case SpvStorageClassInput:
					case SpvStorageClassOutput:
					case SpvStorageClassPrivate:
					case SpvStorageClassPushConstant:
						m_invocationVariables.push_back(
							InvocationVariable{.id = id, .pointee = pointerType->element, .storageClass = storageClass, .initializer = args.size() > 1 ? args[1] : 0});
						return true;
					case SpvStorageClassWorkgroup:
					{
						if (args.size() > 1)
							return Fail("Workgroup variables with an initializer aren't supported");
						const std::optional<Layout> layout = LayoutOf(pointerType->element);
						if (!layout)
							return false;
						auto* type = llvm::ArrayType::get(m_builder.getInt8Ty(), std::max<uint64_t>(layout->size, 1));
						auto* global = new llvm::GlobalVariable(*m_module, type, false, llvm::GlobalValue::InternalLinkage, llvm::UndefValue::get(type), "lds",
																nullptr, llvm::GlobalValue::NotThreadLocal, kLdsAddressSpace);
						global->setAlignment(llvm::Align(16));
						m_globalValues[id] = global;
						return true;
					}
					case SpvStorageClassUniform:
					case SpvStorageClassStorageBuffer:
					case SpvStorageClassUniformConstant:
						return AddDescriptorVariable(id, storageClass, pointerType->element);
					default:
						m_unsupportedVariables[id] = storageClass;
						return true;
				}
			}

			bool AddDescriptorVariable(uint32_t id, uint32_t storageClass, uint32_t pointee)
			{
				const Type* type = TypeOf(pointee);
				if (!type)
					return false;
				if (type->kind == Type::Kind::RuntimeArray)
					return Fail("runtime arrays of descriptors aren't supported");
				const bool isArray = type->kind == Type::Kind::Array;
				const uint32_t block = isArray ? type->element : pointee;
				const Type* element = TypeOf(block);
				if (!element)
					return false;
				DescriptorKind kind = DescriptorKind::UniformBuffer;
				switch (element->kind)
				{
					case Type::Kind::Struct:
						kind = storageClass == SpvStorageClassStorageBuffer || Decoration(block, SpvDecorationBufferBlock).has_value() ? DescriptorKind::StorageBuffer
																																	   : DescriptorKind::UniformBuffer;
						break;
					case Type::Kind::SampledImage:
						kind = DescriptorKind::CombinedImageSampler;
						break;
					case Type::Kind::Image:
						if (element->dim == SpvDimBuffer)
							return Fail("texel buffers aren't supported yet");
						kind = element->sampled == 2 ? DescriptorKind::StorageImage : DescriptorKind::SampledImage;
						break;
					case Type::Kind::Sampler:
						kind = DescriptorKind::Sampler;
						break;
					default:
						return Fail("unexpected descriptor type");
				}
				m_descriptorVariables[id] = DescriptorVariable{
					.set = Decoration(id, SpvDecorationDescriptorSet).value_or(0),
					.binding = Decoration(id, SpvDecorationBinding).value_or(0),
					.kind = kind,
					.block = block,
					.count = isArray ? type->count : 1,
					.isArray = isArray,
				};
				return true;
			}

			static uint32_t DescriptorBytes(DescriptorKind kind)
			{
				switch (kind)
				{
					case DescriptorKind::SampledImage:
					case DescriptorKind::StorageImage:
						return 32;
					case DescriptorKind::CombinedImageSampler:
						return 48;
					default:
						return 16;
				}
			}

			static bool IsBuffer(DescriptorKind kind)
			{
				return kind == DescriptorKind::UniformBuffer || kind == DescriptorKind::StorageBuffer;
			}

			llvm::Value* LoadDescriptor(llvm::Value* pointer, const Type& type, llvm::Type* valueType)
			{
				auto load = [&](unsigned dwords, uint64_t offset)
				{
					llvm::LoadInst* descriptor =
						m_builder.CreateAlignedLoad(llvm::FixedVectorType::get(m_builder.getInt32Ty(), dwords), OffsetPointer(pointer, offset), llvm::Align(16));
					descriptor->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(m_context, {}));
					return descriptor;
				};
				if (type.kind == Type::Kind::Image)
					return load(8, 0);
				if (type.kind == Type::Kind::Sampler)
					return load(4, 0);
				llvm::Value* result = m_builder.CreateInsertValue(llvm::PoisonValue::get(valueType), load(8, 0), {0});
				return m_builder.CreateInsertValue(result, load(4, 32), {1});
			}

			bool AssignDescriptorOffsets(std::span<const Descriptor> descriptorLayout, std::vector<Descriptor>& descriptors)
			{
				std::vector<DescriptorVariable*> variables;
				for (auto& [id, variable] : m_descriptorVariables)
					variables.push_back(&variable);
				std::sort(variables.begin(), variables.end(),
						  [](const DescriptorVariable* a, const DescriptorVariable* b)
						  { return a->set != b->set ? a->set < b->set : a->binding < b->binding; });
				std::array<uint32_t, kMaxDescriptorSets> tableBytes = {};
				for (DescriptorVariable* variable : variables)
				{
					if (variable->set >= kMaxDescriptorSets)
						return Fail(std::format("descriptor set {} is beyond the {} sets Vkd supports", variable->set, kMaxDescriptorSets));
					if (descriptorLayout.empty())
					{
						variable->offset = tableBytes[variable->set];
						tableBytes[variable->set] += variable->count * DescriptorBytes(variable->kind);
					}
					else
					{
						auto shared = std::find_if(descriptorLayout.begin(), descriptorLayout.end(),
												   [&](const Descriptor& d)
												   { return d.set == variable->set && d.binding == variable->binding; });
						if (shared == descriptorLayout.end() || shared->kind != variable->kind)
							return Fail(std::format("set {} binding {} doesn't match the pipeline's descriptor layout", variable->set, variable->binding));
						variable->offset = shared->offset;
					}
					descriptors.push_back(Descriptor{variable->set, variable->binding, variable->kind, variable->offset, variable->count});
					if (std::find(m_usedSets.begin(), m_usedSets.end(), variable->set) == m_usedSets.end())
						m_usedSets.push_back(variable->set);
				}
				std::sort(m_usedSets.begin(), m_usedSets.end());
				return true;
			}

			llvm::Value* BufferPointer(llvm::IRBuilder<>& builder, const DescriptorVariable& variable, llvm::Value* index, llvm::Value*& records)
			{
				llvm::Value* offset = builder.getInt32(variable.offset);
				if (index)
					offset = builder.CreateAdd(offset, builder.CreateMul(builder.CreateZExtOrTrunc(index, builder.getInt32Ty()), builder.getInt32(kBufferDescriptorBytes)));
				llvm::Value* address = builder.CreateGEP(builder.getInt8Ty(), m_setTables.at(variable.set), offset);
				llvm::LoadInst* descriptor = builder.CreateAlignedLoad(llvm::FixedVectorType::get(builder.getInt32Ty(), 4), address, llvm::Align(16));
				descriptor->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(m_context, {}));
				records = builder.CreateExtractElement(descriptor, uint64_t{2});
				llvm::Value* resource =
					builder.CreateIntToPtr(builder.CreateBitCast(descriptor, builder.getInt128Ty()), llvm::PointerType::get(m_context, kBufferResourceAddressSpace));
				return builder.CreateAddrSpaceCast(resource, llvm::PointerType::get(m_context, kBufferFatPointerAddressSpace));
			}

			bool ReadLocalSize(const spirv::EntryPoint& entryPoint, std::array<uint32_t, 3>& size)
			{
				for (const spirv::Instruction& instruction : m_spirv.m_globalInstructions)
				{
					if (instruction.m_opcode != SpvOpExecutionMode && instruction.m_opcode != SpvOpExecutionModeId)
						continue;
					const std::span<const uint32_t> args = Args(instruction);
					if (args.size() < 5 || args[0] != entryPoint.m_functionId)
						continue;
					if (args[1] == SpvExecutionModeLocalSize)
						size = {args[2], args[3], args[4]};
					else if (args[1] == SpvExecutionModeLocalSizeId)
					{
						for (uint32_t i = 0; i < 3; ++i)
						{
							const std::optional<uint64_t> value = ConstantValue(args[2 + i]);
							if (!value)
								return Fail("LocalSizeId of a non-constant");
							size[i] = static_cast<uint32_t>(*value);
						}
					}
				}
				for (const auto& [id, decorations] : m_spirv.m_decorations)
				{
					for (const spirv::Decoration& decoration : decorations)
					{
						if (decoration.m_kind != SpvDecorationBuiltIn || decoration.m_literals.empty() || decoration.m_literals[0] != SpvBuiltInWorkgroupSize)
							continue;
						auto* constant = llvm::dyn_cast_or_null<llvm::Constant>(Value(id));
						if (!constant)
							return Fail("WorkgroupSize isn't a constant");
						for (uint32_t i = 0; i < 3; ++i)
						{
							auto* component = llvm::dyn_cast_or_null<llvm::ConstantInt>(constant->getAggregateElement(i));
							if (!component)
								return Fail("WorkgroupSize isn't a constant");
							size[i] = static_cast<uint32_t>(component->getZExtValue());
						}
					}
				}
				if (size[0] == 0 || size[1] == 0 || size[2] == 0 || size[0] * size[1] * size[2] > 1024)
					return Fail("invalid workgroup size");
				return true;
			}

			bool DeclareFunctions()
			{
				for (const spirv::Function& function : m_spirv.m_functions)
				{
					llvm::Type* returnType = ValueType(function.m_resultType);
					if (!returnType)
						return false;
					std::vector<llvm::Type*> parameters;
					for (const uint32_t parameterType : function.m_parameterTypes)
					{
						llvm::Type* type = ValueType(parameterType);
						if (!type)
							return false;
						parameters.push_back(type);
					}
					parameters.insert(parameters.end(), m_invocationVariables.size(), llvm::PointerType::get(m_context, kPrivateAddressSpace));
					parameters.insert(parameters.end(), m_usedSets.size(), llvm::PointerType::get(m_context, kConstantAddressSpace));
					auto* llvmFunction = llvm::Function::Create(llvm::FunctionType::get(returnType, parameters, false), llvm::GlobalValue::InternalLinkage,
																std::format("spirv.{}", function.m_id), *m_module);
					llvmFunction->addFnAttr(llvm::Attribute::AlwaysInline);
					m_functions[function.m_id] = llvmFunction;
				}
				return true;
			}

			bool LowerFunction(const spirv::Function& function)
			{
				if (function.m_blocks.empty())
					return Fail("function without a body");
				llvm::Function* llvmFunction = m_functions.at(function.m_id);
				m_currentFunction = llvmFunction;
				m_values.clear();
				m_blocks.clear();
				m_pendingPhis.clear();

				for (size_t i = 0; i < function.m_parameters.size(); ++i)
				{
					m_values[function.m_parameters[i]] = llvmFunction->getArg(static_cast<unsigned>(i));
					m_resultTypes[function.m_parameters[i]] = function.m_parameterTypes[i];
				}
				for (size_t i = 0; i < m_invocationVariables.size(); ++i)
					m_values[m_invocationVariables[i].id] = llvmFunction->getArg(static_cast<unsigned>(function.m_parameters.size() + i));
				m_setTables.clear();
				m_bufferRecords.clear();
				for (size_t i = 0; i < m_usedSets.size(); ++i)
					m_setTables[m_usedSets[i]] = llvmFunction->getArg(static_cast<unsigned>(function.m_parameters.size() + m_invocationVariables.size() + i));

				for (const spirv::BasicBlock& block : function.m_blocks)
					m_blocks[block.m_label] = llvm::BasicBlock::Create(m_context, "", llvmFunction);

				for (const spirv::BasicBlock& block : function.m_blocks)
				{
					m_builder.SetInsertPoint(m_blocks.at(block.m_label));
					for (const spirv::Instruction& instruction : block.m_instructions)
					{
						if (!LowerInstruction(instruction))
							return false;
					}
					if (!LowerTerminator(block))
						return false;
				}

				for (const PendingPhi& pending : m_pendingPhis)
				{
					const std::span<const uint32_t> args = Args(*pending.instruction);
					for (size_t i = 0; i + 1 < args.size(); i += 2)
					{
						llvm::Value* value = Value(args[i]);
						llvm::BasicBlock* predecessor = Block(args[i + 1]);
						if (!value || !predecessor)
							return false;
						const llvm::Instruction* terminator = predecessor->getTerminator();
						for (unsigned s = 0; terminator && s < terminator->getNumSuccessors(); ++s)
						{
							if (terminator->getSuccessor(s) == pending.phi->getParent())
								pending.phi->addIncoming(value, predecessor);
						}
					}
				}
				return true;
			}

			llvm::BasicBlock* Block(uint32_t label)
			{
				auto it = m_blocks.find(label);
				if (it == m_blocks.end())
				{
					Fail(std::format("branch to unknown block %{}", label));
					return nullptr;
				}
				return it->second;
			}

			bool LowerTerminator(const spirv::BasicBlock& block)
			{
				switch (block.m_terminator)
				{
					case spirv::Terminator::Branch:
					{
						llvm::BasicBlock* target = Block(block.m_branchTargets[0]);
						if (!target)
							return false;
						m_builder.CreateBr(target);
						return true;
					}
					case spirv::Terminator::BranchConditional:
					{
						llvm::Value* condition = Value(block.m_terminatorOperand);
						llvm::BasicBlock* onTrue = Block(block.m_branchTargets[0]);
						llvm::BasicBlock* onFalse = Block(block.m_branchTargets[1]);
						if (!condition || !onTrue || !onFalse)
							return false;
						m_builder.CreateCondBr(condition, onTrue, onFalse);
						return true;
					}
					case spirv::Terminator::Switch:
					{
						llvm::Value* selector = Value(block.m_terminatorOperand);
						llvm::BasicBlock* defaultBlock = Block(block.m_branchTargets[0]);
						if (!selector || !defaultBlock)
							return false;
						auto* selectorType = llvm::cast<llvm::IntegerType>(selector->getType());
						llvm::SwitchInst* instruction = m_builder.CreateSwitch(selector, defaultBlock, static_cast<unsigned>(block.m_switchLiterals.size()));
						for (size_t i = 0; i < block.m_switchLiterals.size(); ++i)
						{
							llvm::BasicBlock* target = Block(block.m_branchTargets[i + 1]);
							if (!target)
								return false;
							instruction->addCase(llvm::ConstantInt::get(selectorType, block.m_switchLiterals[i]), target);
						}
						return true;
					}
					case spirv::Terminator::Return:
						m_builder.CreateRetVoid();
						return true;
					case spirv::Terminator::ReturnValue:
					{
						llvm::Value* value = Value(block.m_terminatorOperand);
						if (!value)
							return false;
						m_builder.CreateRet(value);
						return true;
					}
					case spirv::Terminator::Unreachable:
						m_builder.CreateUnreachable();
						return true;
					case spirv::Terminator::Kill:
						if (!m_fragmentStage)
							return Fail("OpKill outside a fragment shader");
						m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_kill, {}, {m_builder.getFalse()});
						m_builder.CreateUnreachable();
						return true;
					default:
						return Fail("block without a terminator");
				}
			}

			llvm::AllocaInst* CreateAlloca(const Layout& layout)
			{
				llvm::BasicBlock& entry = m_currentFunction->getEntryBlock();
				llvm::IRBuilder<> builder(&entry, entry.begin());
				auto* type = llvm::ArrayType::get(m_builder.getInt8Ty(), std::max<uint64_t>(AlignUp(layout.size, 4), 4));
				llvm::AllocaInst* alloca = builder.CreateAlloca(type, kPrivateAddressSpace, nullptr);
				alloca->setAlignment(llvm::Align(std::max<uint32_t>(layout.align, 4)));
				return alloca;
			}

			llvm::Value* OffsetPointer(llvm::Value* pointer, uint64_t offset)
			{
				return offset == 0 ? pointer : m_builder.CreateConstGEP1_64(m_builder.getInt8Ty(), pointer, offset);
			}

			llvm::Value* LoadMemory(llvm::Value* pointer, uint32_t typeId, const PointerInfo& info)
			{
				const Type* type = TypeOf(typeId);
				llvm::Type* valueType = ValueType(typeId);
				if (!type || !valueType)
					return nullptr;
				switch (type->kind)
				{
					case Type::Kind::Bool:
						return m_builder.CreateICmpNE(m_builder.CreateAlignedLoad(m_builder.getInt32Ty(), pointer, llvm::Align(4)), m_builder.getInt32(0));
					case Type::Kind::Int:
					case Type::Kind::Float:
						return m_builder.CreateAlignedLoad(valueType, pointer, llvm::Align(type->width / 8));
					case Type::Kind::Pointer:
						return m_builder.CreateIntToPtr(m_builder.CreateAlignedLoad(m_builder.getInt64Ty(), pointer, llvm::Align(4)), valueType);
					case Type::Kind::Image:
					case Type::Kind::Sampler:
					case Type::Kind::SampledImage:
						return LoadDescriptor(pointer, *type, valueType);
					case Type::Kind::Vector:
					{
						const Type& element = m_types.at(type->element);
						if (info.componentStride != 0)
						{
							llvm::Value* result = llvm::PoisonValue::get(valueType);
							for (uint32_t i = 0; i < type->count; ++i)
							{
								llvm::Value* component = LoadMemory(OffsetPointer(pointer, uint64_t{info.componentStride} * i), type->element, {});
								if (!component)
									return nullptr;
								result = m_builder.CreateInsertElement(result, component, uint64_t{i});
							}
							return result;
						}
						if (element.kind == Type::Kind::Bool)
						{
							auto* stored = llvm::FixedVectorType::get(m_builder.getInt32Ty(), type->count);
							return m_builder.CreateICmpNE(m_builder.CreateAlignedLoad(stored, pointer, llvm::Align(4)), llvm::Constant::getNullValue(stored));
						}
						return m_builder.CreateAlignedLoad(valueType, pointer, llvm::Align(element.width / 8));
					}
					case Type::Kind::Matrix:
					case Type::Kind::Array:
					case Type::Kind::Struct:
					{
						llvm::Value* result = llvm::PoisonValue::get(valueType);
						const uint32_t count = type->kind == Type::Kind::Struct ? static_cast<uint32_t>(type->members.size()) : type->count;
						for (uint32_t i = 0; i < count; ++i)
						{
							uint32_t elementType = 0;
							PointerInfo elementInfo;
							const std::optional<uint64_t> offset = ElementOffset(typeId, i, info, elementType, elementInfo);
							if (!offset)
								return nullptr;
							llvm::Value* element = LoadMemory(OffsetPointer(pointer, *offset), elementType, elementInfo);
							if (!element)
								return nullptr;
							result = m_builder.CreateInsertValue(result, element, {i});
						}
						return result;
					}
					default:
						Fail(std::format("load of type %{}", typeId));
						return nullptr;
				}
			}

			bool StoreMemory(llvm::Value* pointer, uint32_t typeId, llvm::Value* value, const PointerInfo& info)
			{
				const Type* type = TypeOf(typeId);
				if (!type)
					return false;
				switch (type->kind)
				{
					case Type::Kind::Bool:
						m_builder.CreateAlignedStore(m_builder.CreateZExt(value, m_builder.getInt32Ty()), pointer, llvm::Align(4));
						return true;
					case Type::Kind::Int:
					case Type::Kind::Float:
						m_builder.CreateAlignedStore(value, pointer, llvm::Align(type->width / 8));
						return true;
					case Type::Kind::Pointer:
						m_builder.CreateAlignedStore(m_builder.CreatePtrToInt(value, m_builder.getInt64Ty()), pointer, llvm::Align(4));
						return true;
					case Type::Kind::Vector:
					{
						const Type& element = m_types.at(type->element);
						if (info.componentStride != 0)
						{
							for (uint32_t i = 0; i < type->count; ++i)
							{
								if (!StoreMemory(OffsetPointer(pointer, uint64_t{info.componentStride} * i), type->element,
												 m_builder.CreateExtractElement(value, uint64_t{i}), {}))
									return false;
							}
							return true;
						}
						if (element.kind == Type::Kind::Bool)
						{
							m_builder.CreateAlignedStore(m_builder.CreateZExt(value, llvm::FixedVectorType::get(m_builder.getInt32Ty(), type->count)), pointer,
														 llvm::Align(4));
							return true;
						}
						m_builder.CreateAlignedStore(value, pointer, llvm::Align(element.width / 8));
						return true;
					}
					case Type::Kind::Matrix:
					case Type::Kind::Array:
					case Type::Kind::Struct:
					{
						const uint32_t count = type->kind == Type::Kind::Struct ? static_cast<uint32_t>(type->members.size()) : type->count;
						for (uint32_t i = 0; i < count; ++i)
						{
							uint32_t elementType = 0;
							PointerInfo elementInfo;
							const std::optional<uint64_t> offset = ElementOffset(typeId, i, info, elementType, elementInfo);
							if (!offset || !StoreMemory(OffsetPointer(pointer, *offset), elementType, m_builder.CreateExtractValue(value, {i}), elementInfo))
								return false;
						}
						return true;
					}
					default:
						return Fail(std::format("store of type %{}", typeId));
				}
			}

			std::optional<uint64_t> ElementOffset(uint32_t typeId, uint32_t i, const PointerInfo& info, uint32_t& elementType, PointerInfo& elementInfo)
			{
				const Type& type = m_types.at(typeId);
				switch (type.kind)
				{
					case Type::Kind::Struct:
						elementType = type.members[i];
						elementInfo = MemberPointerInfo(typeId, i);
						return MemberOffset(typeId, i);
					case Type::Kind::Array:
					case Type::Kind::RuntimeArray:
					{
						elementType = type.element;
						elementInfo = info;
						const std::optional<uint64_t> stride = ArrayStride(typeId);
						if (!stride)
							return std::nullopt;
						return *stride * i;
					}
					case Type::Kind::Matrix:
					{
						elementType = type.element;
						elementInfo = {};
						if (info.rowMajor)
						{
							const std::optional<Layout> component = LayoutOf(m_types.at(type.element).element);
							if (!component)
								return std::nullopt;
							if (info.matrixStride == 0)
							{
								Fail("row-major matrix without a MatrixStride");
								return std::nullopt;
							}
							elementInfo.componentStride = info.matrixStride;
							return component->size * i;
						}
						const std::optional<uint64_t> stride = ColumnStride(typeId, info);
						if (!stride)
							return std::nullopt;
						return *stride * i;
					}
					case Type::Kind::Vector:
					{
						elementType = type.element;
						elementInfo = {};
						if (info.componentStride != 0)
							return uint64_t{info.componentStride} * i;
						const std::optional<Layout> element = LayoutOf(type.element);
						if (!element)
							return std::nullopt;
						return element->size * i;
					}
					default:
						Fail(std::format("type %{} has no elements", typeId));
						return std::nullopt;
				}
			}

			PointerInfo InfoOf(uint32_t pointerId) const
			{
				auto it = m_pointerInfo.find(pointerId);
				return it == m_pointerInfo.end() ? PointerInfo{} : it->second;
			}

			uint32_t PointeeOf(uint32_t pointerId)
			{
				const uint32_t pointerType = ResultType(pointerId);
				auto it = m_types.find(pointerType);
				if (it == m_types.end() || it->second.kind != Type::Kind::Pointer)
				{
					Fail(std::format("%{} isn't a pointer", pointerId));
					return 0;
				}
				return it->second.element;
			}

			bool LowerAccessChain(uint32_t id, std::span<const uint32_t> args, bool hasElement)
			{
				llvm::Value* base = nullptr;
				uint32_t type = 0;
				llvm::Value* records = nullptr;
				size_t first = 1;
				if (auto descriptor = m_descriptorVariables.find(args[0]);
					descriptor != m_descriptorVariables.end() && descriptor->second.isArray && IsBuffer(descriptor->second.kind))
				{
					if (hasElement || args.size() < 2)
						return Fail("unsupported access chain into an array of descriptors");
					llvm::Value* element = Value(args[1]);
					if (!element)
						return false;
					base = BufferPointer(m_builder, descriptor->second, element, records);
					type = descriptor->second.block;
					first = 2;
				}
				else
				{
					base = Value(args[0]);
					type = PointeeOf(args[0]);
					if (auto known = m_bufferRecords.find(args[0]); known != m_bufferRecords.end())
						records = known->second;
				}
				if (!base || type == 0)
					return false;
				if (records)
					m_bufferRecords[id] = records;
				PointerInfo info = InfoOf(args[0]);
				auto* indexType = llvm::cast<llvm::IntegerType>(m_module->getDataLayout().getIndexType(base->getType()));
				llvm::Value* offset = llvm::ConstantInt::get(indexType, 0);

				auto index = [&](uint32_t indexId) -> llvm::Value*
				{
					llvm::Value* value = Value(indexId);
					return value ? m_builder.CreateSExtOrTrunc(value, indexType) : nullptr;
				};
				auto addScaled = [&](llvm::Value* value, uint64_t scale)
				{
					offset = m_builder.CreateAdd(offset, m_builder.CreateMul(value, llvm::ConstantInt::get(indexType, scale)));
				};

				if (hasElement)
				{
					llvm::Value* element = index(args[1]);
					if (!element)
						return false;
					std::optional<uint64_t> stride;
					if (const std::optional<uint32_t> decorated = Decoration(ResultType(args[0]), SpvDecorationArrayStride))
						stride = *decorated;
					else if (const std::optional<Layout> layout = LayoutOf(type))
						stride = AlignUp(layout->size, layout->align);
					if (!stride)
						return false;
					addScaled(element, *stride);
					first = 2;
				}

				for (size_t i = first; i < args.size(); ++i)
				{
					const Type* current = TypeOf(type);
					if (!current)
						return false;
					if (current->kind == Type::Kind::Struct)
					{
						const std::optional<uint64_t> member = ConstantValue(args[i]);
						if (!member || *member >= current->members.size())
							return Fail("struct access chain index isn't a valid constant");
						uint32_t memberType = 0;
						const std::optional<uint64_t> memberOffset = ElementOffset(type, static_cast<uint32_t>(*member), info, memberType, info);
						if (!memberOffset)
							return false;
						offset = m_builder.CreateAdd(offset, llvm::ConstantInt::get(indexType, *memberOffset));
						type = memberType;
						continue;
					}
					llvm::Value* value = index(args[i]);
					if (!value)
						return false;
					uint32_t elementType = 0;
					PointerInfo elementInfo;
					const std::optional<uint64_t> stride = ElementOffset(type, 1, info, elementType, elementInfo);
					if (!stride)
						return false;
					addScaled(value, *stride);
					type = elementType;
					info = elementInfo;
				}

				m_pointerInfo[id] = info;
				return Set(id, m_builder.CreateGEP(m_builder.getInt8Ty(), base, offset));
			}

			bool LowerLocalVariable(const spirv::Instruction& instruction, std::span<const uint32_t> args)
			{
				if (args[0] != SpvStorageClassFunction)
					return Fail("non-Function variable inside a function");
				const Type* pointerType = TypeOf(instruction.m_typeId);
				if (!pointerType)
					return false;
				const std::optional<Layout> layout = LayoutOf(pointerType->element);
				if (!layout)
					return false;
				llvm::AllocaInst* storage = CreateAlloca(*layout);
				if (args.size() > 1)
				{
					llvm::Value* initializer = Value(args[1]);
					if (!initializer || !StoreMemory(storage, pointerType->element, initializer, {}))
						return false;
				}
				return Set(instruction.m_resultId, storage);
			}

			llvm::Value* SafeDivisor(llvm::Value* dividend, llvm::Value* divisor, bool isSigned)
			{
				llvm::Value* zero = llvm::Constant::getNullValue(divisor->getType());
				llvm::Value* one = llvm::ConstantInt::get(divisor->getType(), 1);
				llvm::Value* bad = m_builder.CreateICmpEQ(divisor, zero);
				if (isSigned)
				{
					const unsigned width = divisor->getType()->getScalarSizeInBits();
					llvm::Value* minimum = llvm::ConstantInt::get(divisor->getType(), llvm::APInt::getSignedMinValue(width));
					llvm::Value* overflow = m_builder.CreateAnd(m_builder.CreateICmpEQ(dividend, minimum),
																m_builder.CreateICmpEQ(divisor, llvm::Constant::getAllOnesValue(divisor->getType())));
					bad = m_builder.CreateOr(bad, overflow);
				}
				return m_builder.CreateSelect(bad, one, divisor);
			}

			llvm::Value* ShiftAmount(llvm::Value* amount, llvm::Type* baseType)
			{
				amount = m_builder.CreateZExtOrTrunc(amount, baseType);
				return m_builder.CreateAnd(amount, llvm::ConstantInt::get(baseType, baseType->getScalarSizeInBits() - 1));
			}

			llvm::Value* Splat(llvm::Value* scalar, llvm::Type* like)
			{
				if (auto* vectorType = llvm::dyn_cast<llvm::FixedVectorType>(like))
					return m_builder.CreateVectorSplat(vectorType->getNumElements(), scalar);
				return scalar;
			}

			llvm::Value* QuantizeToF16(llvm::Value* value)
			{
				llvm::Type* halfType = m_builder.getHalfTy();
				if (auto* vectorType = llvm::dyn_cast<llvm::FixedVectorType>(value->getType()))
					halfType = llvm::FixedVectorType::get(halfType, vectorType->getNumElements());
				llvm::Value* quantized = m_builder.CreateFPExt(m_builder.CreateFPTrunc(value, halfType), value->getType());
				llvm::Value* magnitude = m_builder.CreateUnaryIntrinsic(llvm::Intrinsic::fabs, quantized);
				llvm::Value* subnormal = m_builder.CreateFCmpOLT(magnitude, llvm::ConstantFP::get(value->getType(), 6.103515625e-05));
				llvm::Value* zero = m_builder.CreateBinaryIntrinsic(llvm::Intrinsic::copysign, llvm::ConstantFP::get(value->getType(), 0.0), value);
				return m_builder.CreateSelect(subnormal, zero, quantized);
			}

			llvm::Value* ReduceBool(llvm::Value* vector, bool all)
			{
				auto* vectorType = llvm::dyn_cast<llvm::FixedVectorType>(vector->getType());
				if (!vectorType)
					return vector;
				llvm::Value* result = m_builder.CreateExtractElement(vector, uint64_t{0});
				for (unsigned i = 1; i < vectorType->getNumElements(); ++i)
				{
					llvm::Value* component = m_builder.CreateExtractElement(vector, i);
					result = all ? m_builder.CreateAnd(result, component) : m_builder.CreateOr(result, component);
				}
				return result;
			}

			llvm::Value* Dot(llvm::Value* a, llvm::Value* b)
			{
				auto* vectorType = llvm::dyn_cast<llvm::FixedVectorType>(a->getType());
				if (!vectorType)
					return m_builder.CreateFMul(a, b);
				llvm::Value* sum = nullptr;
				for (unsigned i = 0; i < vectorType->getNumElements(); ++i)
				{
					llvm::Value* product = m_builder.CreateFMul(m_builder.CreateExtractElement(a, i), m_builder.CreateExtractElement(b, i));
					sum = sum ? m_builder.CreateFAdd(sum, product) : product;
				}
				return sum;
			}

			bool LowerInstruction(const spirv::Instruction& instruction)
			{
				const std::span<const uint32_t> args = Args(instruction);
				const uint32_t id = instruction.m_resultId;
				const uint32_t resultTypeId = instruction.m_typeId;
				if (id != 0 && resultTypeId != 0)
					m_resultTypes[id] = resultTypeId;
				llvm::Type* resultType = nullptr;
				if (resultTypeId != 0 && instruction.m_opcode != SpvOpVariable && instruction.m_opcode != SpvOpAccessChain &&
					instruction.m_opcode != SpvOpInBoundsAccessChain && instruction.m_opcode != SpvOpPtrAccessChain &&
					instruction.m_opcode != SpvOpInBoundsPtrAccessChain)
				{
					resultType = ValueType(resultTypeId);
					if (!resultType)
						return false;
				}

				auto operand = [&](size_t i) -> llvm::Value*
				{
					return i < args.size() ? Value(args[i]) : nullptr;
				};
				auto binary = [&](auto&& build) -> bool
				{
					llvm::Value* a = operand(0);
					llvm::Value* b = operand(1);
					if (!a || !b)
						return false;
					return Set(id, build(a, b));
				};
				auto unary = [&](auto&& build) -> bool
				{
					llvm::Value* a = operand(0);
					if (!a)
						return false;
					return Set(id, build(a));
				};

				switch (instruction.m_opcode)
				{
					case SpvOpNop:
					case SpvOpLine:
					case SpvOpNoLine:
					case SpvOpName:
					case SpvOpSelectionMerge:
					case SpvOpLoopMerge:
						return true;

					case SpvOpVariable:
						return LowerLocalVariable(instruction, args);
					case SpvOpLoad:
					{
						llvm::Value* pointer = operand(0);
						if (!pointer)
							return false;
						return Set(id, LoadMemory(pointer, resultTypeId, InfoOf(args[0])));
					}
					case SpvOpStore:
					{
						llvm::Value* pointer = operand(0);
						llvm::Value* value = operand(1);
						const uint32_t pointee = PointeeOf(args[0]);
						if (!pointer || !value || pointee == 0)
							return false;
						return StoreMemory(pointer, pointee, value, InfoOf(args[0]));
					}
					case SpvOpCopyMemory:
					{
						llvm::Value* target = operand(0);
						llvm::Value* source = operand(1);
						const uint32_t pointee = PointeeOf(args[1]);
						if (!target || !source || pointee == 0)
							return false;
						llvm::Value* value = LoadMemory(source, pointee, InfoOf(args[1]));
						return value && StoreMemory(target, pointee, value, InfoOf(args[0]));
					}
					case SpvOpAccessChain:
					case SpvOpInBoundsAccessChain:
						return LowerAccessChain(id, args, false);
					case SpvOpPtrAccessChain:
					case SpvOpInBoundsPtrAccessChain:
						return LowerAccessChain(id, args, true);
					case SpvOpCopyObject:
						if (auto info = m_pointerInfo.find(args[0]); info != m_pointerInfo.end())
							m_pointerInfo[id] = info->second;
						return unary([](llvm::Value* a)
									 { return a; });
					case SpvOpUndef:
						return Set(id, llvm::UndefValue::get(resultType));
					case SpvOpPhi:
					{
						llvm::PHINode* phi = m_builder.CreatePHI(resultType, static_cast<unsigned>(args.size() / 2));
						m_pendingPhis.push_back(PendingPhi{phi, &instruction});
						return Set(id, phi);
					}
					case SpvOpFunctionCall:
					{
						auto function = m_functions.find(args[0]);
						if (function == m_functions.end())
							return Fail("call of an unknown function");
						std::vector<llvm::Value*> callArgs;
						for (size_t i = 1; i < args.size(); ++i)
						{
							llvm::Value* value = Value(args[i]);
							if (!value)
								return false;
							callArgs.push_back(value);
						}
						for (const InvocationVariable& variable : m_invocationVariables)
							callArgs.push_back(m_values.at(variable.id));
						for (const uint32_t set : m_usedSets)
							callArgs.push_back(m_setTables.at(set));
						llvm::CallInst* call = m_builder.CreateCall(function->second, callArgs);
						return resultType->isVoidTy() ? true : Set(id, call);
					}

					case SpvOpIAdd:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateAdd(a, b); });
					case SpvOpISub:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateSub(a, b); });
					case SpvOpIMul:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateMul(a, b); });
					case SpvOpUDiv:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateUDiv(a, SafeDivisor(a, b, false)); });
					case SpvOpSDiv:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateSDiv(a, SafeDivisor(a, b, true)); });
					case SpvOpUMod:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateURem(a, SafeDivisor(a, b, false)); });
					case SpvOpSRem:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateSRem(a, SafeDivisor(a, b, true)); });
					case SpvOpSMod:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  {
							llvm::Value* remainder = m_builder.CreateSRem(a, SafeDivisor(a, b, true));
							llvm::Value* zero = llvm::Constant::getNullValue(a->getType());
							llvm::Value* fix = m_builder.CreateAnd(m_builder.CreateICmpNE(remainder, zero),
																   m_builder.CreateICmpSLT(m_builder.CreateXor(remainder, b), zero));
							return m_builder.CreateSelect(fix, m_builder.CreateAdd(remainder, b), remainder); });
					case SpvOpSNegate:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateNeg(a); });

					case SpvOpFAdd:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFAdd(a, b); });
					case SpvOpFSub:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFSub(a, b); });
					case SpvOpFMul:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFMul(a, b); });
					case SpvOpFDiv:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFDiv(a, b); });
					case SpvOpFRem:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFRem(a, b); });
					case SpvOpFMod:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  {
							llvm::Value* quotient = m_builder.CreateUnaryIntrinsic(llvm::Intrinsic::floor, m_builder.CreateFDiv(a, b));
							return m_builder.CreateFSub(a, m_builder.CreateFMul(b, quotient)); });
					case SpvOpFNegate:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateFNeg(a); });
					case SpvOpQuantizeToF16:
						return unary([&](llvm::Value* a)
									 { return QuantizeToF16(a); });
					case SpvOpVectorTimesScalar:
						return binary([&](llvm::Value* v, llvm::Value* s)
									  { return m_builder.CreateFMul(v, Splat(s, v->getType())); });
					case SpvOpDot:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return Dot(a, b); });

					case SpvOpBitwiseAnd:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateAnd(a, b); });
					case SpvOpBitwiseOr:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateOr(a, b); });
					case SpvOpBitwiseXor:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateXor(a, b); });
					case SpvOpNot:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateNot(a); });
					case SpvOpShiftLeftLogical:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateShl(a, ShiftAmount(b, a->getType())); });
					case SpvOpShiftRightLogical:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateLShr(a, ShiftAmount(b, a->getType())); });
					case SpvOpShiftRightArithmetic:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateAShr(a, ShiftAmount(b, a->getType())); });
					case SpvOpBitCount:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateZExtOrTrunc(m_builder.CreateUnaryIntrinsic(llvm::Intrinsic::ctpop, a), resultType); });
					case SpvOpBitReverse:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateUnaryIntrinsic(llvm::Intrinsic::bitreverse, a); });

					case SpvOpConvertFToU:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateIntrinsic(llvm::Intrinsic::fptoui_sat, {resultType, a->getType()}, {a}); });
					case SpvOpConvertFToS:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateIntrinsic(llvm::Intrinsic::fptosi_sat, {resultType, a->getType()}, {a}); });
					case SpvOpConvertSToF:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateSIToFP(a, resultType); });
					case SpvOpConvertUToF:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateUIToFP(a, resultType); });
					case SpvOpUConvert:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateZExtOrTrunc(a, resultType); });
					case SpvOpSConvert:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateSExtOrTrunc(a, resultType); });
					case SpvOpFConvert:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateFPCast(a, resultType); });
					case SpvOpConvertPtrToU:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreatePtrToInt(a, resultType); });
					case SpvOpConvertUToPtr:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateIntToPtr(m_builder.CreateZExtOrTrunc(a, m_builder.getInt64Ty()), resultType); });
					case SpvOpBitcast:
						return unary([&](llvm::Value* a) -> llvm::Value*
									 {
							if (a->getType()->isPointerTy() && resultType->isPointerTy())
								return m_builder.CreateAddrSpaceCast(a, resultType);
							if (a->getType()->isPointerTy())
								return m_builder.CreateBitCast(m_builder.CreatePtrToInt(a, m_builder.getInt64Ty()), resultType);
							if (resultType->isPointerTy())
								return m_builder.CreateIntToPtr(m_builder.CreateBitCast(a, m_builder.getInt64Ty()), resultType);
							return m_builder.CreateBitCast(a, resultType); });

					case SpvOpIEqual:
					case SpvOpLogicalEqual:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateICmpEQ(a, b); });
					case SpvOpINotEqual:
					case SpvOpLogicalNotEqual:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateICmpNE(a, b); });
					case SpvOpUGreaterThan:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateICmpUGT(a, b); });
					case SpvOpSGreaterThan:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateICmpSGT(a, b); });
					case SpvOpUGreaterThanEqual:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateICmpUGE(a, b); });
					case SpvOpSGreaterThanEqual:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateICmpSGE(a, b); });
					case SpvOpULessThan:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateICmpULT(a, b); });
					case SpvOpSLessThan:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateICmpSLT(a, b); });
					case SpvOpULessThanEqual:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateICmpULE(a, b); });
					case SpvOpSLessThanEqual:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateICmpSLE(a, b); });
					case SpvOpFOrdEqual:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFCmpOEQ(a, b); });
					case SpvOpFUnordEqual:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFCmpUEQ(a, b); });
					case SpvOpFOrdNotEqual:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFCmpONE(a, b); });
					case SpvOpFUnordNotEqual:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFCmpUNE(a, b); });
					case SpvOpFOrdLessThan:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFCmpOLT(a, b); });
					case SpvOpFUnordLessThan:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFCmpULT(a, b); });
					case SpvOpFOrdGreaterThan:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFCmpOGT(a, b); });
					case SpvOpFUnordGreaterThan:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFCmpUGT(a, b); });
					case SpvOpFOrdLessThanEqual:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFCmpOLE(a, b); });
					case SpvOpFUnordLessThanEqual:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFCmpULE(a, b); });
					case SpvOpFOrdGreaterThanEqual:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFCmpOGE(a, b); });
					case SpvOpFUnordGreaterThanEqual:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateFCmpUGE(a, b); });
					case SpvOpIsNan:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateFCmpUNO(a, a); });
					case SpvOpIsInf:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateFCmpOEQ(m_builder.CreateUnaryIntrinsic(llvm::Intrinsic::fabs, a), llvm::ConstantFP::getInfinity(a->getType())); });
					case SpvOpLogicalAnd:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateAnd(a, b); });
					case SpvOpLogicalOr:
						return binary([&](llvm::Value* a, llvm::Value* b)
									  { return m_builder.CreateOr(a, b); });
					case SpvOpLogicalNot:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateNot(a); });
					case SpvOpAny:
						return unary([&](llvm::Value* a)
									 { return ReduceBool(a, false); });
					case SpvOpAll:
						return unary([&](llvm::Value* a)
									 { return ReduceBool(a, true); });
					case SpvOpSelect:
					{
						llvm::Value* condition = operand(0);
						llvm::Value* a = operand(1);
						llvm::Value* b = operand(2);
						if (!condition || !a || !b)
							return false;
						return Set(id, m_builder.CreateSelect(condition, a, b));
					}

					case SpvOpCompositeConstruct:
						return LowerCompositeConstruct(id, resultType, args);
					case SpvOpCompositeExtract:
					{
						llvm::Value* value = operand(0);
						if (!value)
							return false;
						for (size_t i = 1; i < args.size(); ++i)
						{
							value = value->getType()->isVectorTy() ? m_builder.CreateExtractElement(value, args[i])
																   : m_builder.CreateExtractValue(value, {args[i]});
						}
						return Set(id, value);
					}
					case SpvOpCompositeInsert:
					{
						llvm::Value* object = operand(0);
						llvm::Value* composite = operand(1);
						if (!object || !composite)
							return false;
						return Set(id, InsertAt(composite, args.subspan(2), object));
					}
					case SpvOpVectorShuffle:
					{
						llvm::Value* a = operand(0);
						llvm::Value* b = operand(1);
						if (!a || !b)
							return false;
						const unsigned aCount = llvm::cast<llvm::FixedVectorType>(a->getType())->getNumElements();
						llvm::Value* result = llvm::PoisonValue::get(resultType);
						for (size_t i = 2; i < args.size(); ++i)
						{
							if (args[i] == 0xFFFFFFFFu)
								continue;
							llvm::Value* component =
								args[i] < aCount ? m_builder.CreateExtractElement(a, args[i]) : m_builder.CreateExtractElement(b, args[i] - aCount);
							result = m_builder.CreateInsertElement(result, component, static_cast<uint64_t>(i - 2));
						}
						return Set(id, result);
					}
					case SpvOpVectorExtractDynamic:
						return binary([&](llvm::Value* v, llvm::Value* index)
									  { return m_builder.CreateExtractElement(v, index); });
					case SpvOpVectorInsertDynamic:
					{
						llvm::Value* vector = operand(0);
						llvm::Value* component = operand(1);
						llvm::Value* index = operand(2);
						if (!vector || !component || !index)
							return false;
						return Set(id, m_builder.CreateInsertElement(vector, component, index));
					}

					case SpvOpControlBarrier:
						return LowerBarrier(args[0], args[1], args[2], true);
					case SpvOpMemoryBarrier:
						return LowerBarrier(0, args[0], args[1], false);

					case SpvOpExtInst:
						return LowerExtInst(id, resultType, args);

					case SpvOpSampledImage:
					{
						llvm::Value* image = operand(0);
						llvm::Value* sampler = operand(1);
						if (!image || !sampler)
							return false;
						return Set(id, m_builder.CreateInsertValue(m_builder.CreateInsertValue(llvm::PoisonValue::get(resultType), image, {0}), sampler, {1}));
					}
					case SpvOpImage:
						return unary([&](llvm::Value* a)
									 { return m_builder.CreateExtractValue(a, {0}); });
					case SpvOpImageSampleImplicitLod:
					case SpvOpImageSampleExplicitLod:
					case SpvOpImageSampleDrefImplicitLod:
					case SpvOpImageSampleDrefExplicitLod:
						return LowerImageSample(instruction.m_opcode, id, resultType, args);
					case SpvOpImageFetch:
					case SpvOpImageRead:
						return LowerImageLoad(id, resultType, args);
					case SpvOpImageWrite:
						return LowerImageStore(args);
					case SpvOpImageQuerySizeLod:
					case SpvOpImageQuerySize:
					case SpvOpImageQueryLevels:
						return LowerImageQuery(instruction.m_opcode, id, resultType, args);

					case SpvOpArrayLength:
					{
						if (!Value(args[0]))
							return false;
						auto records = m_bufferRecords.find(args[0]);
						const uint32_t block = PointeeOf(args[0]);
						if (records == m_bufferRecords.end() || block == 0)
							return Fail("OpArrayLength of a pointer that isn't a buffer block");
						const std::optional<uint64_t> offset = MemberOffset(block, args[1]);
						const std::optional<uint64_t> stride = ArrayStride(m_types.at(block).members[args[1]]);
						if (!offset || !stride)
							return false;
						llvm::Value* bytes = records->second;
						llvm::Value* length = m_builder.CreateUDiv(m_builder.CreateSub(bytes, m_builder.getInt32(static_cast<uint32_t>(*offset))),
																   m_builder.getInt32(static_cast<uint32_t>(*stride)));
						return Set(id, m_builder.CreateSelect(m_builder.CreateICmpULT(bytes, m_builder.getInt32(static_cast<uint32_t>(*offset))),
															  m_builder.getInt32(0), length));
					}

					case SpvOpAtomicLoad:
					case SpvOpAtomicStore:
					case SpvOpAtomicExchange:
					case SpvOpAtomicCompareExchange:
					case SpvOpAtomicIIncrement:
					case SpvOpAtomicIDecrement:
					case SpvOpAtomicIAdd:
					case SpvOpAtomicISub:
					case SpvOpAtomicSMin:
					case SpvOpAtomicUMin:
					case SpvOpAtomicSMax:
					case SpvOpAtomicUMax:
					case SpvOpAtomicAnd:
					case SpvOpAtomicOr:
					case SpvOpAtomicXor:
						return LowerAtomic(instruction.m_opcode, id, resultType, args);

					default:
						return Unsupported(instruction);
				}
			}

			llvm::Value* InsertAt(llvm::Value* composite, std::span<const uint32_t> indices, llvm::Value* object)
			{
				if (indices.empty())
					return object;
				if (composite->getType()->isVectorTy())
					return m_builder.CreateInsertElement(composite, object, indices[0]);
				llvm::Value* inner = m_builder.CreateExtractValue(composite, {indices[0]});
				return m_builder.CreateInsertValue(composite, InsertAt(inner, indices.subspan(1), object), {indices[0]});
			}

			bool LowerCompositeConstruct(uint32_t id, llvm::Type* resultType, std::span<const uint32_t> args)
			{
				llvm::Value* result = llvm::PoisonValue::get(resultType);
				if (resultType->isVectorTy())
				{
					uint64_t component = 0;
					for (const uint32_t constituent : args)
					{
						llvm::Value* value = Value(constituent);
						if (!value)
							return false;
						if (auto* vectorType = llvm::dyn_cast<llvm::FixedVectorType>(value->getType()))
						{
							for (unsigned i = 0; i < vectorType->getNumElements(); ++i)
								result = m_builder.CreateInsertElement(result, m_builder.CreateExtractElement(value, i), component++);
						}
						else
							result = m_builder.CreateInsertElement(result, value, component++);
					}
					return Set(id, result);
				}
				for (size_t i = 0; i < args.size(); ++i)
				{
					llvm::Value* value = Value(args[i]);
					if (!value)
						return false;
					result = m_builder.CreateInsertValue(result, value, {static_cast<unsigned>(i)});
				}
				return Set(id, result);
			}

			llvm::SyncScope::ID SyncScope(uint32_t scope)
			{
				switch (scope)
				{
					case SpvScopeInvocation:
						return llvm::SyncScope::SingleThread;
					case SpvScopeSubgroup:
						return m_context.getOrInsertSyncScopeID("wavefront");
					case SpvScopeWorkgroup:
						return m_context.getOrInsertSyncScopeID("workgroup");
					default:
						return m_context.getOrInsertSyncScopeID("agent");
				}
			}

			bool LowerBarrier(uint32_t executionScopeId, uint32_t memoryScopeId, uint32_t semanticsId, bool control)
			{
				const std::optional<uint64_t> memoryScope = ConstantValue(memoryScopeId);
				const std::optional<uint64_t> semantics = ConstantValue(semanticsId);
				const std::optional<uint64_t> executionScope = control ? ConstantValue(executionScopeId) : std::optional<uint64_t>(0);
				if (!memoryScope || !semantics || !executionScope)
					return Fail("barrier with non-constant scope or semantics");
				const bool fence = (*semantics & ~uint64_t{0x1F}) != 0;
				if (fence)
					m_builder.CreateFence(llvm::AtomicOrdering::Release, SyncScope(static_cast<uint32_t>(*memoryScope)));
				if (control && *executionScope == SpvScopeWorkgroup)
					m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_s_barrier, {}, {});
				if (fence)
					m_builder.CreateFence(llvm::AtomicOrdering::Acquire, SyncScope(static_cast<uint32_t>(*memoryScope)));
				return true;
			}

			struct ImageShape
			{
				const char* name;
				uint32_t coordinates;
				uint32_t gradients;
				bool arrayed;
			};

			const Type* ImageTypeOf(uint32_t valueId)
			{
				const Type* type = TypeOf(ResultType(valueId));
				if (type && type->kind == Type::Kind::SampledImage)
					type = TypeOf(type->element);
				if (!type || type->kind != Type::Kind::Image)
				{
					Fail("image instruction on a value that isn't an image");
					return nullptr;
				}
				return type;
			}

			std::optional<ImageShape> ShapeOf(const Type& image)
			{
				switch (image.dim)
				{
					case SpvDim1D:
						return ImageShape{image.arrayed ? "1darray" : "1d", image.arrayed ? 2u : 1u, 1, image.arrayed};
					case SpvDim2D:
					case SpvDimRect:
						if (image.multisampled)
							return ImageShape{image.arrayed ? "2darraymsaa" : "2dmsaa", image.arrayed ? 3u : 2u, 2, image.arrayed};
						return ImageShape{image.arrayed ? "2darray" : "2d", image.arrayed ? 3u : 2u, 2, image.arrayed};
					case SpvDim3D:
						return ImageShape{"3d", 3, 3, false};
					default:
						Fail(std::format("image dimension {} not supported yet", image.dim));
						return std::nullopt;
				}
			}

			std::vector<llvm::Value*> Components(llvm::Value* value, uint32_t count, llvm::Type* type)
			{
				std::vector<llvm::Value*> result;
				auto* vectorType = llvm::dyn_cast<llvm::FixedVectorType>(value->getType());
				for (uint32_t i = 0; i < count; ++i)
				{
					llvm::Value* component = nullptr;
					if (vectorType && i < vectorType->getNumElements())
						component = m_builder.CreateExtractElement(value, i);
					else if (!vectorType && i == 0)
						component = value;
					else
						component = llvm::Constant::getNullValue(type);
					if (component->getType() != type)
						component = m_builder.CreateBitCast(component, type);
					result.push_back(component);
				}
				return result;
			}

			llvm::Value* PackOffset(llvm::Value* offset)
			{
				std::vector<llvm::Value*> components = Components(offset, 3, m_builder.getInt32Ty());
				llvm::Value* packed = m_builder.getInt32(0);
				for (uint32_t i = 0; i < 3; ++i)
					packed = m_builder.CreateOr(packed, m_builder.CreateShl(m_builder.CreateAnd(components[i], 0x3F), 8 * i));
				return packed;
			}

			llvm::Value* CallImageIntrinsic(const std::string& name, llvm::ArrayRef<llvm::Type*> overloads, llvm::ArrayRef<llvm::Value*> callArgs)
			{
				const llvm::Intrinsic::ID intrinsic = llvm::Function::lookupIntrinsicID(name);
				if (intrinsic == llvm::Intrinsic::not_intrinsic)
				{
					Fail("no intrinsic " + name);
					return nullptr;
				}
				return m_builder.CreateCall(llvm::Intrinsic::getDeclaration(m_module.get(), intrinsic, overloads), callArgs);
			}

			llvm::Value* ToResultType(llvm::Value* value, llvm::Type* resultType)
			{
				auto* vectorType = llvm::dyn_cast<llvm::FixedVectorType>(resultType);
				llvm::Type* scalar = vectorType ? vectorType->getElementType() : resultType;
				const unsigned count = vectorType ? vectorType->getNumElements() : 1;
				llvm::Value* result = vectorType ? static_cast<llvm::Value*>(llvm::PoisonValue::get(resultType)) : nullptr;
				for (unsigned i = 0; i < count; ++i)
				{
					llvm::Value* component = value->getType()->isVectorTy() ? m_builder.CreateExtractElement(value, i) : value;
					if (component->getType() != scalar)
						component = m_builder.CreateBitCast(component, scalar);
					if (!vectorType)
						return component;
					result = m_builder.CreateInsertElement(result, component, uint64_t{i});
				}
				return result;
			}

			bool LowerImageSample(uint32_t opcode, uint32_t id, llvm::Type* resultType, std::span<const uint32_t> args)
			{
				const bool dref = opcode == SpvOpImageSampleDrefImplicitLod || opcode == SpvOpImageSampleDrefExplicitLod;
				llvm::Value* sampledImage = Value(args[0]);
				llvm::Value* coordinate = Value(args[1]);
				const Type* image = ImageTypeOf(args[0]);
				if (!sampledImage || !coordinate || !image)
					return false;
				const std::optional<ImageShape> shape = ShapeOf(*image);
				if (!shape)
					return false;

				size_t next = 2;
				llvm::Value* zcompare = dref ? Value(args[next++]) : nullptr;
				const uint32_t mask = next < args.size() ? args[next++] : 0;
				llvm::Value* bias = nullptr;
				llvm::Value* lod = nullptr;
				llvm::Value* dx = nullptr;
				llvm::Value* dy = nullptr;
				llvm::Value* offset = nullptr;
				if (mask & SpvImageOperandsBiasMask)
					bias = Value(args[next++]);
				if (mask & SpvImageOperandsLodMask)
					lod = Value(args[next++]);
				if (mask & SpvImageOperandsGradMask)
				{
					dx = Value(args[next++]);
					dy = Value(args[next++]);
				}
				if (mask & (SpvImageOperandsConstOffsetMask | SpvImageOperandsOffsetMask))
					offset = PackOffset(Value(args[next++]));
				if (mask & (SpvImageOperandsConstOffsetsMask | SpvImageOperandsSampleMask | SpvImageOperandsMinLodMask))
					return Fail("image sample operand not supported yet");

				std::string name = "llvm.amdgcn.image.sample";
				if (dref)
					name += ".c";
				if (dx)
					name += ".d";
				else if (lod)
					name += ".l";
				else if (bias)
					name += ".b";
				else if (!m_fragmentStage)
					name += ".lz";
				if (offset)
					name += ".o";
				name += std::string(".") + shape->name;

				llvm::Type* floatType = m_builder.getFloatTy();
				std::vector<llvm::Value*> callArgs{m_builder.getInt32(dref ? 1 : 0xF)};
				if (offset)
					callArgs.push_back(offset);
				if (bias)
					callArgs.push_back(bias);
				if (zcompare)
					callArgs.push_back(zcompare);
				if (dx)
				{
					for (llvm::Value* component : Components(dx, shape->gradients, floatType))
						callArgs.push_back(component);
					for (llvm::Value* component : Components(dy, shape->gradients, floatType))
						callArgs.push_back(component);
				}
				std::vector<llvm::Value*> coordinates = Components(coordinate, shape->coordinates, floatType);
				if (shape->arrayed)
					coordinates.back() = m_builder.CreateUnaryIntrinsic(llvm::Intrinsic::roundeven, coordinates.back());
				callArgs.insert(callArgs.end(), coordinates.begin(), coordinates.end());
				if (lod && !dx)
					callArgs.push_back(lod);
				callArgs.push_back(m_builder.CreateExtractValue(sampledImage, {0}));
				callArgs.push_back(m_builder.CreateExtractValue(sampledImage, {1}));
				callArgs.push_back(m_builder.getFalse());
				callArgs.push_back(m_builder.getInt32(0));
				callArgs.push_back(m_builder.getInt32(0));

				llvm::Type* returnType = dref ? floatType : static_cast<llvm::Type*>(llvm::FixedVectorType::get(floatType, 4));
				std::vector<llvm::Type*> overloads{returnType};
				if (dx)
					overloads.push_back(floatType);
				overloads.push_back(floatType);
				llvm::Value* result = CallImageIntrinsic(name, overloads, callArgs);
				return result && Set(id, ToResultType(result, resultType));
			}

			std::vector<llvm::Value*> IntegerCoordinates(llvm::Value* coordinate, const ImageShape& shape, llvm::Value* sample)
			{
				const bool msaa = std::string_view(shape.name).find("msaa") != std::string_view::npos;
				std::vector<llvm::Value*> coordinates = Components(coordinate, shape.coordinates, m_builder.getInt32Ty());
				if (msaa)
					coordinates.push_back(sample ? sample : m_builder.getInt32(0));
				return coordinates;
			}

			bool LowerImageLoad(uint32_t id, llvm::Type* resultType, std::span<const uint32_t> args)
			{
				llvm::Value* image = Value(args[0]);
				llvm::Value* coordinate = Value(args[1]);
				const Type* imageType = ImageTypeOf(args[0]);
				if (!image || !coordinate || !imageType)
					return false;
				const std::optional<ImageShape> shape = ShapeOf(*imageType);
				if (!shape)
					return false;
				size_t next = 2;
				const uint32_t mask = next < args.size() ? args[next++] : 0;
				llvm::Value* lod = nullptr;
				llvm::Value* sample = nullptr;
				if (mask & SpvImageOperandsLodMask)
					lod = Value(args[next++]);
				if (mask & SpvImageOperandsSampleMask)
					sample = Value(args[next++]);
				if (mask & (SpvImageOperandsBiasMask | SpvImageOperandsGradMask | SpvImageOperandsConstOffsetMask | SpvImageOperandsOffsetMask |
							SpvImageOperandsConstOffsetsMask | SpvImageOperandsMinLodMask))
					return Fail("image load operand not supported yet");

				std::vector<llvm::Value*> callArgs{m_builder.getInt32(0xF)};
				std::vector<llvm::Value*> coordinates = IntegerCoordinates(coordinate, *shape, sample);
				callArgs.insert(callArgs.end(), coordinates.begin(), coordinates.end());
				if (lod)
					callArgs.push_back(m_builder.CreateSExtOrTrunc(lod, m_builder.getInt32Ty()));
				callArgs.push_back(image->getType()->isStructTy() ? m_builder.CreateExtractValue(image, {0}) : image);
				callArgs.push_back(m_builder.getInt32(0));
				callArgs.push_back(m_builder.getInt32(0));
				const std::string name = std::string(lod ? "llvm.amdgcn.image.load.mip." : "llvm.amdgcn.image.load.") + shape->name;
				llvm::Type* returnType = llvm::FixedVectorType::get(m_builder.getFloatTy(), 4);
				llvm::Value* result = CallImageIntrinsic(name, {returnType, m_builder.getInt32Ty()}, callArgs);
				return result && Set(id, ToResultType(result, resultType));
			}

			bool LowerImageStore(std::span<const uint32_t> args)
			{
				llvm::Value* image = Value(args[0]);
				llvm::Value* coordinate = Value(args[1]);
				llvm::Value* texel = Value(args[2]);
				const Type* imageType = ImageTypeOf(args[0]);
				if (!image || !coordinate || !texel || !imageType)
					return false;
				const std::optional<ImageShape> shape = ShapeOf(*imageType);
				if (!shape)
					return false;
				llvm::Value* sample = nullptr;
				if (args.size() > 3 && (args[3] & SpvImageOperandsSampleMask))
					sample = Value(args[4]);
				llvm::Type* dataType = llvm::FixedVectorType::get(m_builder.getFloatTy(), 4);
				llvm::Value* data = llvm::PoisonValue::get(dataType);
				std::vector<llvm::Value*> components = Components(texel, 4, m_builder.getFloatTy());
				for (uint32_t i = 0; i < 4; ++i)
					data = m_builder.CreateInsertElement(data, components[i], uint64_t{i});
				std::vector<llvm::Value*> callArgs{data, m_builder.getInt32(0xF)};
				std::vector<llvm::Value*> coordinates = IntegerCoordinates(coordinate, *shape, sample);
				callArgs.insert(callArgs.end(), coordinates.begin(), coordinates.end());
				callArgs.push_back(image);
				callArgs.push_back(m_builder.getInt32(0));
				callArgs.push_back(m_builder.getInt32(0));
				return CallImageIntrinsic(std::string("llvm.amdgcn.image.store.") + shape->name, {dataType, m_builder.getInt32Ty()}, callArgs) != nullptr;
			}

			bool LowerImageQuery(uint32_t opcode, uint32_t id, llvm::Type* resultType, std::span<const uint32_t> args)
			{
				llvm::Value* image = Value(args[0]);
				const Type* imageType = ImageTypeOf(args[0]);
				if (!image || !imageType)
					return false;
				const std::optional<ImageShape> shape = ShapeOf(*imageType);
				if (!shape)
					return false;
				llvm::Value* lod = m_builder.getInt32(0);
				if (opcode == SpvOpImageQuerySizeLod)
				{
					lod = Value(args[1]);
					if (!lod)
						return false;
					lod = m_builder.CreateSExtOrTrunc(lod, m_builder.getInt32Ty());
				}
				if (image->getType()->isStructTy())
					image = m_builder.CreateExtractValue(image, {0});
				llvm::Type* returnType = llvm::FixedVectorType::get(m_builder.getFloatTy(), 4);
				llvm::Value* info = CallImageIntrinsic(std::string("llvm.amdgcn.image.getresinfo.") + shape->name, {returnType, m_builder.getInt32Ty()},
													   {m_builder.getInt32(0xF), lod, image, m_builder.getInt32(0), m_builder.getInt32(0)});
				if (!info)
					return false;
				info = m_builder.CreateBitCast(info, llvm::FixedVectorType::get(m_builder.getInt32Ty(), 4));
				if (opcode == SpvOpImageQueryLevels)
					return Set(id, m_builder.CreateExtractElement(info, uint64_t{3}));
				if (imageType->dim == SpvDim1D && imageType->arrayed)
					info = m_builder.CreateInsertElement(info, m_builder.CreateExtractElement(info, uint64_t{2}), uint64_t{1});
				return Set(id, ToResultType(info, resultType));
			}

			std::optional<llvm::AtomicOrdering> AtomicOrdering(uint32_t semanticsId)
			{
				const std::optional<uint64_t> semantics = ConstantValue(semanticsId);
				if (!semantics)
				{
					Fail("atomic with non-constant semantics");
					return std::nullopt;
				}
				const bool acquire = (*semantics & SpvMemorySemanticsAcquireMask) != 0;
				const bool release = (*semantics & SpvMemorySemanticsReleaseMask) != 0;
				if (*semantics & SpvMemorySemanticsSequentiallyConsistentMask)
					return llvm::AtomicOrdering::SequentiallyConsistent;
				if ((*semantics & SpvMemorySemanticsAcquireReleaseMask) || (acquire && release))
					return llvm::AtomicOrdering::AcquireRelease;
				if (acquire)
					return llvm::AtomicOrdering::Acquire;
				if (release)
					return llvm::AtomicOrdering::Release;
				return llvm::AtomicOrdering::Monotonic;
			}

			bool LowerAtomic(uint32_t opcode, uint32_t id, llvm::Type* resultType, std::span<const uint32_t> args)
			{
				llvm::Value* pointer = Value(args[0]);
				const std::optional<uint64_t> scopeValue = ConstantValue(args[1]);
				std::optional<llvm::AtomicOrdering> ordering = AtomicOrdering(args[2]);
				if (!pointer || !ordering)
					return false;
				if (!scopeValue)
					return Fail("atomic with a non-constant scope");
				const llvm::SyncScope::ID scope = SyncScope(static_cast<uint32_t>(*scopeValue));

				if (opcode == SpvOpAtomicLoad)
				{
					if (*ordering == llvm::AtomicOrdering::Release || *ordering == llvm::AtomicOrdering::AcquireRelease)
						ordering = llvm::AtomicOrdering::Acquire;
					llvm::LoadInst* load = m_builder.CreateAlignedLoad(resultType, pointer, llvm::Align(resultType->getScalarSizeInBits() / 8));
					load->setAtomic(*ordering, scope);
					return Set(id, load);
				}
				if (opcode == SpvOpAtomicStore)
				{
					llvm::Value* value = Value(args[3]);
					if (!value)
						return false;
					if (*ordering == llvm::AtomicOrdering::Acquire || *ordering == llvm::AtomicOrdering::AcquireRelease)
						ordering = llvm::AtomicOrdering::Release;
					llvm::StoreInst* store = m_builder.CreateAlignedStore(value, pointer, llvm::Align(value->getType()->getScalarSizeInBits() / 8));
					store->setAtomic(*ordering, scope);
					return true;
				}
				if (opcode == SpvOpAtomicCompareExchange)
				{
					llvm::Value* value = Value(args[4]);
					llvm::Value* comparator = Value(args[5]);
					if (!value || !comparator)
						return false;
					llvm::AtomicCmpXchgInst* exchange =
						m_builder.CreateAtomicCmpXchg(pointer, comparator, value, llvm::MaybeAlign(), *ordering,
													  llvm::AtomicCmpXchgInst::getStrongestFailureOrdering(*ordering), scope);
					return Set(id, m_builder.CreateExtractValue(exchange, {0}));
				}

				llvm::AtomicRMWInst::BinOp operation = llvm::AtomicRMWInst::Add;
				llvm::Value* value = nullptr;
				switch (opcode)
				{
					case SpvOpAtomicIIncrement:
						value = llvm::ConstantInt::get(resultType, 1);
						break;
					case SpvOpAtomicIDecrement:
						operation = llvm::AtomicRMWInst::Sub;
						value = llvm::ConstantInt::get(resultType, 1);
						break;
					case SpvOpAtomicExchange:
						operation = llvm::AtomicRMWInst::Xchg;
						break;
					case SpvOpAtomicISub:
						operation = llvm::AtomicRMWInst::Sub;
						break;
					case SpvOpAtomicSMin:
						operation = llvm::AtomicRMWInst::Min;
						break;
					case SpvOpAtomicUMin:
						operation = llvm::AtomicRMWInst::UMin;
						break;
					case SpvOpAtomicSMax:
						operation = llvm::AtomicRMWInst::Max;
						break;
					case SpvOpAtomicUMax:
						operation = llvm::AtomicRMWInst::UMax;
						break;
					case SpvOpAtomicAnd:
						operation = llvm::AtomicRMWInst::And;
						break;
					case SpvOpAtomicOr:
						operation = llvm::AtomicRMWInst::Or;
						break;
					case SpvOpAtomicXor:
						operation = llvm::AtomicRMWInst::Xor;
						break;
					default:
						break;
				}
				if (!value)
					value = Value(args[3]);
				if (!value)
					return false;
				return Set(id, m_builder.CreateAtomicRMW(operation, pointer, value, llvm::MaybeAlign(), *ordering, scope));
			}

			bool LowerExtInst(uint32_t id, llvm::Type* resultType, std::span<const uint32_t> args)
			{
				auto set = m_extInstSets.find(args[0]);
				if (set == m_extInstSets.end() || set->second != "GLSL.std.450")
					return Fail("extended instruction set not supported");
				const uint32_t number = args[1];
				std::vector<llvm::Value*> operands;
				for (size_t i = 2; i < args.size(); ++i)
				{
					llvm::Value* value = Value(args[i]);
					if (!value)
						return false;
					operands.push_back(value);
				}
				auto unaryIntrinsic = [&](llvm::Intrinsic::ID intrinsic)
				{
					return Set(id, m_builder.CreateUnaryIntrinsic(intrinsic, operands[0]));
				};
				auto binaryIntrinsic = [&](llvm::Intrinsic::ID intrinsic)
				{
					return Set(id, m_builder.CreateBinaryIntrinsic(intrinsic, operands[0], operands[1]));
				};
				auto clamp = [&](llvm::Intrinsic::ID maximum, llvm::Intrinsic::ID minimum)
				{
					return Set(id, m_builder.CreateBinaryIntrinsic(minimum, m_builder.CreateBinaryIntrinsic(maximum, operands[0], operands[1]), operands[2]));
				};
				llvm::Value* zero = llvm::Constant::getNullValue(resultType);
				switch (number)
				{
					case GLSLstd450Round:
						return unaryIntrinsic(llvm::Intrinsic::round);
					case GLSLstd450RoundEven:
						return unaryIntrinsic(llvm::Intrinsic::roundeven);
					case GLSLstd450Trunc:
						return unaryIntrinsic(llvm::Intrinsic::trunc);
					case GLSLstd450FAbs:
						return unaryIntrinsic(llvm::Intrinsic::fabs);
					case GLSLstd450SAbs:
						return Set(id, m_builder.CreateBinaryIntrinsic(llvm::Intrinsic::abs, operands[0], m_builder.getFalse()));
					case GLSLstd450FSign:
					{
						llvm::Value* one = llvm::ConstantFP::get(resultType, 1.0);
						llvm::Value* minusOne = llvm::ConstantFP::get(resultType, -1.0);
						llvm::Value* sign = m_builder.CreateSelect(m_builder.CreateFCmpOLT(operands[0], zero), minusOne, zero);
						return Set(id, m_builder.CreateSelect(m_builder.CreateFCmpOGT(operands[0], zero), one, sign));
					}
					case GLSLstd450SSign:
					{
						llvm::Value* one = llvm::ConstantInt::get(resultType, 1);
						llvm::Value* clamped = m_builder.CreateBinaryIntrinsic(llvm::Intrinsic::smin, operands[0], one);
						return Set(id, m_builder.CreateBinaryIntrinsic(llvm::Intrinsic::smax, clamped, llvm::Constant::getAllOnesValue(resultType)));
					}
					case GLSLstd450Floor:
						return unaryIntrinsic(llvm::Intrinsic::floor);
					case GLSLstd450Ceil:
						return unaryIntrinsic(llvm::Intrinsic::ceil);
					case GLSLstd450Fract:
						return Set(id, m_builder.CreateFSub(operands[0], m_builder.CreateUnaryIntrinsic(llvm::Intrinsic::floor, operands[0])));
					case GLSLstd450Sin:
						return unaryIntrinsic(llvm::Intrinsic::sin);
					case GLSLstd450Cos:
						return unaryIntrinsic(llvm::Intrinsic::cos);
					case GLSLstd450Pow:
						return binaryIntrinsic(llvm::Intrinsic::pow);
					case GLSLstd450Exp:
						return unaryIntrinsic(llvm::Intrinsic::exp);
					case GLSLstd450Log:
						return unaryIntrinsic(llvm::Intrinsic::log);
					case GLSLstd450Exp2:
						return unaryIntrinsic(llvm::Intrinsic::exp2);
					case GLSLstd450Log2:
						return unaryIntrinsic(llvm::Intrinsic::log2);
					case GLSLstd450Sqrt:
						return unaryIntrinsic(llvm::Intrinsic::sqrt);
					case GLSLstd450InverseSqrt:
						return Set(id, m_builder.CreateFDiv(llvm::ConstantFP::get(resultType, 1.0),
															m_builder.CreateUnaryIntrinsic(llvm::Intrinsic::sqrt, operands[0])));
					case GLSLstd450FMin:
					case GLSLstd450NMin:
						return binaryIntrinsic(llvm::Intrinsic::minnum);
					case GLSLstd450FMax:
					case GLSLstd450NMax:
						return binaryIntrinsic(llvm::Intrinsic::maxnum);
					case GLSLstd450UMin:
						return binaryIntrinsic(llvm::Intrinsic::umin);
					case GLSLstd450SMin:
						return binaryIntrinsic(llvm::Intrinsic::smin);
					case GLSLstd450UMax:
						return binaryIntrinsic(llvm::Intrinsic::umax);
					case GLSLstd450SMax:
						return binaryIntrinsic(llvm::Intrinsic::smax);
					case GLSLstd450FClamp:
					case GLSLstd450NClamp:
						return clamp(llvm::Intrinsic::maxnum, llvm::Intrinsic::minnum);
					case GLSLstd450UClamp:
						return clamp(llvm::Intrinsic::umax, llvm::Intrinsic::umin);
					case GLSLstd450SClamp:
						return clamp(llvm::Intrinsic::smax, llvm::Intrinsic::smin);
					case GLSLstd450FMix:
					{
						llvm::Value* weight = Splat(operands[2], resultType);
						return Set(id, m_builder.CreateFAdd(operands[0], m_builder.CreateFMul(m_builder.CreateFSub(operands[1], operands[0]), weight)));
					}
					case GLSLstd450Step:
					{
						llvm::Value* edge = Splat(operands[0], resultType);
						return Set(id, m_builder.CreateSelect(m_builder.CreateFCmpOLT(operands[1], edge), zero, llvm::ConstantFP::get(resultType, 1.0)));
					}
					case GLSLstd450Fma:
						return Set(id, m_builder.CreateIntrinsic(llvm::Intrinsic::fma, {resultType}, {operands[0], operands[1], operands[2]}));
					case GLSLstd450Length:
						return Set(id, operands[0]->getType()->isVectorTy()
										   ? m_builder.CreateUnaryIntrinsic(llvm::Intrinsic::sqrt, Dot(operands[0], operands[0]))
										   : m_builder.CreateUnaryIntrinsic(llvm::Intrinsic::fabs, operands[0]));
					case GLSLstd450Distance:
					{
						llvm::Value* difference = m_builder.CreateFSub(operands[0], operands[1]);
						return Set(id, difference->getType()->isVectorTy()
										   ? m_builder.CreateUnaryIntrinsic(llvm::Intrinsic::sqrt, Dot(difference, difference))
										   : m_builder.CreateUnaryIntrinsic(llvm::Intrinsic::fabs, difference));
					}
					case GLSLstd450Normalize:
					{
						if (!operands[0]->getType()->isVectorTy())
							return Set(id, m_builder.CreateSelect(m_builder.CreateFCmpOLT(operands[0], zero), llvm::ConstantFP::get(resultType, -1.0),
																  llvm::ConstantFP::get(resultType, 1.0)));
						llvm::Value* length = m_builder.CreateUnaryIntrinsic(llvm::Intrinsic::sqrt, Dot(operands[0], operands[0]));
						llvm::Value* inverse = m_builder.CreateFDiv(llvm::ConstantFP::get(length->getType(), 1.0), length);
						return Set(id, m_builder.CreateFMul(operands[0], Splat(inverse, resultType)));
					}
					case GLSLstd450Cross:
					{
						auto component = [&](llvm::Value* v, unsigned i)
						{
							return m_builder.CreateExtractElement(v, i);
						};
						llvm::Value* result = llvm::PoisonValue::get(resultType);
						for (unsigned i = 0; i < 3; ++i)
						{
							const unsigned j = (i + 1) % 3;
							const unsigned k = (i + 2) % 3;
							llvm::Value* value = m_builder.CreateFSub(m_builder.CreateFMul(component(operands[0], j), component(operands[1], k)),
																	  m_builder.CreateFMul(component(operands[0], k), component(operands[1], j)));
							result = m_builder.CreateInsertElement(result, value, i);
						}
						return Set(id, result);
					}
					case GLSLstd450FindILsb:
					{
						llvm::Value* bit = m_builder.CreateBinaryIntrinsic(llvm::Intrinsic::cttz, operands[0], m_builder.getFalse());
						return Set(id, m_builder.CreateSelect(m_builder.CreateICmpEQ(operands[0], zero), llvm::Constant::getAllOnesValue(resultType), bit));
					}
					case GLSLstd450FindUMsb:
					case GLSLstd450FindSMsb:
					{
						llvm::Value* value = operands[0];
						if (number == GLSLstd450FindSMsb)
							value = m_builder.CreateSelect(m_builder.CreateICmpSLT(value, zero), m_builder.CreateNot(value), value);
						const unsigned width = resultType->getScalarSizeInBits();
						llvm::Value* leading = m_builder.CreateBinaryIntrinsic(llvm::Intrinsic::ctlz, value, m_builder.getFalse());
						llvm::Value* bit = m_builder.CreateSub(llvm::ConstantInt::get(resultType, width - 1), leading);
						return Set(id, m_builder.CreateSelect(m_builder.CreateICmpEQ(value, zero), llvm::Constant::getAllOnesValue(resultType), bit));
					}
					default:
						return Fail(std::format("GLSL.std.450 instruction {} not supported yet", number));
				}
			}

			llvm::Value* ComputeBuiltIn(uint32_t builtIn, llvm::Value* const (&localId)[3], llvm::Value* const (&groupId)[3], const std::array<uint32_t, 3>& size,
										llvm::Value* numWorkgroups)
			{
				auto vector3 = [&](llvm::Value* x, llvm::Value* y, llvm::Value* z)
				{
					llvm::Value* result = llvm::PoisonValue::get(llvm::FixedVectorType::get(m_builder.getInt32Ty(), 3));
					result = m_builder.CreateInsertElement(result, x, uint64_t{0});
					result = m_builder.CreateInsertElement(result, y, uint64_t{1});
					return m_builder.CreateInsertElement(result, z, uint64_t{2});
				};
				switch (builtIn)
				{
					case SpvBuiltInLocalInvocationId:
						return vector3(localId[0], localId[1], localId[2]);
					case SpvBuiltInWorkgroupId:
						return vector3(groupId[0], groupId[1], groupId[2]);
					case SpvBuiltInGlobalInvocationId:
					{
						llvm::Value* global[3];
						for (int i = 0; i < 3; ++i)
							global[i] = m_builder.CreateAdd(m_builder.CreateMul(groupId[i], m_builder.getInt32(size[i])), localId[i]);
						return vector3(global[0], global[1], global[2]);
					}
					case SpvBuiltInNumWorkgroups:
						return numWorkgroups;
					case SpvBuiltInLocalInvocationIndex:
					{
						llvm::Value* index = m_builder.CreateAdd(m_builder.CreateMul(localId[2], m_builder.getInt32(size[1])), localId[1]);
						return m_builder.CreateAdd(m_builder.CreateMul(index, m_builder.getInt32(size[0])), localId[0]);
					}
					default:
						Fail(std::format("compute built-in {} not supported yet", SpvBuiltInToString(static_cast<SpvBuiltIn>(builtIn))));
						return nullptr;
				}
			}

			llvm::Value* TablePointer(llvm::Value* low)
			{
				llvm::Value* pc = m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_s_getpc, {}, {});
				llvm::Value* high = m_builder.CreateAnd(pc, m_builder.getInt64(0xFFFFFFFF00000000ull));
				llvm::Value* address = m_builder.CreateOr(high, m_builder.CreateZExt(low, m_builder.getInt64Ty()));
				return m_builder.CreateIntToPtr(address, llvm::PointerType::get(m_context, kConstantAddressSpace));
			}

			llvm::Value* Export(uint32_t target, uint32_t enable, llvm::ArrayRef<llvm::Value*> values, bool done, bool validMask)
			{
				llvm::Type* type = values[0]->getType();
				std::vector<llvm::Value*> callArgs{m_builder.getInt32(target), m_builder.getInt32(enable)};
				for (uint32_t i = 0; i < 4; ++i)
					callArgs.push_back(i < values.size() ? values[i] : llvm::PoisonValue::get(type));
				callArgs.push_back(m_builder.getInt1(done));
				callArgs.push_back(m_builder.getInt1(validMask));
				return m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_exp, {type}, callArgs);
			}

			llvm::Value* AsVec4Float(llvm::Value* value, float fill)
			{
				llvm::Type* floatType = m_builder.getFloatTy();
				std::vector<llvm::Value*> components = Components(value, 4, floatType);
				const unsigned count = value->getType()->isVectorTy() ? llvm::cast<llvm::FixedVectorType>(value->getType())->getNumElements() : 1;
				for (unsigned i = count; i < 4; ++i)
					components[i] = llvm::ConstantFP::get(floatType, i == 3 ? fill : 0.0f);
				llvm::Value* result = llvm::PoisonValue::get(llvm::FixedVectorType::get(floatType, 4));
				for (unsigned i = 0; i < 4; ++i)
					result = m_builder.CreateInsertElement(result, components[i], uint64_t{i});
				return result;
			}

			struct InterfaceSlot
			{
				uint64_t offset;
				uint32_t typeId;
			};

			bool InterfaceSlots(uint32_t typeId, uint64_t offset, std::vector<InterfaceSlot>& slots)
			{
				const Type* type = TypeOf(typeId);
				if (!type)
					return false;
				if (IsScalarOrVector(typeId))
				{
					slots.push_back({offset, typeId});
					return true;
				}
				if (type->kind == Type::Kind::Matrix && IsScalarOrVector(type->element))
				{
					const std::optional<Layout> column = LayoutOf(type->element);
					if (!column)
						return false;
					for (uint32_t i = 0; i < type->count; ++i)
						slots.push_back({offset + i * AlignUp(column->size, column->align), type->element});
					return true;
				}
				if (type->kind == Type::Kind::Array)
				{
					const std::optional<uint64_t> stride = ArrayStride(typeId);
					if (!stride)
						return false;
					for (uint32_t i = 0; i < type->count; ++i)
					{
						if (!InterfaceSlots(type->element, offset + i * *stride, slots))
							return false;
					}
					return true;
				}
				return false;
			}

			bool IsScalarOrVector(uint32_t typeId)
			{
				const Type* type = TypeOf(typeId);
				if (!type)
					return false;
				if (type->kind == Type::Kind::Vector)
					type = TypeOf(type->element);
				return type && (type->kind == Type::Kind::Int || type->kind == Type::Kind::Float) && type->width == 32;
			}

			bool IsVertexAttribute(const InvocationVariable& variable)
			{
				return variable.storageClass == SpvStorageClassInput && InEntryInterface(variable.id) && !Decoration(variable.id, SpvDecorationBuiltIn);
			}

			llvm::Value* FetchVertexAttribute(const VertexAttribute& attribute, uint32_t typeId, llvm::Value* table, llvm::Value* index)
			{
				const Type* type = TypeOf(typeId);
				const uint32_t count = type->kind == Type::Kind::Vector ? type->count : 1;
				const Type* component = type->kind == Type::Kind::Vector ? TypeOf(type->element) : type;
				llvm::LoadInst* descriptor = m_builder.CreateAlignedLoad(llvm::FixedVectorType::get(m_builder.getInt32Ty(), 4),
																		 OffsetPointer(table, uint64_t{attribute.binding} * 16), llvm::Align(16));
				descriptor->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(m_context, {}));
				llvm::Type* fetchType = attribute.channels == 1 ? static_cast<llvm::Type*>(m_builder.getInt32Ty())
																: llvm::FixedVectorType::get(m_builder.getInt32Ty(), attribute.channels);
				llvm::Value* fetched = m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_struct_tbuffer_load, {fetchType},
																 {descriptor, index, m_builder.getInt32(attribute.offset), m_builder.getInt32(0),
																  m_builder.getInt32(attribute.format), m_builder.getInt32(0)});
				const bool isFloat = component->kind == Type::Kind::Float;
				llvm::Type* elementType = isFloat ? m_builder.getFloatTy() : m_builder.getInt32Ty();
				llvm::Value* result = count == 1 ? nullptr : llvm::PoisonValue::get(llvm::FixedVectorType::get(elementType, count));
				for (uint32_t i = 0; i < count; ++i)
				{
					const uint8_t source = attribute.swizzle[i];
					llvm::Value* value = nullptr;
					if (source == kVertexSwizzleOne)
						value = m_builder.getInt32(isFloat ? 0x3F800000u : 1u);
					else if (source >= attribute.channels)
						value = m_builder.getInt32(0);
					else
						value = attribute.channels == 1 ? fetched : m_builder.CreateExtractElement(fetched, uint64_t{source});
					value = m_builder.CreateBitCast(value, elementType);
					if (count == 1)
						return value;
					result = m_builder.CreateInsertElement(result, value, uint64_t{i});
				}
				return result;
			}

			std::optional<uint32_t> PushConstantDwords()
			{
				uint32_t dwords = 0;
				for (const InvocationVariable& variable : m_invocationVariables)
				{
					if (variable.storageClass != SpvStorageClassPushConstant)
						continue;
					const std::optional<Layout> block = LayoutOf(variable.pointee);
					if (!block)
						return std::nullopt;
					dwords = std::max(dwords, static_cast<uint32_t>((block->size + 3) / 4));
				}
				return dwords;
			}

			bool CreateVertexEntry(std::span<const VertexAttribute> attributes, VertexLayout& layout)
			{
				const std::optional<uint32_t> pushConstantDwords = PushConstantDwords();
				if (!pushConstantDwords)
					return false;
				const bool vertexBuffers = std::any_of(m_invocationVariables.begin(), m_invocationVariables.end(),
													   [&](const InvocationVariable& variable)
													   { return IsVertexAttribute(variable); });
				if (vertexBuffers)
				{
					layout.userSgprs = 4;
					layout.vertexBufferTableUserData = 1;
					layout.baseVertexUserData = 2;
					layout.baseInstanceUserData = 3;
				}
				for (const uint32_t set : m_usedSets)
					layout.setTableUserData[set] = layout.userSgprs++;
				if (*pushConstantDwords > 0)
				{
					layout.pushConstantUserData = layout.userSgprs;
					layout.pushConstantDwords = *pushConstantDwords;
					layout.userSgprs += *pushConstantDwords;
				}
				if (layout.userSgprs > 32)
					return Fail("the vertex shader's descriptor sets and push constants don't fit in the user SGPRs");
				constexpr unsigned kSystemSgprs = 8;
				constexpr unsigned kVgprs = 9;
				const unsigned sgprs = kSystemSgprs + layout.userSgprs;
				std::vector<llvm::Type*> parameters(sgprs + kVgprs, m_builder.getInt32Ty());
				auto* entry = llvm::Function::Create(llvm::FunctionType::get(m_builder.getVoidTy(), parameters, false), llvm::GlobalValue::ExternalLinkage,
													 "_amdgpu_gs_main", *m_module);
				entry->setCallingConv(llvm::CallingConv::AMDGPU_GS);
				for (unsigned i = 0; i < sgprs; ++i)
					entry->addParamAttr(i, llvm::Attribute::InReg);

				m_currentFunction = entry;
				m_values.clear();
				m_builder.SetInsertPoint(llvm::BasicBlock::Create(m_context, "", entry));
				m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_init_exec, {}, {m_builder.getInt64(~uint64_t{0})});

				llvm::Value* lane = m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_mbcnt_lo, {}, {m_builder.getInt32(~0u), m_builder.getInt32(0)});
				lane = m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_mbcnt_hi, {}, {m_builder.getInt32(~0u), lane});
				llvm::Value* wave = m_builder.CreateAnd(m_builder.CreateLShr(entry->getArg(3), 24), 0xF);
				llvm::Value* thread = m_builder.CreateAdd(m_builder.CreateMul(wave, m_builder.getInt32(64)), lane);
				llvm::Value* vertexCount = m_builder.CreateAnd(m_builder.CreateLShr(entry->getArg(2), 12), 0x1FF);
				llvm::Value* primitiveCount = m_builder.CreateAnd(m_builder.CreateLShr(entry->getArg(2), 22), 0x1FF);
				llvm::Value* vertexIndex = m_builder.CreateAdd(entry->getArg(sgprs + 5), entry->getArg(kSystemSgprs + layout.baseVertexUserData));
				llvm::Value* instanceIndex = m_builder.CreateAdd(entry->getArg(sgprs + 8), entry->getArg(kSystemSgprs + layout.baseInstanceUserData));
				llvm::Value* vertexBufferTable = vertexBuffers ? TablePointer(entry->getArg(kSystemSgprs + layout.vertexBufferTableUserData)) : nullptr;
				std::vector<llvm::Value*> setTables;
				for (const uint32_t set : m_usedSets)
					setTables.push_back(TablePointer(entry->getArg(kSystemSgprs + layout.setTableUserData[set])));
				llvm::Value* activeVertex = m_builder.CreateICmpULT(thread, vertexCount);

				llvm::BasicBlock* shaderBlock = llvm::BasicBlock::Create(m_context, "", entry);
				llvm::BasicBlock* primitiveTest = llvm::BasicBlock::Create(m_context, "", entry);
				llvm::BasicBlock* primitiveBlock = llvm::BasicBlock::Create(m_context, "", entry);
				llvm::BasicBlock* afterPrimitive = llvm::BasicBlock::Create(m_context, "", entry);
				llvm::BasicBlock* vertexBlock = llvm::BasicBlock::Create(m_context, "", entry);
				llvm::BasicBlock* end = llvm::BasicBlock::Create(m_context, "", entry);
				m_builder.CreateCondBr(activeVertex, shaderBlock, primitiveTest);
				m_builder.SetInsertPoint(shaderBlock);

				std::vector<llvm::Value*> storage;
				for (const InvocationVariable& variable : m_invocationVariables)
				{
					const std::optional<Layout> variableLayout = LayoutOf(variable.pointee);
					if (!variableLayout)
						return false;
					llvm::AllocaInst* alloca = CreateAlloca(*variableLayout);
					storage.push_back(alloca);
					if (variable.storageClass == SpvStorageClassPushConstant)
					{
						for (uint32_t d = 0; d < (variableLayout->size + 3) / 4; ++d)
							m_builder.CreateAlignedStore(entry->getArg(kSystemSgprs + layout.pushConstantUserData + d), OffsetPointer(alloca, 4 * d),
														 llvm::Align(4));
						continue;
					}
					if (IsVertexAttribute(variable))
					{
						const std::optional<uint32_t> location = Decoration(variable.id, SpvDecorationLocation);
						if (!location)
							return Fail("vertex input without a location");
						std::vector<InterfaceSlot> slots;
						if (Decoration(variable.id, SpvDecorationComponent) || !InterfaceSlots(variable.pointee, 0, slots))
							return Fail("vertex inputs other than 32-bit scalars, vectors, matrices and arrays of them aren't supported yet");
						for (size_t k = 0; k < slots.size(); ++k)
						{
							const uint32_t slotLocation = *location + static_cast<uint32_t>(k);
							auto attribute =
								std::find_if(attributes.begin(), attributes.end(), [&](const VertexAttribute& a)
											 { return a.location == slotLocation; });
							if (attribute == attributes.end())
								return Fail(std::format("vertex input location {} has no attribute in the pipeline", slotLocation));
							if (attribute->perInstance)
								layout.instanceIndex = true;
							llvm::Value* value =
								FetchVertexAttribute(*attribute, slots[k].typeId, vertexBufferTable, attribute->perInstance ? instanceIndex : vertexIndex);
							if (!StoreMemory(OffsetPointer(alloca, slots[k].offset), slots[k].typeId, value, {}))
								return false;
						}
					}
					else if (variable.storageClass == SpvStorageClassInput && InEntryInterface(variable.id))
					{
						const std::optional<uint32_t> builtIn = Decoration(variable.id, SpvDecorationBuiltIn);
						llvm::Value* value = nullptr;
						if (*builtIn == SpvBuiltInVertexIndex)
							value = vertexIndex;
						else if (*builtIn == SpvBuiltInInstanceIndex)
						{
							value = instanceIndex;
							layout.instanceIndex = true;
						}
						else
							return Fail(std::format("vertex built-in {} not supported yet", SpvBuiltInToString(static_cast<SpvBuiltIn>(*builtIn))));
						if (!StoreMemory(alloca, variable.pointee, value, {}))
							return false;
					}
					else if (variable.initializer != 0)
					{
						llvm::Value* initializer = Value(variable.initializer);
						if (!initializer || !StoreMemory(alloca, variable.pointee, initializer, {}))
							return false;
					}
				}
				std::vector<llvm::Value*> arguments = storage;
				arguments.insert(arguments.end(), setTables.begin(), setTables.end());
				m_builder.CreateCall(m_functions.at(m_entryFunctionId), arguments);
				m_builder.CreateBr(primitiveTest);

				m_builder.SetInsertPoint(primitiveTest);
				m_builder.CreateCondBr(m_builder.CreateICmpULT(thread, primitiveCount), primitiveBlock, afterPrimitive);
				m_builder.SetInsertPoint(primitiveBlock);
				Export(20, 1, {entry->getArg(sgprs)}, true, false);
				m_builder.CreateBr(afterPrimitive);
				m_builder.SetInsertPoint(afterPrimitive);
				m_builder.CreateCondBr(activeVertex, vertexBlock, end);
				m_builder.SetInsertPoint(vertexBlock);

				llvm::Value* position = nullptr;
				std::vector<std::pair<uint32_t, llvm::Value*>> outputs;
				for (size_t i = 0; i < m_invocationVariables.size(); ++i)
				{
					const InvocationVariable& variable = m_invocationVariables[i];
					if (variable.storageClass != SpvStorageClassOutput || !InEntryInterface(variable.id))
						continue;
					if (const std::optional<uint32_t> builtIn = Decoration(variable.id, SpvDecorationBuiltIn))
					{
						if (*builtIn == SpvBuiltInPosition)
							position = LoadMemory(storage[i], variable.pointee, {});
						continue;
					}
					const Type* type = TypeOf(variable.pointee);
					if (type && type->kind == Type::Kind::Struct && m_spirv.m_memberDecorations.contains(variable.pointee))
					{
						for (uint32_t member = 0; member < type->members.size(); ++member)
						{
							if (MemberDecoration(variable.pointee, member, SpvDecorationBuiltIn) != SpvBuiltInPosition)
								continue;
							const std::optional<uint64_t> offset = MemberOffset(variable.pointee, member);
							if (!offset)
								return false;
							position = LoadMemory(OffsetPointer(storage[i], *offset), type->members[member], {});
						}
						continue;
					}
					const std::optional<uint32_t> location = Decoration(variable.id, SpvDecorationLocation);
					if (!location)
						continue;
					std::vector<InterfaceSlot> slots;
					if (Decoration(variable.id, SpvDecorationComponent) || !InterfaceSlots(variable.pointee, 0, slots))
						return Fail("vertex outputs other than 32-bit scalars, vectors, matrices and arrays of them aren't supported yet");
					for (size_t k = 0; k < slots.size(); ++k)
					{
						llvm::Value* value = LoadMemory(OffsetPointer(storage[i], slots[k].offset), slots[k].typeId, {});
						if (!value)
							return false;
						outputs.push_back({*location + static_cast<uint32_t>(k), AsVec4Float(value, 0.0f)});
					}
				}
				std::sort(outputs.begin(), outputs.end(), [](const auto& a, const auto& b)
						  { return a.first < b.first; });
				for (const auto& output : outputs)
					layout.outputLocations.push_back(output.first);
				if (!position)
					position = llvm::Constant::getNullValue(llvm::FixedVectorType::get(m_builder.getFloatTy(), 4));

				if (!outputs.empty())
				{
					llvm::LoadInst* ring = m_builder.CreateAlignedLoad(llvm::FixedVectorType::get(m_builder.getInt32Ty(), 4),
																	   OffsetPointer(TablePointer(entry->getArg(kSystemSgprs)), 0xB0), llvm::Align(16));
					ring->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(m_context, {}));
					llvm::Value* descriptor = ring;
					if (outputs.size() > 1)
					{
						llvm::Value* word = m_builder.CreateExtractElement(descriptor, uint64_t{1});
						word = m_builder.CreateOr(m_builder.CreateAnd(word, 0xC000FFFFu), static_cast<uint64_t>(16 * outputs.size()) << 16);
						descriptor = m_builder.CreateInsertElement(descriptor, word, uint64_t{1});
					}
					llvm::Value* waveOffset = m_builder.CreateAnd(m_builder.CreateShl(entry->getArg(5), 9), 0xFFFE00u);
					for (size_t parameter = 0; parameter < outputs.size(); ++parameter)
					{
						m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_struct_buffer_store, {outputs[parameter].second->getType()},
												  {outputs[parameter].second, descriptor, thread, m_builder.getInt32(static_cast<uint32_t>(16 * parameter)), waveOffset,
												   m_builder.getInt32(1)});
					}
					m_builder.CreateFence(llvm::AtomicOrdering::Release, m_context.getOrInsertSyncScopeID("agent"));
				}
				std::vector<llvm::Value*> coordinates = Components(position, 4, m_builder.getFloatTy());
				Export(12, 0xF, coordinates, true, false);
				m_builder.CreateBr(end);

				m_builder.SetInsertPoint(end);
				m_builder.CreateRetVoid();
				return true;
			}

			bool CreateFragmentEntry(std::span<const ColorExportFormat> colorExports, FragmentLayout& layout)
			{
				const std::optional<uint32_t> pushConstantDwords = PushConstantDwords();
				if (!pushConstantDwords)
					return false;
				layout.userSgprs = 1;
				for (const uint32_t set : m_usedSets)
					layout.setTableUserData[set] = layout.userSgprs++;
				if (*pushConstantDwords > 0)
				{
					layout.pushConstantUserData = layout.userSgprs;
					layout.pushConstantDwords = *pushConstantDwords;
					layout.userSgprs += *pushConstantDwords;
				}
				if (layout.userSgprs > 32)
					return Fail("the fragment shader's descriptor sets and push constants don't fit in the user SGPRs");

				llvm::Type* i32 = m_builder.getInt32Ty();
				llvm::Type* f32 = m_builder.getFloatTy();
				llvm::Type* v2f32 = llvm::FixedVectorType::get(f32, 2);
				llvm::Type* v3f32 = llvm::FixedVectorType::get(f32, 3);
				std::vector<llvm::Type*> parameters(layout.userSgprs + 1, i32);
				const unsigned firstVgpr = static_cast<unsigned>(parameters.size());
				const llvm::Type* inputs[] = {v2f32, v2f32, v2f32, v3f32, v2f32, v2f32, v2f32, f32, f32, f32, f32, f32, f32, i32, i32, i32};
				for (const llvm::Type* type : inputs)
					parameters.push_back(const_cast<llvm::Type*>(type));
				auto* entry = llvm::Function::Create(llvm::FunctionType::get(m_builder.getVoidTy(), parameters, false), llvm::GlobalValue::ExternalLinkage,
													 "_amdgpu_ps_main", *m_module);
				entry->setCallingConv(llvm::CallingConv::AMDGPU_PS);
				for (unsigned i = 0; i < firstVgpr; ++i)
					entry->addParamAttr(i, llvm::Attribute::InReg);

				m_currentFunction = entry;
				m_values.clear();
				m_builder.SetInsertPoint(llvm::BasicBlock::Create(m_context, "", entry));
				llvm::Value* primitiveMask = entry->getArg(layout.userSgprs);
				auto vgpr = [&](unsigned index)
				{ return entry->getArg(firstVgpr + index); };
				constexpr unsigned kPerspSample = 0, kPerspCenter = 1, kPerspCentroid = 2, kLinearSample = 4, kLinearCenter = 5, kLinearCentroid = 6;
				constexpr unsigned kPosX = 8, kFrontFace = 12;

				struct InputSlot
				{
					uint32_t location;
					size_t variable;
					InterfaceSlot slot;
				};
				std::vector<InputSlot> inputSlots;
				for (size_t i = 0; i < m_invocationVariables.size(); ++i)
				{
					const InvocationVariable& variable = m_invocationVariables[i];
					if (variable.storageClass != SpvStorageClassInput || !InEntryInterface(variable.id) || Decoration(variable.id, SpvDecorationBuiltIn))
						continue;
					const std::optional<uint32_t> location = Decoration(variable.id, SpvDecorationLocation);
					if (!location)
						return Fail("fragment input without a Location");
					std::vector<InterfaceSlot> slots;
					if (Decoration(variable.id, SpvDecorationComponent) || !InterfaceSlots(variable.pointee, 0, slots))
						return Fail("fragment inputs other than 32-bit scalars, vectors, matrices and arrays of them aren't supported yet");
					for (size_t k = 0; k < slots.size(); ++k)
						inputSlots.push_back({*location + static_cast<uint32_t>(k), i, slots[k]});
				}
				std::sort(inputSlots.begin(), inputSlots.end(), [](const InputSlot& a, const InputSlot& b)
						  { return a.location < b.location; });

				std::vector<llvm::Value*> storage;
				for (size_t i = 0; i < m_invocationVariables.size(); ++i)
				{
					const InvocationVariable& variable = m_invocationVariables[i];
					const std::optional<Layout> variableLayout = LayoutOf(variable.pointee);
					if (!variableLayout)
						return false;
					llvm::AllocaInst* alloca = CreateAlloca(*variableLayout);
					storage.push_back(alloca);
					if (variable.storageClass == SpvStorageClassPushConstant)
					{
						for (uint32_t d = 0; d < (variableLayout->size + 3) / 4; ++d)
							m_builder.CreateAlignedStore(entry->getArg(layout.pushConstantUserData + d), OffsetPointer(alloca, 4 * d), llvm::Align(4));
						continue;
					}
					if (variable.storageClass != SpvStorageClassInput || !InEntryInterface(variable.id))
					{
						if (variable.initializer != 0)
						{
							llvm::Value* initializer = Value(variable.initializer);
							if (!initializer || !StoreMemory(alloca, variable.pointee, initializer, {}))
								return false;
						}
						continue;
					}
					llvm::Value* value = nullptr;
					if (const std::optional<uint32_t> builtIn = Decoration(variable.id, SpvDecorationBuiltIn))
					{
						if (*builtIn == SpvBuiltInFragCoord)
						{
							llvm::Value* w = m_builder.CreateFDiv(llvm::ConstantFP::get(f32, 1.0), vgpr(kPosX + 3));
							llvm::Value* coordinate = llvm::PoisonValue::get(llvm::FixedVectorType::get(f32, 4));
							for (unsigned c = 0; c < 4; ++c)
								coordinate = m_builder.CreateInsertElement(coordinate, c == 3 ? w : vgpr(kPosX + c), uint64_t{c});
							value = coordinate;
						}
						else if (*builtIn == SpvBuiltInFrontFacing)
							value = m_builder.CreateICmpNE(m_builder.CreateBitCast(vgpr(kFrontFace), i32), m_builder.getInt32(0));
						else
							return Fail(std::format("fragment built-in {} not supported yet", SpvBuiltInToString(static_cast<SpvBuiltIn>(*builtIn))));
					}
					else
					{
						const bool linear = Decoration(variable.id, SpvDecorationNoPerspective).has_value();
						unsigned barycentric = linear ? kLinearCenter : kPerspCenter;
						if (Decoration(variable.id, SpvDecorationCentroid))
							barycentric = linear ? kLinearCentroid : kPerspCentroid;
						else if (Decoration(variable.id, SpvDecorationSample))
							barycentric = linear ? kLinearSample : kPerspSample;
						llvm::Value* baryI = m_builder.CreateExtractElement(vgpr(barycentric), uint64_t{0});
						llvm::Value* baryJ = m_builder.CreateExtractElement(vgpr(barycentric), uint64_t{1});
						for (size_t attribute = 0; attribute < inputSlots.size(); ++attribute)
						{
							if (inputSlots[attribute].variable != i)
								continue;
							const uint32_t typeId = inputSlots[attribute].slot.typeId;
							const Type* type = TypeOf(typeId);
							const Type* scalar = type->kind == Type::Kind::Vector ? TypeOf(type->element) : type;
							const unsigned count = type->kind == Type::Kind::Vector ? type->count : 1;
							const bool flat = Decoration(variable.id, SpvDecorationFlat).has_value() || scalar->kind == Type::Kind::Int;
							layout.inputs.push_back({inputSlots[attribute].location, flat});
							std::vector<llvm::Value*> components;
							for (unsigned c = 0; c < count; ++c)
							{
								llvm::Value* parameter = m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_lds_param_load, {},
																				   {m_builder.getInt32(c), m_builder.getInt32(static_cast<uint32_t>(attribute)), primitiveMask});
								llvm::Value* component =
									flat ? m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_interp_inreg_p10, {}, {parameter, llvm::ConstantFP::get(f32, 0.0), parameter})
										 : m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_interp_inreg_p2, {},
																	 {parameter, baryJ,
																	  m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_interp_inreg_p10, {}, {parameter, baryI, parameter})});
								components.push_back(component);
							}
							llvm::Type* valueType = ValueType(typeId);
							llvm::Value* slotValue = components[0];
							if (count > 1)
							{
								slotValue = llvm::PoisonValue::get(llvm::FixedVectorType::get(f32, count));
								for (unsigned c = 0; c < count; ++c)
									slotValue = m_builder.CreateInsertElement(slotValue, components[c], uint64_t{c});
							}
							if (slotValue->getType() != valueType)
								slotValue = m_builder.CreateBitCast(slotValue, valueType);
							if (!StoreMemory(OffsetPointer(alloca, inputSlots[attribute].slot.offset), typeId, slotValue, {}))
								return false;
						}
						continue;
					}
					if (!StoreMemory(alloca, variable.pointee, value, {}))
						return false;
				}
				std::sort(layout.inputs.begin(), layout.inputs.end(), [](const FragmentInput& a, const FragmentInput& b)
						  { return a.location < b.location; });

				for (const uint32_t set : m_usedSets)
					storage.push_back(TablePointer(entry->getArg(layout.setTableUserData[set])));
				m_builder.CreateCall(m_functions.at(m_entryFunctionId), storage);

				std::vector<std::pair<uint32_t, llvm::Value*>> outputs;
				for (size_t i = 0; i < m_invocationVariables.size(); ++i)
				{
					const InvocationVariable& variable = m_invocationVariables[i];
					if (variable.storageClass != SpvStorageClassOutput || !InEntryInterface(variable.id))
						continue;
					if (Decoration(variable.id, SpvDecorationBuiltIn))
						return Fail("fragment output built-ins aren't supported yet");
					const std::optional<uint32_t> location = Decoration(variable.id, SpvDecorationLocation);
					if (!location || *location >= 8 || !IsScalarOrVector(variable.pointee))
						return Fail("fragment outputs other than 32-bit scalars and vectors at locations 0-7 aren't supported yet");
					llvm::Value* value = LoadMemory(storage[i], variable.pointee, {});
					if (!value)
						return false;
					outputs.push_back({*location, AsVec4Float(value, 1.0f)});
				}
				std::sort(outputs.begin(), outputs.end(), [](const auto& a, const auto& b)
						  { return a.first < b.first; });
				constexpr uint32_t kColFormatFp16Abgr = 4;
				constexpr uint32_t kColFormat32Abgr = 9;
				for (size_t k = 0; k < outputs.size(); ++k)
				{
					const uint32_t location = outputs[k].first;
					const bool last = k + 1 == outputs.size();
					const ColorExportFormat format = location < colorExports.size() ? colorExports[location] : ColorExportFormat::Float16;
					std::vector<llvm::Value*> components = Components(outputs[k].second, 4, f32);
					if (format == ColorExportFormat::Float16)
					{
						llvm::Value* low = m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_cvt_pkrtz, {}, {components[0], components[1]});
						llvm::Value* high = m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_cvt_pkrtz, {}, {components[2], components[3]});
						Export(location, 0x3, {m_builder.CreateBitCast(low, f32), m_builder.CreateBitCast(high, f32)}, last, true);
						layout.colorFormat |= kColFormatFp16Abgr << (4 * location);
					}
					else
					{
						Export(location, 0xF, components, last, true);
						layout.colorFormat |= kColFormat32Abgr << (4 * location);
					}
					layout.shaderMask |= 0xFu << (4 * location);
				}
				if (outputs.empty())
					Export(9, 0, {llvm::PoisonValue::get(f32)}, true, true);
				m_builder.CreateRetVoid();
				return true;
			}

			bool CreateComputeEntry(ComputeLayout& layout)
			{
				for (const InvocationVariable& variable : m_invocationVariables)
				{
					if (variable.storageClass != SpvStorageClassPushConstant)
						continue;
					const std::optional<Layout> block = LayoutOf(variable.pointee);
					if (!block)
						return false;
					layout.pushConstantDwords = std::max(layout.pushConstantDwords, static_cast<uint32_t>((block->size + 3) / 4));
				}
				layout.userSgprs = kPushConstantUserData + layout.pushConstantDwords + static_cast<uint32_t>(m_usedSets.size());
				const bool needsNumWorkgroups = std::any_of(m_invocationVariables.begin(), m_invocationVariables.end(),
															[&](const InvocationVariable& variable)
															{
																return variable.storageClass == SpvStorageClassInput &&
																	   Decoration(variable.id, SpvDecorationBuiltIn) == SpvBuiltInNumWorkgroups &&
																	   std::find(m_entryInterface.begin(), m_entryInterface.end(), variable.id) != m_entryInterface.end();
															});
				if (needsNumWorkgroups)
				{
					layout.numWorkgroupsUserData = layout.userSgprs;
					layout.userSgprs += 2;
				}
				if (layout.userSgprs > kMaxComputeUserSgprs)
					return Fail(std::format("{} push constant dwords and {} descriptor sets don't fit in the user SGPRs", layout.pushConstantDwords,
											m_usedSets.size()));

				const unsigned sgprArguments = layout.userSgprs + 4;
				std::vector<llvm::Type*> parameters(sgprArguments + 1, m_builder.getInt32Ty());
				auto* entry = llvm::Function::Create(llvm::FunctionType::get(m_builder.getVoidTy(), parameters, false), llvm::GlobalValue::ExternalLinkage,
													 "_amdgpu_cs_main", *m_module);
				entry->setCallingConv(llvm::CallingConv::AMDGPU_CS);
				for (unsigned i = 0; i < sgprArguments; ++i)
					entry->addParamAttr(i, llvm::Attribute::InReg);
				const uint32_t groupSize = layout.localSize[0] * layout.localSize[1] * layout.localSize[2];
				entry->addFnAttr("amdgpu-flat-work-group-size", std::format("{},{}", groupSize, groupSize));

				m_currentFunction = entry;
				m_values.clear();
				m_builder.SetInsertPoint(llvm::BasicBlock::Create(m_context, "", entry));

				llvm::Value* packed = entry->getArg(sgprArguments);
				llvm::Value* const localId[3] = {
					m_builder.CreateAnd(packed, 0x3FF),
					m_builder.CreateAnd(m_builder.CreateLShr(packed, 10), 0x3FF),
					m_builder.CreateAnd(m_builder.CreateLShr(packed, 20), 0x3FF),
				};
				llvm::Value* const groupId[3] = {entry->getArg(layout.userSgprs), entry->getArg(layout.userSgprs + 1), entry->getArg(layout.userSgprs + 2)};

				llvm::Value* numWorkgroups = nullptr;
				if (layout.numWorkgroupsUserData != 0)
				{
					llvm::Value* low = m_builder.CreateZExt(entry->getArg(layout.numWorkgroupsUserData), m_builder.getInt64Ty());
					llvm::Value* high = m_builder.CreateZExt(entry->getArg(layout.numWorkgroupsUserData + 1), m_builder.getInt64Ty());
					llvm::Value* address = m_builder.CreateOr(m_builder.CreateShl(high, 32), low);
					llvm::LoadInst* load = m_builder.CreateAlignedLoad(llvm::FixedVectorType::get(m_builder.getInt32Ty(), 3),
																	   m_builder.CreateIntToPtr(address, llvm::PointerType::get(m_context, kConstantAddressSpace)),
																	   llvm::Align(4));
					load->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(m_context, {}));
					numWorkgroups = load;
				}

				std::vector<llvm::Value*> storage;
				for (const InvocationVariable& variable : m_invocationVariables)
				{
					const std::optional<Layout> variableLayout = LayoutOf(variable.pointee);
					if (!variableLayout)
						return false;
					llvm::AllocaInst* alloca = CreateAlloca(*variableLayout);
					storage.push_back(alloca);
					switch (variable.storageClass)
					{
						case SpvStorageClassPushConstant:
							for (uint32_t i = 0; i < (variableLayout->size + 3) / 4; ++i)
								m_builder.CreateAlignedStore(entry->getArg(kPushConstantUserData + i), OffsetPointer(alloca, 4 * i), llvm::Align(4));
							break;
						case SpvStorageClassInput:
						{
							if (std::find(m_entryInterface.begin(), m_entryInterface.end(), variable.id) == m_entryInterface.end())
								break;
							const std::optional<uint32_t> builtIn = Decoration(variable.id, SpvDecorationBuiltIn);
							if (!builtIn)
								return Fail("compute shader input without a BuiltIn decoration");
							llvm::Value* value = ComputeBuiltIn(*builtIn, localId, groupId, layout.localSize, numWorkgroups);
							if (!value || !StoreMemory(alloca, variable.pointee, value, {}))
								return false;
							break;
						}
						default:
							if (variable.initializer != 0)
							{
								llvm::Value* initializer = Value(variable.initializer);
								if (!initializer || !StoreMemory(alloca, variable.pointee, initializer, {}))
									return false;
							}
							break;
					}
				}
				if (!m_usedSets.empty())
				{
					llvm::Value* pc = m_builder.CreateIntrinsic(llvm::Intrinsic::amdgcn_s_getpc, {}, {});
					llvm::Value* high = m_builder.CreateAnd(pc, m_builder.getInt64(0xFFFFFFFF00000000ull));
					for (size_t i = 0; i < m_usedSets.size(); ++i)
					{
						const uint32_t userData = kPushConstantUserData + layout.pushConstantDwords + static_cast<uint32_t>(i);
						layout.setTableUserData[m_usedSets[i]] = userData;
						llvm::Value* address = m_builder.CreateOr(high, m_builder.CreateZExt(entry->getArg(userData), m_builder.getInt64Ty()));
						storage.push_back(m_builder.CreateIntToPtr(address, llvm::PointerType::get(m_context, kConstantAddressSpace)));
					}
				}
				m_builder.CreateCall(m_functions.at(m_entryFunctionId), storage);
				m_builder.CreateRetVoid();
				return true;
			}

			const spirv::Module& m_spirv;
			const CompileOptions& m_options;
			llvm::LLVMContext& m_context;
			std::unique_ptr<llvm::Module> m_module;
			llvm::IRBuilder<> m_builder;
			std::string m_error;

			const spirv::EntryPoint* m_entryPoint = nullptr;
			uint32_t m_entryFunctionId = 0;
			std::vector<uint32_t> m_entryInterface;
			std::unordered_map<uint32_t, Type> m_types;
			std::unordered_map<uint32_t, llvm::Type*> m_llvmTypes;
			std::unordered_map<uint32_t, std::string> m_extInstSets;
			std::unordered_map<uint32_t, llvm::Value*> m_globalValues;
			std::unordered_map<uint32_t, uint32_t> m_resultTypes;
			std::unordered_map<uint32_t, uint32_t> m_unsupportedVariables;
			std::unordered_map<uint32_t, PointerInfo> m_pointerInfo;
			std::vector<InvocationVariable> m_invocationVariables;
			std::unordered_map<uint32_t, DescriptorVariable> m_descriptorVariables;
			std::vector<uint32_t> m_usedSets;
			std::unordered_map<uint32_t, llvm::Function*> m_functions;

			llvm::Function* m_currentFunction = nullptr;
			llvm::Function* m_constantFolder = nullptr;
			bool m_fragmentStage = false;
			std::unordered_map<uint32_t, llvm::Value*> m_values;
			std::unordered_map<uint32_t, llvm::BasicBlock*> m_blocks;
			std::vector<PendingPhi> m_pendingPhis;
			std::unordered_map<uint32_t, llvm::Value*> m_setTables;
			std::unordered_map<uint32_t, llvm::Value*> m_bufferRecords;
		};
	} // namespace

	std::unique_ptr<llvm::Module> LowerComputeShader(const see::spirv::Module& spirv, const CompileOptions& options, llvm::LLVMContext& context,
													 llvm::TargetMachine& tm, ComputeLayout& layout, std::string& error)
	{
		Lowering lowering(spirv, options, context, tm);
		return lowering.LowerCompute(layout, error);
	}

	bool ScanDescriptors(const see::spirv::Module& spirv, const CompileOptions& options, llvm::LLVMContext& context, llvm::TargetMachine& tm,
						 std::vector<Descriptor>& descriptors, std::string& error)
	{
		Lowering lowering(spirv, options, context, tm);
		return lowering.ScanDescriptors(descriptors, error);
	}

	std::unique_ptr<llvm::Module> LowerVertexShader(const see::spirv::Module& spirv, const CompileOptions& options, std::span<const Descriptor> descriptorLayout,
													std::span<const VertexAttribute> attributes,
													llvm::LLVMContext& context, llvm::TargetMachine& tm, VertexLayout& layout, std::string& error)
	{
		Lowering lowering(spirv, options, context, tm);
		return lowering.LowerVertex(descriptorLayout, attributes, layout, error);
	}

	std::unique_ptr<llvm::Module> LowerFragmentShader(const see::spirv::Module& spirv, const CompileOptions& options,
													  std::span<const Descriptor> descriptorLayout, std::span<const ColorExportFormat> colorExports,
													  llvm::LLVMContext& context, llvm::TargetMachine& tm, FragmentLayout& layout, std::string& error)
	{
		Lowering lowering(spirv, options, context, tm);
		return lowering.LowerFragment(descriptorLayout, colorExports, layout, error);
	}
} // namespace amdcomp
