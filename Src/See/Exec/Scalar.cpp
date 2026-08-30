/**
 * @file Scalar.cpp
 * @brief Implementation of the scalar IR interpreter
 * @date 2026-08-24
 */

#include "See/Exec/Scalar.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#include "See/Defines.hpp"
#include <spirv/unified1/GLSL.std.450.h>
#include <spirv/unified1/spirv.h>

namespace see::exec
{
	float Value::GetFloat(cct::UInt32 index) const
	{
		float value;
		std::memcpy(&value, &Words()[index], sizeof(float));
		return value;
	}

	void Value::SetFloat(cct::UInt32 index, float value)
	{
		std::memcpy(&Words()[index], &value, sizeof(float));
	}

	cct::Int32 Value::GetInt32(cct::UInt32 index) const
	{
		cct::Int32 value;
		std::memcpy(&value, &Words()[index], sizeof(cct::Int32));
		return value;
	}

	void Value::SetInt32(cct::UInt32 index, cct::Int32 value)
	{
		std::memcpy(&Words()[index], &value, sizeof(cct::Int32));
	}

	bool Value::GetBool(cct::UInt32 index) const
	{
		return Words()[index] != 0;
	}

	void Value::SetBool(cct::UInt32 index, bool value)
	{
		Words()[index] = value ? 1u : 0u;
	}

	namespace
	{
		struct PointerInfo
		{
			cct::UInt32 m_baseVariable = 0;
			cct::UInt32 m_wordOffset = 0;
			cct::UInt32 m_pointeeType = 0; // ir type id of what this pointer currently points to
			cct::UInt32 m_matrixStride = 0; // words between matrix columns in memory, 0 when naturally packed
		};

		struct RuntimeState
		{
			const ir::Module& m_module;
			const PreparedModule& m_prepared;
			RegisterFile& m_registers;
			std::unordered_map<cct::UInt32, Value> m_variables;
			const std::unordered_map<cct::UInt32, ExternalBuffer>& m_externalBuffers;
			const std::unordered_map<cct::UInt32, ExternalImage>& m_externalImages;
			std::unordered_map<cct::UInt32, PointerInfo> m_pointers;
		};

		// `count` contiguous words starting at wordOffset within baseVariable's storage - an external
		// buffer if it's bound to one (e.g. a uniform block), otherwise the local Value register.
		// Returns nullptr if the variable is unknown or the range doesn't fit its storage.
		cct::UInt32* ResolveWords(RuntimeState& state, cct::UInt32 baseVariable, cct::UInt32 wordOffset, cct::UInt32 count)
		{
			if (auto externalIt = state.m_externalBuffers.find(baseVariable); externalIt != state.m_externalBuffers.end())
			{
				if (wordOffset + count > externalIt->second.m_wordCount)
					return nullptr;
				return externalIt->second.m_words + wordOffset;
			}

			auto variableIt = state.m_variables.find(baseVariable);
			if (variableIt == state.m_variables.end() || wordOffset + count > variableIt->second.m_bits.size())
				return nullptr;
			return variableIt->second.m_bits.data() + wordOffset;
		}

		std::optional<Value> MakeZeroValue(const ir::Module& module, cct::UInt32 typeId);
		bool ExecuteFunctionCall(RuntimeState& state, const ir::Instruction& instruction);

		std::optional<std::vector<MemberLayout>> GetStructMemberLayout(const ir::Module& module, const ir::Type& structType, cct::UInt32 structTypeId)
		{
			const std::vector<ir::MemberDecoration>* memberDecorations = nullptr;
			if (auto it = module.m_memberDecorations.find(structTypeId); it != module.m_memberDecorations.end())
				memberDecorations = &it->second;

			std::vector<MemberLayout> layout;
			cct::UInt32 packedOffset = 0;
			for (cct::UInt32 memberIndex = 0; memberIndex < structType.m_memberTypes.size(); ++memberIndex)
			{
				std::optional<Value> memberShape = MakeZeroValue(module, structType.m_memberTypes[memberIndex]);

				cct::UInt32 offset = packedOffset;
				cct::UInt32 matrixStride = 0;
				if (memberDecorations)
					for (const ir::MemberDecoration& decoration : *memberDecorations)
					{
						if (decoration.m_member != memberIndex || decoration.m_literals.empty())
							continue;

						if (decoration.m_kind == SpvDecorationOffset)
							offset = decoration.m_literals[0] / 4; // byte offset -> word offset
						else if (decoration.m_kind == SpvDecorationMatrixStride)
							matrixStride = decoration.m_literals[0] / 4;
					}

				cct::UInt32 wordCount = memberShape ? memberShape->WordCount() : 0;
				if (matrixStride != 0 && memberShape && memberShape->m_columns > 1)
					wordCount = matrixStride * memberShape->m_columns;

				layout.push_back(MemberLayout{.m_offset = offset, .m_wordCount = wordCount, .m_matrixStride = matrixStride});
				packedOffset = offset + wordCount;
			}
			return layout;
		}

		// Word stride of one array element: the ArrayStride decoration on the array type if the SPIR-V
		// carries one (required for std140/std430-backed arrays), otherwise the element's own natural size.
		cct::UInt32 GetArrayStride(const ir::Module& module, cct::UInt32 arrayTypeId, cct::UInt32 naturalElementWordCount)
		{
			if (auto it = module.m_decorations.find(arrayTypeId); it != module.m_decorations.end())
				for (const ir::Decoration& decoration : it->second)
					if (decoration.m_kind == SpvDecorationArrayStride && !decoration.m_literals.empty())
						return decoration.m_literals[0] / 4; // byte stride -> word stride

			return naturalElementWordCount;
		}

		std::optional<Value> MakeZeroValue(const ir::Module& module, cct::UInt32 typeId)
		{
			auto typeIt = module.m_types.find(typeId);
			if (typeIt == module.m_types.end())
				return std::nullopt;

			const ir::Type& type = typeIt->second;
			switch (type.m_kind)
			{
				case ir::TypeKind::Pointer:
					return MakeZeroValue(module, type.m_pointee);
				case ir::TypeKind::Scalar:
					return Value{.m_scalar = type.m_scalar};
				case ir::TypeKind::Vector:
					return Value{.m_scalar = type.m_scalar, .m_rows = type.m_componentCount};
				case ir::TypeKind::Matrix:
				{
					auto columnIt = module.m_types.find(type.m_pointee);
					if (columnIt == module.m_types.end())
						return std::nullopt;
					return Value{.m_scalar = type.m_scalar, .m_rows = columnIt->second.m_componentCount, .m_columns = type.m_componentCount};
				}
				case ir::TypeKind::Struct:
				{
					std::optional<std::vector<MemberLayout>> layout = GetStructMemberLayout(module, type, typeId);
					if (!layout)
						return std::nullopt;
					const cct::UInt32 totalWords = layout->empty() ? 0 : layout->back().m_offset + layout->back().m_wordCount;
					// A Value can only hold Value::MaxWords words - fine for locals (the usual case), but a struct
					// backed by an external buffer is addressed member-by-member instead of ever being
					// materialized whole, so this cap doesn't limit external (e.g. uniform block) size.
					if (totalWords > Value::MaxWords)
						return std::nullopt;
					return Value{.m_rows = totalWords, .m_structType = typeId};
				}
				case ir::TypeKind::Array:
				{
					if (type.m_arrayLength == 0)
						return std::nullopt; // runtime array: unbounded, addressed member-by-member via an external buffer

					std::optional<Value> elementShape = MakeZeroValue(module, type.m_pointee);
					if (!elementShape)
						return std::nullopt;

					const cct::UInt32 stride = GetArrayStride(module, typeId, elementShape->WordCount());
					const cct::UInt32 totalWords = stride * type.m_arrayLength;
					if (totalWords > Value::MaxWords)
						return std::nullopt;

					return Value{.m_rows = totalWords};
				}
				default:
					return std::nullopt;
			}
		}

		const Value* ResolveValue(const RuntimeState& state, cct::UInt32 id)
		{
			if (id >= state.m_module.m_valueRefs.size())
				return nullptr;

			const ir::ValueRef ref = state.m_module.m_valueRefs[id];
			switch (ir::GetKind(ref))
			{
				case ir::ValueKind::Ssa:
					return state.m_registers.Find(ir::GetSlot(ref));
				case ir::ValueKind::Constant:
					return state.m_prepared.GetConstant(ir::GetSlot(ref));
				case ir::ValueKind::Pointer:
				case ir::ValueKind::None:
					return nullptr;
			}
			return nullptr;
		}

