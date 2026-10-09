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
				Opaque
			};

			Kind kind = Kind::Opaque;
			uint32_t width = 0;
			bool isSigned = false;
			uint32_t element = 0;
			uint32_t count = 0;
			uint32_t storageClass = 0;
			std::vector<uint32_t> members;
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

			bool RunCompute(ComputeLayout& layout)
			{
				const spirv::EntryPoint* entryPoint = nullptr;
				for (const spirv::EntryPoint& candidate : m_spirv.m_entryPoints)
				{
					if (candidate.m_name == m_options.entryPoint && candidate.m_executionModel == SpvExecutionModelGLCompute)
						entryPoint = &candidate;
				}
				if (!entryPoint)
					return Fail(std::format("no GLCompute entry point named {}", m_options.entryPoint));
				m_entryFunctionId = entryPoint->m_functionId;

				if (!LowerGlobals() || !ReadLocalSize(*entryPoint, layout.localSize) || !AssignDescriptorOffsets(layout) || !DeclareFunctions())
					return false;
				for (const spirv::Function& function : m_spirv.m_functions)
				{
					if (!LowerFunction(function))
						return false;
				}
				return CreateComputeEntry(layout);
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
						case SpvOpTypeSampler:
						case SpvOpTypeSampledImage:
							m_types[id] = Type{.kind = Type::Kind::Opaque};
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
						default:
							return Unsupported(instruction);
					}
				}
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
				const bool storage = storageClass == SpvStorageClassStorageBuffer || Decoration(block, SpvDecorationBufferBlock).has_value();
				m_descriptorVariables[id] = DescriptorVariable{
					.set = Decoration(id, SpvDecorationDescriptorSet).value_or(0),
					.binding = Decoration(id, SpvDecorationBinding).value_or(0),
					.kind = storage ? DescriptorKind::StorageBuffer : DescriptorKind::UniformBuffer,
					.block = block,
					.count = isArray ? type->count : 1,
					.isArray = isArray,
				};
				return true;
			}

			bool AssignDescriptorOffsets(ComputeLayout& layout)
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
					variable->offset = tableBytes[variable->set];
					tableBytes[variable->set] += variable->count * kBufferDescriptorBytes;
					layout.descriptors.push_back(Descriptor{variable->set, variable->binding, variable->kind, variable->offset, variable->count});
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
						return Fail("OpKill outside a fragment shader");
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
				if (auto descriptor = m_descriptorVariables.find(args[0]); descriptor != m_descriptorVariables.end() && descriptor->second.isArray)
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
																	   Decoration(variable.id, SpvDecorationBuiltIn) == SpvBuiltInNumWorkgroups;
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

			uint32_t m_entryFunctionId = 0;
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
} // namespace amdcomp