		Value* SsaSlot(RuntimeState& state, cct::UInt32 id)
		{
			if (id >= state.m_module.m_valueRefs.size())
				return nullptr;

			const ir::ValueRef ref = state.m_module.m_valueRefs[id];
			if (ir::GetKind(ref) != ir::ValueKind::Ssa)
				return nullptr;

			return &state.m_registers.Emplace(ir::GetSlot(ref));
		}

		void ResetHeader(Value& value)
		{
			value.m_scalar = ir::ScalarKind::Float32;
			value.m_rows = 1;
			value.m_columns = 1;
			value.m_structType = 0;
			value.m_imageVariable = 0;
		}

		Value* EmplaceResult(RuntimeState& state, cct::UInt32 id)
		{
			Value* slot = SsaSlot(state, id);
			if (!slot)
				return nullptr;

			ResetHeader(*slot);
			return slot;
		}

		bool StoreSsa(RuntimeState& state, cct::UInt32 id, const Value& value)
		{
			Value* slot = SsaSlot(state, id);
			if (!slot)
				return false;

			*slot = value;
			return true;
		}

		void RegisterVariable(RuntimeState& state, cct::UInt32 id, cct::UInt32 pointerTypeId)
		{
			cct::UInt32 pointeeType = 0;
			if (auto typeIt = state.m_module.m_types.find(pointerTypeId); typeIt != state.m_module.m_types.end() && typeIt->second.m_kind == ir::TypeKind::Pointer)
				pointeeType = typeIt->second.m_pointee;

			state.m_pointers[id] = PointerInfo{.m_baseVariable = id, .m_wordOffset = 0, .m_pointeeType = pointeeType};
			if (state.m_externalBuffers.find(id) == state.m_externalBuffers.end() && state.m_variables.find(id) == state.m_variables.end())
				if (const Value* zero = state.m_prepared.GetZeroValue(pointerTypeId))
					state.m_variables[id] = *zero;
		}

		cct::UInt32 StridedMatrixColumnStride(const RuntimeState& state, const PointerInfo& pointer, const Value& shape)
		{
			if (pointer.m_matrixStride == 0 || pointer.m_matrixStride == shape.m_rows || shape.m_columns <= 1)
				return 0;

			auto typeIt = state.m_module.m_types.find(pointer.m_pointeeType);
			if (typeIt == state.m_module.m_types.end() || typeIt->second.m_kind != ir::TypeKind::Matrix)
				return 0;

			return pointer.m_matrixStride;
		}

		bool ExecuteLoad(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.empty())
				return false;

			auto pointerIt = state.m_pointers.find(instruction.m_args[0]);
			if (pointerIt == state.m_pointers.end())
				return false;

			// (Sampled) images aren't word-addressable data - loading one just produces a handle
			// naming the variable it came from, for a later ImageSample to resolve against the
			// bound ExternalImage.
			if (auto typeIt = state.m_module.m_types.find(pointerIt->second.m_pointeeType);
				typeIt != state.m_module.m_types.end() && (typeIt->second.m_kind == ir::TypeKind::Image || typeIt->second.m_kind == ir::TypeKind::SampledImage))
			{
				return StoreSsa(state, instruction.m_id, Value{.m_imageVariable = pointerIt->second.m_baseVariable});
			}

			const Value* resultShape = state.m_prepared.GetZeroValue(pointerIt->second.m_pointeeType);
			if (!resultShape)
				return false;

			std::optional<Value> result = *resultShape;

			const cct::UInt32 wordCount = result->WordCount();
			const cct::UInt32 columnStride = StridedMatrixColumnStride(state, pointerIt->second, *result);
			const cct::UInt32 spannedWords = columnStride != 0 ? columnStride * (result->m_columns - 1) + result->m_rows : wordCount;
			const cct::UInt32* source = ResolveWords(state, pointerIt->second.m_baseVariable, pointerIt->second.m_wordOffset, spannedWords);
			if (!source)
				return false;

			const std::span<cct::UInt32> words = result->Words();
			if (columnStride != 0)
			{
				for (cct::UInt32 column = 0; column < result->m_columns; ++column)
					for (cct::UInt32 row = 0; row < result->m_rows; ++row)
						words[column * result->m_rows + row] = source[column * columnStride + row];
			}
			else
			{
				for (cct::UInt32 i = 0; i < wordCount; ++i)
					words[i] = source[i];
			}

			return StoreSsa(state, instruction.m_id, *result);
		}

		bool ExecuteStore(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			auto pointerIt = state.m_pointers.find(instruction.m_args[0]);
			if (pointerIt == state.m_pointers.end())
				return false;

			const Value* value = ResolveValue(state, instruction.m_args[1]);
			if (!value)
				return false;

			const cct::UInt32 wordCount = value->WordCount();
			const cct::UInt32 columnStride = StridedMatrixColumnStride(state, pointerIt->second, *value);
			const cct::UInt32 spannedWords = columnStride != 0 ? columnStride * (value->m_columns - 1) + value->m_rows : wordCount;
			cct::UInt32* target = ResolveWords(state, pointerIt->second.m_baseVariable, pointerIt->second.m_wordOffset, spannedWords);
			if (!target)
				return false;

			const std::span<const cct::UInt32> words = value->Words();
			if (columnStride != 0)
			{
				for (cct::UInt32 column = 0; column < value->m_columns; ++column)
					for (cct::UInt32 row = 0; row < value->m_rows; ++row)
						target[column * columnStride + row] = words[column * value->m_rows + row];
			}
			else
			{
				for (cct::UInt32 i = 0; i < wordCount; ++i)
					target[i] = words[i];
			}

			return true;
		}

		bool ExecuteCopyMemory(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			auto targetIt = state.m_pointers.find(instruction.m_args[0]);
			auto sourceIt = state.m_pointers.find(instruction.m_args[1]);
			if (targetIt == state.m_pointers.end() || sourceIt == state.m_pointers.end())
				return false;

			const Value* shape = state.m_prepared.GetZeroValue(targetIt->second.m_pointeeType);
			if (!shape)
				return false;

			const cct::UInt32 wordCount = shape->WordCount();
			cct::UInt32* target = ResolveWords(state, targetIt->second.m_baseVariable, targetIt->second.m_wordOffset, wordCount);
			const cct::UInt32* source = ResolveWords(state, sourceIt->second.m_baseVariable, sourceIt->second.m_wordOffset, wordCount);
			if (!target || !source)
				return false;

			for (cct::UInt32 i = 0; i < wordCount && i < Value::MaxWords; ++i)
				target[i] = source[i];

			return true;
		}

		bool ExecuteAccessChain(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.empty())
				return false;

			auto basePointerIt = state.m_pointers.find(instruction.m_args[0]);
			if (basePointerIt == state.m_pointers.end())
				return false;

			PointerInfo info = basePointerIt->second;
			for (std::size_t argIndex = 1; argIndex < instruction.m_args.size(); ++argIndex)
			{
				const Value* index = ResolveValue(state, instruction.m_args[argIndex]);
				if (!index)
					return false;
				const cct::UInt32 indexValue = static_cast<cct::UInt32>(index->GetInt32());

				auto typeIt = state.m_module.m_types.find(info.m_pointeeType);
				if (typeIt != state.m_module.m_types.end() && typeIt->second.m_kind == ir::TypeKind::Struct)
				{
					const std::vector<MemberLayout>* layout = state.m_prepared.GetMemberLayout(info.m_pointeeType);
					if (!layout || indexValue >= layout->size())
						return false;
					info.m_wordOffset += (*layout)[indexValue].m_offset;
					info.m_matrixStride = (*layout)[indexValue].m_matrixStride;
					info.m_pointeeType = typeIt->second.m_memberTypes[indexValue];
				}
				else if (typeIt != state.m_module.m_types.end() && typeIt->second.m_kind == ir::TypeKind::Array)
				{
					const cct::UInt32 stride = state.m_prepared.GetArrayStride(info.m_pointeeType);
					if (stride == 0)
						return false;
					info.m_wordOffset += indexValue * stride;
					info.m_pointeeType = typeIt->second.m_pointee;
				}
				else if (typeIt != state.m_module.m_types.end() && typeIt->second.m_kind == ir::TypeKind::Matrix)
				{
					auto columnIt = state.m_module.m_types.find(typeIt->second.m_pointee);
					if (columnIt == state.m_module.m_types.end())
						return false;
					const cct::UInt32 columnStride = info.m_matrixStride != 0 ? info.m_matrixStride : columnIt->second.m_componentCount;
					info.m_wordOffset += indexValue * columnStride;
					info.m_matrixStride = 0;
					info.m_pointeeType = typeIt->second.m_pointee;
				}
				else if (typeIt != state.m_module.m_types.end() && typeIt->second.m_kind == ir::TypeKind::Vector)
				{
					info.m_wordOffset += indexValue;
					info.m_pointeeType = typeIt->second.m_pointee;
				}
				else
				{
					info.m_wordOffset += indexValue;
				}
			}

			state.m_pointers[instruction.m_id] = info;
			return true;
		}

		bool ExecuteCompositeConstruct(RuntimeState& state, const ir::Instruction& instruction)
		{
			const Value* resultShape = state.m_prepared.GetZeroValue(instruction.m_type);
			if (!resultShape)
				return false;

			std::optional<Value> result = *resultShape;

			// Each arg contributes its own word count, concatenated in order - covers scalars-into-vector,
			// vectors-into-matrix-columns, and mixed scalar/vector args (e.g. vec4 built from a vec3 + a float).
			const std::span<cct::UInt32> words = result->Words();
			const cct::UInt32 totalWords = result->WordCount();
			cct::UInt32 writeIndex = 0;
			for (cct::UInt32 argId : instruction.m_args)
			{
				const Value* component = ResolveValue(state, argId);
				if (!component)
					return false;

				const std::span<const cct::UInt32> componentWords = component->Words();
				for (cct::UInt32 i = 0; i < componentWords.size() && writeIndex < totalWords; ++i, ++writeIndex)
					words[writeIndex] = componentWords[i];
			}

			if (writeIndex != totalWords)
				return false;

			return StoreSsa(state, instruction.m_id, *result);
		}

		bool ExecuteCompositeExtract(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			const Value* composite = ResolveValue(state, instruction.m_args[0]);
			if (!composite)
				return false;

			Value current = *composite;
			for (std::size_t argIndex = 1; argIndex < instruction.m_args.size(); ++argIndex)
			{
				const cct::UInt32 index = instruction.m_args[argIndex];

				if (current.m_structType != 0)
				{
					std::optional<Value> member = ExtractStructMember(state.m_module, current, index);
					if (!member)
						return false;

					current = *member;
					continue;
				}

				if (current.m_columns > 1)
				{
					if (index >= current.m_columns)
						return false;

					Value column;
					column.m_scalar = current.m_scalar;
					column.m_rows = current.m_rows;
					for (cct::UInt32 word = 0; word < current.m_rows; ++word)
						column.Words()[word] = current.Words()[index * current.m_rows + word];

					current = column;
					continue;
				}

				if (index >= current.m_rows)
					return false;

				Value component;
				component.m_scalar = current.m_scalar;
				component.Words()[0] = current.Words()[index];
				current = component;
			}

			return StoreSsa(state, instruction.m_id, current);
		}

		bool ExecuteVectorExtractDynamic(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			const Value* vector = ResolveValue(state, instruction.m_args[0]);
			const Value* index = ResolveValue(state, instruction.m_args[1]);
			if (!vector || !index)
				return false;

			const cct::UInt32 indexValue = static_cast<cct::UInt32>(index->GetInt32());
			if (indexValue >= vector->WordCount())
				return false;

			Value* result = EmplaceResult(state, instruction.m_id);
			if (!result)
				return false;

			result->m_scalar = vector->m_scalar;
			result->Words()[0] = vector->Words()[indexValue];

			return true;
		}

		bool ExecuteVectorInsertDynamic(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 3)
				return false;

			const Value* vector = ResolveValue(state, instruction.m_args[0]);
			const Value* component = ResolveValue(state, instruction.m_args[1]);
			const Value* index = ResolveValue(state, instruction.m_args[2]);
			if (!vector || !component || !index)
				return false;

			const cct::UInt32 indexValue = static_cast<cct::UInt32>(index->GetInt32());
			if (indexValue >= vector->WordCount())
				return false;

			Value* result = SsaSlot(state, instruction.m_id);
			if (!result)
				return false;

			*result = *vector;
			result->Words()[indexValue] = component->Words()[0];

			return true;
		}

		static constexpr cct::UInt32 UndefinedShuffleComponent = 0xFFFFFFFF;

		bool ExecuteVectorShuffle(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			const Value* first = ResolveValue(state, instruction.m_args[0]);
			const Value* second = ResolveValue(state, instruction.m_args[1]);
			if (!first || !second || first->m_scalar != second->m_scalar)
				return false;

			const Value* resultShape = state.m_prepared.GetZeroValue(instruction.m_type);
			if (!resultShape)
				return false;

			std::optional<Value> result = *resultShape;

			const cct::UInt32 componentCount = static_cast<cct::UInt32>(instruction.m_args.size() - 2);
			if (componentCount != result->WordCount())
				return false;

			const cct::UInt32 firstWords = first->WordCount();
			const cct::UInt32 secondWords = second->WordCount();
			for (cct::UInt32 i = 0; i < componentCount; ++i)
			{
				const cct::UInt32 selector = instruction.m_args[i + 2];
				if (selector == UndefinedShuffleComponent)
					continue;

				if (selector < firstWords)
					result->Words()[i] = first->Words()[selector];
				else if (selector - firstWords < secondWords)
					result->Words()[i] = second->Words()[selector - firstWords];
				else
					return false;
			}

			return StoreSsa(state, instruction.m_id, *result);
		}

		bool ExecuteArithmetic(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			const Value* lhs = ResolveValue(state, instruction.m_args[0]);
			const Value* rhs = ResolveValue(state, instruction.m_args[1]);
			if (!lhs || !rhs || lhs->m_rows != rhs->m_rows || lhs->m_scalar != rhs->m_scalar)
				return false;

			Value* result = EmplaceResult(state, instruction.m_id);
			if (!result)
				return false;

			result->m_scalar = lhs->m_scalar;
			result->m_rows = lhs->m_rows;

			for (cct::UInt32 i = 0; i < result->m_rows; ++i)
			{
				if (result->m_scalar == ir::ScalarKind::Int32)
				{
					const cct::Int32 a = lhs->GetInt32(i);
					const cct::Int32 b = rhs->GetInt32(i);
					cct::Int32 r = 0;
					switch (instruction.m_op)
					{
						case ir::Op::Add:
							r = a + b;
							break;
						case ir::Op::Sub:
							r = a - b;
							break;
						case ir::Op::Mul:
							r = a * b;
							break;
						case ir::Op::Div:
							r = b != 0 ? a / b : 0;
							break;
						case ir::Op::Rem:
							r = b != 0 ? a % b : 0;
							break;
						case ir::Op::Mod:
							r = b != 0 ? ((a % b) + b) % b : 0;
							break;
						case ir::Op::UnsignedMod:
							r = b != 0 ? static_cast<cct::Int32>(static_cast<cct::UInt32>(a) % static_cast<cct::UInt32>(b)) : 0;
							break;
						case ir::Op::UnsignedDiv:
							r = b != 0 ? static_cast<cct::Int32>(static_cast<cct::UInt32>(a) / static_cast<cct::UInt32>(b)) : 0;
							break;
						case ir::Op::BitwiseAnd:
							r = a & b;
							break;
						case ir::Op::BitwiseOr:
							r = a | b;
							break;
						case ir::Op::BitwiseXor:
							r = a ^ b;
							break;
						case ir::Op::ShiftLeftLogical:
							r = static_cast<cct::Int32>(static_cast<cct::UInt32>(a) << (static_cast<cct::UInt32>(b) & 31u));
							break;
						case ir::Op::ShiftRightLogical:
							r = static_cast<cct::Int32>(static_cast<cct::UInt32>(a) >> (static_cast<cct::UInt32>(b) & 31u));
							break;
						case ir::Op::ShiftRightArithmetic:
							r = a >> (static_cast<cct::UInt32>(b) & 31u);
							break;
						default:
							return false;
					}
					result->SetInt32(i, r);
				}
				else
				{
					const float a = lhs->GetFloat(i);
					const float b = rhs->GetFloat(i);
					float r = 0.0f;
					switch (instruction.m_op)
					{
						case ir::Op::Add:
							r = a + b;
							break;
						case ir::Op::Sub:
							r = a - b;
							break;
						case ir::Op::Mul:
							r = a * b;
							break;
						case ir::Op::Div:
							r = b != 0.0f ? a / b : 0.0f;
							break;
						case ir::Op::Rem:
							r = b != 0.0f ? std::fmod(a, b) : 0.0f;
							break;
						case ir::Op::Mod:
							r = b != 0.0f ? a - b * std::floor(a / b) : 0.0f;
							break;
						default:
							return false;
					}
					result->SetFloat(i, r);
				}
			}

			return true;
		}

		bool ExecuteNegate(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.empty())
				return false;

			const Value* operand = ResolveValue(state, instruction.m_args[0]);
			if (!operand)
				return false;

			Value* result = SsaSlot(state, instruction.m_id);
			if (!result)
				return false;

			*result = *operand;
			for (cct::UInt32 i = 0; i < result->WordCount(); ++i)
			{
				if (result->m_scalar == ir::ScalarKind::Int32)
					result->SetInt32(i, -operand->GetInt32(i));
				else
					result->SetFloat(i, -operand->GetFloat(i));
			}

			return true;
		}

		cct::UInt32 ReverseBits(cct::UInt32 value)
		{
			value = ((value & 0x55555555u) << 1) | ((value >> 1) & 0x55555555u);
			value = ((value & 0x33333333u) << 2) | ((value >> 2) & 0x33333333u);
			value = ((value & 0x0F0F0F0Fu) << 4) | ((value >> 4) & 0x0F0F0F0Fu);
			value = ((value & 0x00FF00FFu) << 8) | ((value >> 8) & 0x00FF00FFu);
			return (value << 16) | (value >> 16);
		}

		bool ExecuteBitUnary(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.empty())
				return false;

			const Value* operand = ResolveValue(state, instruction.m_args[0]);
			if (!operand || operand->m_scalar != ir::ScalarKind::Int32)
				return false;

			Value* result = SsaSlot(state, instruction.m_id);
			if (!result)
				return false;

			*result = *operand;
			for (cct::UInt32 i = 0; i < result->WordCount(); ++i)
			{
				const cct::UInt32 bits = static_cast<cct::UInt32>(operand->GetInt32(i));
				switch (instruction.m_op)
				{
					case ir::Op::BitwiseNot:
						result->SetInt32(i, static_cast<cct::Int32>(~bits));
						break;
					case ir::Op::BitCount:
						result->SetInt32(i, static_cast<cct::Int32>(std::popcount(bits)));
						break;
					case ir::Op::BitReverse:
						result->SetInt32(i, static_cast<cct::Int32>(ReverseBits(bits)));
						break;
					default:
						return false;
				}
			}

			return true;
		}

		bool ExecuteBitField(RuntimeState& state, const ir::Instruction& instruction)
		{
			const bool isInsert = instruction.m_op == ir::Op::BitFieldInsert;
			if (instruction.m_args.size() < (isInsert ? 4u : 3u))
				return false;

			const Value* base = ResolveValue(state, instruction.m_args[0]);
			const Value* insert = isInsert ? ResolveValue(state, instruction.m_args[1]) : nullptr;
			const Value* offset = ResolveValue(state, instruction.m_args[isInsert ? 2 : 1]);
			const Value* count = ResolveValue(state, instruction.m_args[isInsert ? 3 : 2]);
			if (!base || !offset || !count || (isInsert && !insert))
				return false;

			const cct::UInt32 offsetValue = static_cast<cct::UInt32>(offset->GetInt32());
			const cct::UInt32 countValue = static_cast<cct::UInt32>(count->GetInt32());
			if (offsetValue > 32 || countValue > 32 || offsetValue + countValue > 32)
				return false;

			Value* result = SsaSlot(state, instruction.m_id);
			if (!result)
				return false;

			*result = *base;
			const cct::UInt32 componentCount = result->WordCount();

			if (countValue == 0)
			{
				if (!isInsert)
					for (cct::UInt32 i = 0; i < componentCount; ++i)
						result->SetInt32(i, 0);

				return true;
			}

			const cct::UInt32 mask = countValue == 32 ? 0xFFFFFFFFu : ((1u << countValue) - 1u);
			for (cct::UInt32 i = 0; i < componentCount; ++i)
			{
				const cct::UInt32 bits = static_cast<cct::UInt32>(base->GetInt32(i));
				cct::UInt32 value = 0;
				switch (instruction.m_op)
				{
					case ir::Op::BitFieldInsert:
						value = (bits & ~(mask << offsetValue)) | ((static_cast<cct::UInt32>(insert->GetInt32(i)) & mask) << offsetValue);
						break;
					case ir::Op::BitFieldUnsignedExtract:
						value = (bits >> offsetValue) & mask;
						break;
					case ir::Op::BitFieldSignedExtract:
						value = (bits >> offsetValue) & mask;
						if ((value & (1u << (countValue - 1u))) != 0)
							value |= ~mask;
						break;
					default:
						return false;
				}
				result->SetInt32(i, static_cast<cct::Int32>(value));
			}

			return true;
		}

		bool ExecuteBitcast(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.empty())
				return false;

			const Value* operand = ResolveValue(state, instruction.m_args[0]);
			if (!operand)
				return false;

			const Value* resultShape = state.m_prepared.GetZeroValue(instruction.m_type);
			if (!resultShape)
				return false;

			std::optional<Value> result = *resultShape;

			const cct::UInt32 componentCount = operand->WordCount();
			if (result->WordCount() != componentCount)
				return false;

			for (cct::UInt32 i = 0; i < componentCount; ++i)
				result->Words()[i] = operand->Words()[i];

			return StoreSsa(state, instruction.m_id, *result);
		}

		bool ExecuteConvert(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.empty())
				return false;

			const Value* operand = ResolveValue(state, instruction.m_args[0]);
			if (!operand)
				return false;

			Value* result = SsaSlot(state, instruction.m_id);
			if (!result)
				return false;

			*result = *operand;
			result->m_scalar = (instruction.m_op == ir::Op::ConvertSignedToFloat || instruction.m_op == ir::Op::ConvertUnsignedToFloat) ? ir::ScalarKind::Float32 : ir::ScalarKind::Int32;
			for (cct::UInt32 i = 0; i < result->WordCount(); ++i)
			{
				switch (instruction.m_op)
				{
					case ir::Op::ConvertSignedToFloat:
						result->SetFloat(i, static_cast<float>(operand->GetInt32(i)));
						break;
					case ir::Op::ConvertUnsignedToFloat:
						result->SetFloat(i, static_cast<float>(static_cast<cct::UInt32>(operand->GetInt32(i))));
						break;
					case ir::Op::ConvertFloatToSigned:
						result->SetInt32(i, static_cast<cct::Int32>(operand->GetFloat(i)));
						break;
					case ir::Op::ConvertFloatToUnsigned:
						result->SetInt32(i, static_cast<cct::Int32>(static_cast<cct::UInt32>(operand->GetFloat(i))));
						break;
					default:
						return false;
				}
			}

			return true;
		}

		bool ExecuteDot(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			const Value* lhs = ResolveValue(state, instruction.m_args[0]);
			const Value* rhs = ResolveValue(state, instruction.m_args[1]);
			if (!lhs || !rhs || lhs->m_rows != rhs->m_rows)
				return false;

			float sum = 0.0f;
			for (cct::UInt32 i = 0; i < lhs->m_rows; ++i)
				sum += lhs->GetFloat(i) * rhs->GetFloat(i);

			Value* result = EmplaceResult(state, instruction.m_id);
			if (!result)
				return false;

			result->SetFloat(0, sum);
			return true;
		}

		bool ExecuteMatrixTimesVector(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			const Value* matrix = ResolveValue(state, instruction.m_args[0]);
			const Value* vector = ResolveValue(state, instruction.m_args[1]);
			if (!matrix || !vector || matrix->m_columns != vector->m_rows)
				return false;

			Value* result = EmplaceResult(state, instruction.m_id);
			if (!result)
				return false;

			result->m_rows = matrix->m_rows;

			for (cct::UInt32 row = 0; row < matrix->m_rows; ++row)
			{
				float sum = 0.0f;
				for (cct::UInt32 col = 0; col < matrix->m_columns; ++col)
					sum += matrix->GetFloat(col * matrix->m_rows + row) * vector->GetFloat(col);
				result->SetFloat(row, sum);
			}

			return true;
		}

		bool ExecuteVectorTimesMatrix(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			const Value* vector = ResolveValue(state, instruction.m_args[0]);
			const Value* matrix = ResolveValue(state, instruction.m_args[1]);
			if (!vector || !matrix || vector->m_rows != matrix->m_rows)
				return false;

			Value* result = EmplaceResult(state, instruction.m_id);
			if (!result)
				return false;

			result->m_rows = matrix->m_columns;

			for (cct::UInt32 col = 0; col < matrix->m_columns; ++col)
			{
				float sum = 0.0f;
				for (cct::UInt32 row = 0; row < matrix->m_rows; ++row)
					sum += vector->GetFloat(row) * matrix->GetFloat(col * matrix->m_rows + row);
				result->SetFloat(col, sum);
			}

			return true;
		}

		bool ExecuteMatrixTimesMatrix(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			const Value* lhs = ResolveValue(state, instruction.m_args[0]);
			const Value* rhs = ResolveValue(state, instruction.m_args[1]);
			if (!lhs || !rhs || lhs->m_columns != rhs->m_rows)
				return false;

			Value* result = EmplaceResult(state, instruction.m_id);
			if (!result)
				return false;

			result->m_rows = lhs->m_rows;
			result->m_columns = rhs->m_columns;

			for (cct::UInt32 col = 0; col < result->m_columns; ++col)
			{
				for (cct::UInt32 row = 0; row < result->m_rows; ++row)
				{
					float sum = 0.0f;
					for (cct::UInt32 k = 0; k < lhs->m_columns; ++k)
						sum += lhs->GetFloat(k * lhs->m_rows + row) * rhs->GetFloat(col * rhs->m_rows + k);
					result->SetFloat(col * result->m_rows + row, sum);
				}
			}

			return true;
		}

		bool ExecuteVectorTimesScalar(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			const Value* composite = ResolveValue(state, instruction.m_args[0]);
			const Value* scalar = ResolveValue(state, instruction.m_args[1]);
			if (!composite || !scalar)
				return false;

			Value* result = SsaSlot(state, instruction.m_id);
			if (!result)
				return false;

			*result = *composite;
			const float factor = scalar->GetFloat(0);
			for (cct::UInt32 i = 0; i < result->WordCount(); ++i)
				result->SetFloat(i, composite->GetFloat(i) * factor);

			return true;
		}

		bool ExecuteCompare(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			const Value* lhs = ResolveValue(state, instruction.m_args[0]);
			const Value* rhs = ResolveValue(state, instruction.m_args[1]);
			if (!lhs || !rhs)
				return false;

			const bool isInt = lhs->m_scalar != ir::ScalarKind::Float32;

			Value* result = EmplaceResult(state, instruction.m_id);
			if (!result)
				return false;

			result->m_scalar = ir::ScalarKind::Bool;
			result->m_rows = lhs->m_rows;
			for (cct::UInt32 i = 0; i < result->m_rows; ++i)
			{
				const cct::Int32 signedLeft = lhs->GetInt32(i);
				const cct::Int32 signedRight = rhs->GetInt32(i);
				const cct::UInt32 unsignedLeft = static_cast<cct::UInt32>(signedLeft);
				const cct::UInt32 unsignedRight = static_cast<cct::UInt32>(signedRight);
				const float floatLeft = lhs->GetFloat(i);
				const float floatRight = rhs->GetFloat(i);

				bool comparisonResult = false;
				switch (instruction.m_op)
				{
					case ir::Op::CompareEqual:
						comparisonResult = isInt ? signedLeft == signedRight : floatLeft == floatRight;
						break;
					case ir::Op::CompareNotEqual:
						comparisonResult = isInt ? signedLeft != signedRight : floatLeft != floatRight;
						break;
					case ir::Op::CompareLess:
						comparisonResult = isInt ? signedLeft < signedRight : floatLeft < floatRight;
						break;
					case ir::Op::CompareLessEqual:
						comparisonResult = isInt ? signedLeft <= signedRight : floatLeft <= floatRight;
						break;
					case ir::Op::CompareGreater:
						comparisonResult = isInt ? signedLeft > signedRight : floatLeft > floatRight;
						break;
					case ir::Op::CompareGreaterEqual:
						comparisonResult = isInt ? signedLeft >= signedRight : floatLeft >= floatRight;
						break;
					case ir::Op::CompareUnsignedLess:
						comparisonResult = unsignedLeft < unsignedRight;
						break;
					case ir::Op::CompareUnsignedLessEqual:
						comparisonResult = unsignedLeft <= unsignedRight;
						break;
					case ir::Op::CompareUnsignedGreater:
						comparisonResult = unsignedLeft > unsignedRight;
						break;
					case ir::Op::CompareUnsignedGreaterEqual:
						comparisonResult = unsignedLeft >= unsignedRight;
						break;
					default:
						return false;
				}
				result->SetBool(i, comparisonResult);
			}

			return true;
		}

		bool ExecuteLogicalBinary(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			const Value* lhs = ResolveValue(state, instruction.m_args[0]);
			const Value* rhs = ResolveValue(state, instruction.m_args[1]);
			if (!lhs || !rhs)
				return false;

			Value* result = EmplaceResult(state, instruction.m_id);
			if (!result)
				return false;

			result->m_scalar = ir::ScalarKind::Bool;
			result->m_rows = lhs->m_rows;
			for (cct::UInt32 i = 0; i < result->m_rows; ++i)
			{
				const bool a = lhs->GetBool(i);
				const bool b = rhs->GetBool(i);
				switch (instruction.m_op)
				{
					case ir::Op::LogicalAnd:
						result->SetBool(i, a && b);
						break;
					case ir::Op::LogicalOr:
						result->SetBool(i, a || b);
						break;
					case ir::Op::LogicalEqual:
						result->SetBool(i, a == b);
						break;
					case ir::Op::LogicalNotEqual:
						result->SetBool(i, a != b);
						break;
					default:
						return false;
				}
			}

			return true;
		}

		bool ExecuteLogicalNot(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.empty())
				return false;

			const Value* operand = ResolveValue(state, instruction.m_args[0]);
			if (!operand)
				return false;

			Value* result = EmplaceResult(state, instruction.m_id);
			if (!result)
				return false;

			result->m_scalar = ir::ScalarKind::Bool;
			result->m_rows = operand->m_rows;
			for (cct::UInt32 i = 0; i < result->m_rows; ++i)
				result->SetBool(i, !operand->GetBool(i));

			return true;
		}

		bool ExecuteAllAny(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.empty())
				return false;

			const Value* operand = ResolveValue(state, instruction.m_args[0]);
			if (!operand)
				return false;

			bool accumulator = instruction.m_op == ir::Op::All;
			for (cct::UInt32 i = 0; i < operand->m_rows; ++i)
			{
				if (instruction.m_op == ir::Op::All)
					accumulator = accumulator && operand->GetBool(i);
				else
					accumulator = accumulator || operand->GetBool(i);
			}

			Value* result = EmplaceResult(state, instruction.m_id);
			if (!result)
				return false;

			result->m_scalar = ir::ScalarKind::Bool;
			result->SetBool(0, accumulator);
			return true;
		}

		bool ExecuteSelect(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 3)
				return false;

			const Value* condition = ResolveValue(state, instruction.m_args[0]);
			const Value* whenTrue = ResolveValue(state, instruction.m_args[1]);
			const Value* whenFalse = ResolveValue(state, instruction.m_args[2]);
			if (!condition || !whenTrue || !whenFalse)
				return false;

			// A vector condition selects component-wise (e.g. glslang's uvec3(bvec3) conversion);
			// a scalar condition picks one operand whole, whatever its shape.
			if (condition->WordCount() > 1)
			{
				Value* result = SsaSlot(state, instruction.m_id);
				if (!result)
					return false;

				*result = *whenTrue;
				for (cct::UInt32 i = 0; i < result->WordCount(); ++i)
					result->Words()[i] = condition->GetBool(i) ? whenTrue->Words()[i] : whenFalse->Words()[i];
				return true;
			}

			return StoreSsa(state, instruction.m_id, condition->GetBool() ? *whenTrue : *whenFalse);
		}

		bool ExecuteImageSample(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			const Value* imageHandle = ResolveValue(state, instruction.m_args[0]);
			const Value* coord = ResolveValue(state, instruction.m_args[1]);
			if (!imageHandle || !coord || imageHandle->m_imageVariable == 0)
				return false;

			auto imageIt = state.m_externalImages.find(imageHandle->m_imageVariable);
			if (imageIt == state.m_externalImages.end())
				return false;

			const ExternalImage& image = imageIt->second;
			if (!image.m_texels || image.m_width == 0 || image.m_height == 0)
				return false;

			// Nearest-neighbor, clamped to the edge, LOD 0 only - no mip chain / filtering yet.
			const float u = std::clamp(coord->GetFloat(0), 0.0f, 0.999999f);
			const float v = coord->m_rows > 1 ? std::clamp(coord->GetFloat(1), 0.0f, 0.999999f) : 0.0f;
			const cct::UInt32 x = std::min(static_cast<cct::UInt32>(u * static_cast<float>(image.m_width)), image.m_width - 1);
			const cct::UInt32 y = std::min(static_cast<cct::UInt32>(v * static_cast<float>(image.m_height)), image.m_height - 1);

			const cct::UInt8* texel = image.m_texels + (static_cast<std::size_t>(y) * image.m_width + x) * 4;

			Value* result = EmplaceResult(state, instruction.m_id);
			if (!result)
				return false;

			result->m_rows = 4;
			for (cct::UInt32 i = 0; i < 4; ++i)
				result->SetFloat(i, static_cast<float>(texel[i]) / 255.0f);

			return true;
		}

		bool ExecuteGlslStd450(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.empty())
				return false;

			std::vector<const Value*> operands;
			operands.reserve(instruction.m_args.size() - 1);
			for (std::size_t i = 1; i < instruction.m_args.size(); ++i)
			{
				const Value* operand = ResolveValue(state, instruction.m_args[i]);
				if (!operand)
					return false;
				operands.push_back(operand);
			}

			if (operands.empty())
				return false;

			const cct::UInt32 componentCount = operands[0]->m_rows * operands[0]->m_columns;
			Value* result = SsaSlot(state, instruction.m_id);
			if (!result)
				return false;

			*result = *operands[0];

			const auto floatUnary = [&](auto&& f)
			{
				for (cct::UInt32 i = 0; i < componentCount; ++i)
					result->SetFloat(i, f(operands[0]->GetFloat(i)));
				return true;
			};
			const auto floatBinary = [&](auto&& f)
			{
				if (operands.size() < 2)
					return false;
				for (cct::UInt32 i = 0; i < componentCount; ++i)
					result->SetFloat(i, f(operands[0]->GetFloat(i), operands[1]->GetFloat(i)));
				return true;
			};
			const auto floatTernary = [&](auto&& f)
			{
				if (operands.size() < 3)
					return false;
				for (cct::UInt32 i = 0; i < componentCount; ++i)
					result->SetFloat(i, f(operands[0]->GetFloat(i), operands[1]->GetFloat(i), operands[2]->GetFloat(i)));
				return true;
			};
			const auto intUnary = [&](auto&& f)
			{
				for (cct::UInt32 i = 0; i < componentCount; ++i)
					result->SetInt32(i, f(operands[0]->GetInt32(i)));
				return true;
			};
			const auto intBinary = [&](auto&& f)
			{
				if (operands.size() < 2)
					return false;
				for (cct::UInt32 i = 0; i < componentCount; ++i)
					result->SetInt32(i, f(operands[0]->GetInt32(i), operands[1]->GetInt32(i)));
				return true;
			};
			const auto intTernary = [&](auto&& f)
			{
				if (operands.size() < 3)
					return false;
				for (cct::UInt32 i = 0; i < componentCount; ++i)
					result->SetInt32(i, f(operands[0]->GetInt32(i), operands[1]->GetInt32(i), operands[2]->GetInt32(i)));
				return true;
			};
			const auto vectorLength = [](const Value& v)
			{
				float sum = 0.0f;
				for (cct::UInt32 i = 0; i < v.WordCount(); ++i)
					sum += v.GetFloat(i) * v.GetFloat(i);
				return std::sqrt(sum);
			};

			bool ok = true;
			switch (static_cast<GLSLstd450>(instruction.m_args[0]))
			{
				case GLSLstd450Round:
					ok = floatUnary([](float x)
									{ return std::round(x); });
					break;
				case GLSLstd450RoundEven:
					ok = floatUnary([](float x)
									{ return std::nearbyint(x); });
					break;
				case GLSLstd450Trunc:
					ok = floatUnary([](float x)
									{ return std::trunc(x); });
					break;
				case GLSLstd450FAbs:
					ok = floatUnary([](float x)
									{ return std::fabs(x); });
					break;
				case GLSLstd450SAbs:
					ok = intUnary([](cct::Int32 x)
								  { return x < 0 ? -x : x; });
					break;
				case GLSLstd450FSign:
					ok = floatUnary([](float x)
									{ return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); });
					break;
				case GLSLstd450SSign:
					ok = intUnary([](cct::Int32 x)
								  { return x > 0 ? 1 : (x < 0 ? -1 : 0); });
					break;
				case GLSLstd450Floor:
					ok = floatUnary([](float x)
									{ return std::floor(x); });
					break;
				case GLSLstd450Ceil:
					ok = floatUnary([](float x)
									{ return std::ceil(x); });
					break;
				case GLSLstd450Fract:
					ok = floatUnary([](float x)
									{ return x - std::floor(x); });
					break;
				case GLSLstd450Radians:
					ok = floatUnary([](float x)
									{ return x * 0.017453292519943295f; });
					break;
				case GLSLstd450Degrees:
					ok = floatUnary([](float x)
									{ return x * 57.29577951308232f; });
					break;
				case GLSLstd450Sin:
					ok = floatUnary([](float x)
									{ return std::sin(x); });
					break;
				case GLSLstd450Cos:
					ok = floatUnary([](float x)
									{ return std::cos(x); });
					break;
				case GLSLstd450Tan:
					ok = floatUnary([](float x)
									{ return std::tan(x); });
					break;
				case GLSLstd450Asin:
					ok = floatUnary([](float x)
									{ return std::asin(x); });
					break;
				case GLSLstd450Acos:
					ok = floatUnary([](float x)
									{ return std::acos(x); });
					break;
				case GLSLstd450Atan:
					ok = floatUnary([](float x)
									{ return std::atan(x); });
					break;
				case GLSLstd450Atan2:
					ok = floatBinary([](float y, float x)
									 { return std::atan2(y, x); });
					break;
				case GLSLstd450Pow:
					ok = floatBinary([](float x, float y)
									 { return std::pow(x, y); });
					break;
				case GLSLstd450Exp:
					ok = floatUnary([](float x)
									{ return std::exp(x); });
					break;
				case GLSLstd450Log:
					ok = floatUnary([](float x)
									{ return std::log(x); });
					break;
				case GLSLstd450Exp2:
					ok = floatUnary([](float x)
									{ return std::exp2(x); });
					break;
				case GLSLstd450Log2:
					ok = floatUnary([](float x)
									{ return std::log2(x); });
					break;
				case GLSLstd450Sqrt:
					ok = floatUnary([](float x)
									{ return std::sqrt(x); });
					break;
				case GLSLstd450InverseSqrt:
					ok = floatUnary([](float x)
									{ return 1.0f / std::sqrt(x); });
					break;
				case GLSLstd450FMin:
					ok = floatBinary([](float x, float y)
									 { return std::min(x, y); });
					break;
				case GLSLstd450FMax:
					ok = floatBinary([](float x, float y)
									 { return std::max(x, y); });
					break;
				case GLSLstd450FClamp:
					ok = floatTernary([](float x, float lo, float hi)
									  { return std::clamp(x, lo, hi); });
					break;
				case GLSLstd450SMin:
					ok = intBinary([](cct::Int32 x, cct::Int32 y)
								   { return std::min(x, y); });
					break;
				case GLSLstd450SMax:
					ok = intBinary([](cct::Int32 x, cct::Int32 y)
								   { return std::max(x, y); });
					break;
				case GLSLstd450SClamp:
					ok = intTernary([](cct::Int32 x, cct::Int32 lo, cct::Int32 hi)
									{ return std::clamp(x, lo, hi); });
					break;
				case GLSLstd450UMin:
					ok = intBinary([](cct::Int32 x, cct::Int32 y)
								   { return static_cast<cct::Int32>(std::min(static_cast<cct::UInt32>(x), static_cast<cct::UInt32>(y))); });
					break;
				case GLSLstd450UMax:
					ok = intBinary([](cct::Int32 x, cct::Int32 y)
								   { return static_cast<cct::Int32>(std::max(static_cast<cct::UInt32>(x), static_cast<cct::UInt32>(y))); });
					break;
				case GLSLstd450UClamp:
					ok = intTernary([](cct::Int32 x, cct::Int32 lo, cct::Int32 hi)
									{ return static_cast<cct::Int32>(std::clamp(static_cast<cct::UInt32>(x), static_cast<cct::UInt32>(lo), static_cast<cct::UInt32>(hi))); });
					break;
				case GLSLstd450FMix:
					ok = floatTernary([](float x, float y, float t)
									  { return x + t * (y - x); });
					break;
				case GLSLstd450Step:
					ok = floatBinary([](float edge, float x)
									 { return x < edge ? 0.0f : 1.0f; });
					break;
				case GLSLstd450SmoothStep:
					ok = floatTernary(
						[](float edge0, float edge1, float x)
						{
							const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
							return t * t * (3.0f - 2.0f * t);
						});
					break;
				case GLSLstd450Fma:
					ok = floatTernary([](float a, float b, float c)
									  { return a * b + c; });
					break;
				case GLSLstd450Length:
					ResetHeader(*result);
					result->SetFloat(0, vectorLength(*operands[0]));
					break;
				case GLSLstd450Distance:
				{
					if (operands.size() < 2)
						return false;
					float sum = 0.0f;
					for (cct::UInt32 i = 0; i < componentCount; ++i)
					{
						const float d = operands[0]->GetFloat(i) - operands[1]->GetFloat(i);
						sum += d * d;
					}
					ResetHeader(*result);
					result->SetFloat(0, std::sqrt(sum));
					break;
				}
				case GLSLstd450Cross:
				{
					if (operands.size() < 2 || componentCount != 3)
						return false;
					const Value& a = *operands[0];
					const Value& b = *operands[1];
					result->SetFloat(0, a.GetFloat(1) * b.GetFloat(2) - a.GetFloat(2) * b.GetFloat(1));
					result->SetFloat(1, a.GetFloat(2) * b.GetFloat(0) - a.GetFloat(0) * b.GetFloat(2));
					result->SetFloat(2, a.GetFloat(0) * b.GetFloat(1) - a.GetFloat(1) * b.GetFloat(0));
					break;
				}
				case GLSLstd450Normalize:
				{
					const float length = vectorLength(*operands[0]);
					if (length == 0.0f)
						return false;
					for (cct::UInt32 i = 0; i < componentCount; ++i)
						result->SetFloat(i, operands[0]->GetFloat(i) / length);
					break;
				}
				default:
					return false;
			}

			if (!ok)
				return false;

			return true;
		}

		bool ExecuteInstruction(RuntimeState& state, const ir::Instruction& instruction)
		{
			switch (instruction.m_op)
			{
				case ir::Op::Variable:
					RegisterVariable(state, instruction.m_id, instruction.m_type);
					return true;
				case ir::Op::Load:
					return ExecuteLoad(state, instruction);
				case ir::Op::Store:
					return ExecuteStore(state, instruction);
				case ir::Op::CopyMemory:
					return ExecuteCopyMemory(state, instruction);
				case ir::Op::AccessChain:
					return ExecuteAccessChain(state, instruction);
				case ir::Op::CompositeConstruct:
					return ExecuteCompositeConstruct(state, instruction);
				case ir::Op::CompositeExtract:
					return ExecuteCompositeExtract(state, instruction);
				case ir::Op::VectorExtractDynamic:
					return ExecuteVectorExtractDynamic(state, instruction);
				case ir::Op::VectorInsertDynamic:
					return ExecuteVectorInsertDynamic(state, instruction);
				case ir::Op::VectorShuffle:
					return ExecuteVectorShuffle(state, instruction);
				case ir::Op::Add:
				case ir::Op::Sub:
				case ir::Op::Mul:
				case ir::Op::Div:
				case ir::Op::Rem:
				case ir::Op::Mod:
				case ir::Op::UnsignedMod:
				case ir::Op::UnsignedDiv:
				case ir::Op::BitwiseAnd:
				case ir::Op::BitwiseOr:
				case ir::Op::BitwiseXor:
				case ir::Op::ShiftLeftLogical:
				case ir::Op::ShiftRightLogical:
				case ir::Op::ShiftRightArithmetic:
					return ExecuteArithmetic(state, instruction);
				case ir::Op::BitwiseNot:
				case ir::Op::BitCount:
				case ir::Op::BitReverse:
					return ExecuteBitUnary(state, instruction);
				case ir::Op::BitFieldInsert:
				case ir::Op::BitFieldSignedExtract:
				case ir::Op::BitFieldUnsignedExtract:
					return ExecuteBitField(state, instruction);
				case ir::Op::Bitcast:
					return ExecuteBitcast(state, instruction);
				case ir::Op::Negate:
					return ExecuteNegate(state, instruction);
				case ir::Op::ConvertSignedToFloat:
				case ir::Op::ConvertUnsignedToFloat:
				case ir::Op::ConvertFloatToSigned:
				case ir::Op::ConvertFloatToUnsigned:
					return ExecuteConvert(state, instruction);
				case ir::Op::Dot:
					return ExecuteDot(state, instruction);
				case ir::Op::MatrixTimesVector:
					return ExecuteMatrixTimesVector(state, instruction);
				case ir::Op::VectorTimesMatrix:
					return ExecuteVectorTimesMatrix(state, instruction);
				case ir::Op::MatrixTimesMatrix:
					return ExecuteMatrixTimesMatrix(state, instruction);
				case ir::Op::VectorTimesScalar:
					return ExecuteVectorTimesScalar(state, instruction);
				case ir::Op::CompareEqual:
				case ir::Op::CompareNotEqual:
				case ir::Op::CompareLess:
				case ir::Op::CompareLessEqual:
				case ir::Op::CompareGreater:
				case ir::Op::CompareGreaterEqual:
				case ir::Op::CompareUnsignedLess:
				case ir::Op::CompareUnsignedLessEqual:
				case ir::Op::CompareUnsignedGreater:
				case ir::Op::CompareUnsignedGreaterEqual:
					return ExecuteCompare(state, instruction);
				case ir::Op::LogicalAnd:
				case ir::Op::LogicalOr:
				case ir::Op::LogicalEqual:
				case ir::Op::LogicalNotEqual:
					return ExecuteLogicalBinary(state, instruction);
				case ir::Op::LogicalNot:
					return ExecuteLogicalNot(state, instruction);
				case ir::Op::All:
				case ir::Op::Any:
					return ExecuteAllAny(state, instruction);
				case ir::Op::Select:
					return ExecuteSelect(state, instruction);
				case ir::Op::ImageSample:
					return ExecuteImageSample(state, instruction);
				case ir::Op::FunctionCall:
					return ExecuteFunctionCall(state, instruction);
				case ir::Op::GlslStd450:
					return ExecuteGlslStd450(state, instruction);
				default:
					return false;
			}
		}

		const ir::BasicBlock* FindBlock(const ir::Function& function, cct::UInt32 label)
		{
			for (const ir::BasicBlock& block : function.m_blocks)
				if (block.m_label == label)
					return &block;
			return nullptr;
		}

		const ir::Function* FindFunction(const ir::Module& module, cct::UInt32 id)
		{
			for (const ir::Function& function : module.m_functions)
				if (function.m_id == id)
					return &function;
			return nullptr;
		}

		struct BlockWalkOutcome
		{
			bool m_success = false;
			std::optional<Value> m_returnValue; // set only when the function returned via OpReturnValue
		};

		BlockWalkOutcome RunBlocks(RuntimeState& state, const ir::Function& function)
		{
			SEE_AUTO_PROFILER_SCOPE();
			if (function.m_blocks.empty())
				return {};

			const ir::BasicBlock* block = &function.m_blocks[0];
			while (block)
			{
				for (const ir::Instruction& instruction : block->m_instructions)
					if (!ExecuteInstruction(state, instruction))
						return {};

				switch (block->m_terminator)
				{
					case ir::Terminator::Return:
						return BlockWalkOutcome{.m_success = true};
					case ir::Terminator::ReturnValue:
					{
						const Value* value = ResolveValue(state, block->m_terminatorOperand);
						if (!value)
							return {};
						return BlockWalkOutcome{.m_success = true, .m_returnValue = *value};
					}
					case ir::Terminator::Branch:
						if (block->m_branchTargets.empty())
							return {};
						block = FindBlock(function, block->m_branchTargets[0]);
						break;
					case ir::Terminator::BranchConditional:
					{
						const Value* condition = ResolveValue(state, block->m_terminatorOperand);
						if (!condition || block->m_branchTargets.size() < 2)
							return {};
						block = FindBlock(function, condition->GetBool() ? block->m_branchTargets[0] : block->m_branchTargets[1]);
						break;
					}
					default:
						return {};
				}

				if (!block)
					return {};
			}

			return {};
		}

		bool ExecuteFunctionCall(RuntimeState& state, const ir::Instruction& instruction)
		{
			SEE_AUTO_PROFILER_SCOPE();
			if (instruction.m_args.empty())
				return false;

			const ir::Function* callee = FindFunction(state.m_module, instruction.m_args[0]);
			if (!callee || instruction.m_args.size() - 1 != callee->m_parameters.size())
				return false;

			for (std::size_t i = 0; i < callee->m_parameters.size(); ++i)
			{
				const cct::UInt32 argId = instruction.m_args[i + 1];
				const cct::UInt32 paramId = callee->m_parameters[i];

				if (const Value* argument = ResolveValue(state, argId))
				{
					if (!StoreSsa(state, paramId, *argument))
						return false;
					continue;
				}

				auto argPointerIt = state.m_pointers.find(argId);
				if (argPointerIt == state.m_pointers.end())
					return false;

				const Value* copyShape = state.m_prepared.GetZeroValue(argPointerIt->second.m_pointeeType);
				if (!copyShape)
					return false;

				std::optional<Value> copy = *copyShape;

				const cct::UInt32 wordCount = copy->WordCount();
				const cct::UInt32* source = ResolveWords(state, argPointerIt->second.m_baseVariable, argPointerIt->second.m_wordOffset, wordCount);
				if (!source)
					return false;

				const std::span<cct::UInt32> words = copy->Words();
				for (cct::UInt32 w = 0; w < wordCount; ++w)
					words[w] = source[w];

				state.m_pointers[paramId] = PointerInfo{.m_baseVariable = paramId, .m_wordOffset = 0, .m_pointeeType = argPointerIt->second.m_pointeeType};
				state.m_variables[paramId] = *copy;
			}

			BlockWalkOutcome outcome = RunBlocks(state, *callee);
			if (!outcome.m_success)
				return false;

			if (outcome.m_returnValue && !StoreSsa(state, instruction.m_id, *outcome.m_returnValue))
				return false;

			return true;
		}
	} // namespace

	std::optional<Value> ExtractStructMember(const ir::Module& module, const Value& composite, cct::UInt32 memberIndex)
	{
		if (composite.m_structType == 0)
			return std::nullopt;

		auto typeIt = module.m_types.find(composite.m_structType);
		if (typeIt == module.m_types.end())
			return std::nullopt;

		std::optional<std::vector<MemberLayout>> layout = GetStructMemberLayout(module, typeIt->second, composite.m_structType);
		if (!layout || memberIndex >= layout->size())
			return std::nullopt;

		std::optional<Value> result = MakeZeroValue(module, typeIt->second.m_memberTypes[memberIndex]);
		if (!result)
			return std::nullopt;

		const MemberLayout& member = (*layout)[memberIndex];
		if (member.m_matrixStride != 0 && result->m_columns > 1 && member.m_matrixStride != result->m_rows)
		{
			for (cct::UInt32 column = 0; column < result->m_columns; ++column)
				for (cct::UInt32 row = 0; row < result->m_rows; ++row)
				{
					const cct::UInt32 source = member.m_offset + column * member.m_matrixStride + row;
					if (source >= Value::MaxWords)
						return std::nullopt;

					result->Words()[column * result->m_rows + row] = composite.Words()[source];
				}

			return result;
		}

		const cct::UInt32 packedWords = result->WordCount();
		for (cct::UInt32 i = 0; i < packedWords && member.m_offset + i < Value::MaxWords; ++i)
			result->Words()[i] = composite.Words()[member.m_offset + i];

		return result;
	}

	const Value* PreparedModule::GetConstant(cct::UInt32 slot) const noexcept
	{
		if (slot >= m_constants.size() || !m_constantValid[slot])
			return nullptr;

		return &m_constants[slot];
	}

	const Value* PreparedModule::GetZeroValue(cct::UInt32 typeId) const noexcept
	{
		if (typeId >= m_zeroValues.size() || !m_zeroValid[typeId])
			return nullptr;

		return &m_zeroValues[typeId];
	}

	const std::vector<MemberLayout>* PreparedModule::GetMemberLayout(cct::UInt32 typeId) const noexcept
	{
		if (typeId >= m_memberLayouts.size() || !m_memberLayoutValid[typeId])
			return nullptr;

		return &m_memberLayouts[typeId];
	}

	cct::UInt32 PreparedModule::GetArrayStride(cct::UInt32 typeId) const noexcept
	{
		return typeId < m_arrayStrides.size() ? m_arrayStrides[typeId] : 0;
	}

	PreparedModule BuildPreparedModule(const ir::Module& module)
	{
		SEE_AUTO_PROFILER_SCOPE();
		PreparedModule prepared;

		cct::UInt32 maxTypeId = 0;
		for (const auto& [typeId, type] : module.m_types)
			maxTypeId = std::max(maxTypeId, typeId);

		const std::size_t typeCount = static_cast<std::size_t>(maxTypeId) + 1;
		prepared.m_zeroValues.assign(typeCount, Value{});
		prepared.m_zeroValid.assign(typeCount, 0);
		prepared.m_memberLayouts.assign(typeCount, {});
		prepared.m_memberLayoutValid.assign(typeCount, 0);
		prepared.m_arrayStrides.assign(typeCount, 0);

		for (const auto& [typeId, type] : module.m_types)
		{
			if (std::optional<Value> zero = MakeZeroValue(module, typeId))
			{
				prepared.m_zeroValues[typeId] = *zero;
				prepared.m_zeroValid[typeId] = 1;
			}

			if (type.m_kind == ir::TypeKind::Struct)
			{
				if (std::optional<std::vector<MemberLayout>> layout = GetStructMemberLayout(module, type, typeId))
				{
					prepared.m_memberLayouts[typeId] = std::move(*layout);
					prepared.m_memberLayoutValid[typeId] = 1;
				}
			}
			else if (type.m_kind == ir::TypeKind::Array)
			{
				if (std::optional<Value> elementShape = MakeZeroValue(module, type.m_pointee))
					prepared.m_arrayStrides[typeId] = GetArrayStride(module, typeId, elementShape->WordCount());
			}
		}

		prepared.m_constants.assign(module.m_constantSlotCount, Value{});
		prepared.m_constantValid.assign(module.m_constantSlotCount, 0);

		for (const auto& [id, constant] : module.m_constants)
		{
			if (id >= module.m_valueRefs.size() || ir::GetKind(module.m_valueRefs[id]) != ir::ValueKind::Constant)
				continue;

			std::optional<Value> value = MakeZeroValue(module, constant.m_type);
			if (!value)
				continue;

			const std::span<cct::UInt32> words = value->Words();
			if (constant.m_words.empty())
				words[0] = constant.m_bits;
			else
				for (std::size_t i = 0; i < constant.m_words.size() && i < words.size(); ++i)
					words[i] = constant.m_words[i];

			const cct::UInt32 slot = ir::GetSlot(module.m_valueRefs[id]);
			prepared.m_constants[slot] = *value;
			prepared.m_constantValid[slot] = 1;
		}

		return prepared;
	}

	void RegisterFile::Reset(cct::UInt32 slotCount)
	{
		if (m_slotEpoch.size() < slotCount)
		{
			m_values.resize(slotCount);
			m_slotEpoch.resize(slotCount, 0);
		}

		if (m_epoch == 0xFFFFFFFFu)
		{
			std::fill(m_slotEpoch.begin(), m_slotEpoch.end(), 0u);
			m_epoch = 0;
		}

		++m_epoch;
	}

	std::optional<std::unordered_map<cct::UInt32, Value>> Run(const ir::Module& module, const ir::Function& function, std::unordered_map<cct::UInt32, Value> inputs,
															  const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
															  const std::unordered_map<cct::UInt32, ExternalImage>& externalImages)
	{
		return Run(module, function, std::move(inputs), externalBuffers, externalImages, BuildPreparedModule(module));
	}

	std::optional<std::unordered_map<cct::UInt32, Value>> Run(const ir::Module& module, const ir::Function& function, std::unordered_map<cct::UInt32, Value> inputs,
															  const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
															  const std::unordered_map<cct::UInt32, ExternalImage>& externalImages, const PreparedModule& prepared)
	{
		RegisterFile registers;
		return Run(module, function, std::move(inputs), externalBuffers, externalImages, prepared, registers);
	}

	std::optional<std::unordered_map<cct::UInt32, Value>> Run(const ir::Module& module, const ir::Function& function, std::unordered_map<cct::UInt32, Value> inputs,
															  const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
															  const std::unordered_map<cct::UInt32, ExternalImage>& externalImages, const PreparedModule& prepared,
															  RegisterFile& registers)
	{
		SEE_AUTO_PROFILER_SCOPE();
		registers.Reset(module.m_ssaSlotCount);
		RuntimeState state{.m_module = module, .m_prepared = prepared, .m_registers = registers, .m_variables = std::move(inputs), .m_externalBuffers = externalBuffers, .m_externalImages = externalImages};

		for (const ir::Instruction& global : module.m_globalInstructions)
			if (global.m_op == ir::Op::Variable)
				RegisterVariable(state, global.m_id, global.m_type);

		if (!RunBlocks(state, function).m_success)
			return std::nullopt;

		return state.m_variables;
	}
} // namespace see::exec
