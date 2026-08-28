/**
 * @file Scalar.cpp
 * @brief Implementation of the scalar IR interpreter
 * @date 2026-08-24
 */

#include "See/Exec/Scalar.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <spirv/unified1/GLSL.std.450.h>
#include <spirv/unified1/spirv.h>

namespace see::exec
{
	float Value::GetFloat(cct::UInt32 index) const
	{
		float value;
		std::memcpy(&value, &m_bits[index], sizeof(float));
		return value;
	}

	void Value::SetFloat(cct::UInt32 index, float value)
	{
		std::memcpy(&m_bits[index], &value, sizeof(float));
	}

	cct::Int32 Value::GetInt32(cct::UInt32 index) const
	{
		cct::Int32 value;
		std::memcpy(&value, &m_bits[index], sizeof(cct::Int32));
		return value;
	}

	void Value::SetInt32(cct::UInt32 index, cct::Int32 value)
	{
		std::memcpy(&m_bits[index], &value, sizeof(cct::Int32));
	}

	bool Value::GetBool(cct::UInt32 index) const
	{
		return m_bits[index] != 0;
	}

	void Value::SetBool(cct::UInt32 index, bool value)
	{
		m_bits[index] = value ? 1u : 0u;
	}

	namespace
	{
		struct PointerInfo
		{
			cct::UInt32 m_baseVariable = 0;
			cct::UInt32 m_wordOffset = 0;
			cct::UInt32 m_pointeeType = 0; // ir type id of what this pointer currently points to
		};

		struct RuntimeState
		{
			const ir::Module& m_module;
			std::unordered_map<cct::UInt32, Value> m_variables;
			std::unordered_map<cct::UInt32, ExternalBuffer> m_externalBuffers;
			std::unordered_map<cct::UInt32, ExternalImage> m_externalImages;
			std::unordered_map<cct::UInt32, Value> m_ssaValues;
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

		struct MemberLayout
		{
			cct::UInt32 m_offset = 0;
			cct::UInt32 m_wordCount = 0;
		};

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
				const cct::UInt32 wordCount = memberShape ? memberShape->m_rows * memberShape->m_columns : 0;

				cct::UInt32 offset = packedOffset;
				if (memberDecorations)
					for (const ir::MemberDecoration& decoration : *memberDecorations)
						if (decoration.m_member == memberIndex && decoration.m_kind == SpvDecorationOffset && !decoration.m_literals.empty())
						{
							offset = decoration.m_literals[0] / 4; // byte offset -> word offset
							break;
						}

				layout.push_back(MemberLayout{.m_offset = offset, .m_wordCount = wordCount});
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
					// A Value can only hold 16 words - fine for small locals (the usual case), but a struct
					// backed by an external buffer is addressed member-by-member instead of ever being
					// materialized whole, so this cap doesn't limit external (e.g. uniform block) size.
					if (totalWords > 16)
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

					const cct::UInt32 stride = GetArrayStride(module, typeId, elementShape->m_rows * elementShape->m_columns);
					const cct::UInt32 totalWords = stride * type.m_arrayLength;
					if (totalWords > 16)
						return std::nullopt;

					return Value{.m_rows = totalWords};
				}
				default:
					return std::nullopt;
			}
		}

		std::optional<Value> ResolveConstant(const ir::Module& module, cct::UInt32 id)
		{
			auto constantIt = module.m_constants.find(id);
			if (constantIt == module.m_constants.end())
				return std::nullopt;

			std::optional<Value> value = MakeZeroValue(module, constantIt->second.m_type);
			if (!value)
				return std::nullopt;

			if (constantIt->second.m_words.empty())
				value->m_bits[0] = constantIt->second.m_bits;
			else
				for (cct::UInt32 i = 0; i < constantIt->second.m_words.size() && i < 16; ++i)
					value->m_bits[i] = constantIt->second.m_words[i];

			return value;
		}

		std::optional<Value> ResolveValue(const RuntimeState& state, cct::UInt32 id)
		{
			if (auto it = state.m_ssaValues.find(id); it != state.m_ssaValues.end())
				return it->second;

			return ResolveConstant(state.m_module, id);
		}

		void RegisterVariable(RuntimeState& state, cct::UInt32 id, cct::UInt32 pointerTypeId)
		{
			cct::UInt32 pointeeType = 0;
			if (auto typeIt = state.m_module.m_types.find(pointerTypeId); typeIt != state.m_module.m_types.end() && typeIt->second.m_kind == ir::TypeKind::Pointer)
				pointeeType = typeIt->second.m_pointee;

			state.m_pointers[id] = PointerInfo{.m_baseVariable = id, .m_wordOffset = 0, .m_pointeeType = pointeeType};
			if (state.m_externalBuffers.find(id) == state.m_externalBuffers.end() && state.m_variables.find(id) == state.m_variables.end())
				if (std::optional<Value> zero = MakeZeroValue(state.m_module, pointerTypeId))
					state.m_variables[id] = *zero;
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
				state.m_ssaValues[instruction.m_id] = Value{.m_imageVariable = pointerIt->second.m_baseVariable};
				return true;
			}

			std::optional<Value> result = MakeZeroValue(state.m_module, pointerIt->second.m_pointeeType);
			if (!result)
				return false;

			const cct::UInt32 wordCount = result->m_rows * result->m_columns;
			const cct::UInt32* source = ResolveWords(state, pointerIt->second.m_baseVariable, pointerIt->second.m_wordOffset, wordCount);
			if (!source)
				return false;

			for (cct::UInt32 i = 0; i < wordCount && i < 16; ++i)
				result->m_bits[i] = source[i];

			state.m_ssaValues[instruction.m_id] = *result;
			return true;
		}

		bool ExecuteStore(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			auto pointerIt = state.m_pointers.find(instruction.m_args[0]);
			if (pointerIt == state.m_pointers.end())
				return false;

			std::optional<Value> value = ResolveValue(state, instruction.m_args[1]);
			if (!value)
				return false;

			const cct::UInt32 wordCount = value->m_rows * value->m_columns;
			cct::UInt32* target = ResolveWords(state, pointerIt->second.m_baseVariable, pointerIt->second.m_wordOffset, wordCount);
			if (!target)
				return false;

			for (cct::UInt32 i = 0; i < wordCount && i < 16; ++i)
				target[i] = value->m_bits[i];

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

			std::optional<Value> shape = MakeZeroValue(state.m_module, targetIt->second.m_pointeeType);
			if (!shape)
				return false;

			const cct::UInt32 wordCount = shape->m_rows * shape->m_columns;
			cct::UInt32* target = ResolveWords(state, targetIt->second.m_baseVariable, targetIt->second.m_wordOffset, wordCount);
			const cct::UInt32* source = ResolveWords(state, sourceIt->second.m_baseVariable, sourceIt->second.m_wordOffset, wordCount);
			if (!target || !source)
				return false;

			for (cct::UInt32 i = 0; i < wordCount && i < 16; ++i)
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
				std::optional<Value> index = ResolveValue(state, instruction.m_args[argIndex]);
				if (!index)
					return false;
				const cct::UInt32 indexValue = static_cast<cct::UInt32>(index->GetInt32());

				auto typeIt = state.m_module.m_types.find(info.m_pointeeType);
				if (typeIt != state.m_module.m_types.end() && typeIt->second.m_kind == ir::TypeKind::Struct)
				{
					std::optional<std::vector<MemberLayout>> layout = GetStructMemberLayout(state.m_module, typeIt->second, info.m_pointeeType);
					if (!layout || indexValue >= layout->size())
						return false;
					info.m_wordOffset += (*layout)[indexValue].m_offset;
					info.m_pointeeType = typeIt->second.m_memberTypes[indexValue];
				}
				else if (typeIt != state.m_module.m_types.end() && typeIt->second.m_kind == ir::TypeKind::Array)
				{
					std::optional<Value> elementShape = MakeZeroValue(state.m_module, typeIt->second.m_pointee);
					if (!elementShape)
						return false;
					const cct::UInt32 stride = GetArrayStride(state.m_module, info.m_pointeeType, elementShape->m_rows * elementShape->m_columns);
					info.m_wordOffset += indexValue * stride;
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
			std::optional<Value> result = MakeZeroValue(state.m_module, instruction.m_type);
			if (!result)
				return false;

			// Each arg contributes its own word count, concatenated in order - covers scalars-into-vector,
			// vectors-into-matrix-columns, and mixed scalar/vector args (e.g. vec4 built from a vec3 + a float).
			const cct::UInt32 totalWords = result->m_rows * result->m_columns;
			cct::UInt32 writeIndex = 0;
			for (cct::UInt32 argId : instruction.m_args)
			{
				std::optional<Value> component = ResolveValue(state, argId);
				if (!component)
					return false;

				const cct::UInt32 componentWords = component->m_rows * component->m_columns;
				for (cct::UInt32 i = 0; i < componentWords && writeIndex < totalWords; ++i, ++writeIndex)
					result->m_bits[writeIndex] = component->m_bits[i];
			}

			if (writeIndex != totalWords)
				return false;

			state.m_ssaValues[instruction.m_id] = *result;
			return true;
		}

		bool ExecuteCompositeExtract(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			std::optional<Value> composite = ResolveValue(state, instruction.m_args[0]);
			if (!composite)
				return false;

			const cct::UInt32 index = instruction.m_args[1];

			if (composite->m_structType != 0)
			{
				std::optional<Value> result = ExtractStructMember(state.m_module, *composite, index);
				if (!result)
					return false;

				state.m_ssaValues[instruction.m_id] = *result;
				return true;
			}

			if (index >= composite->m_rows * composite->m_columns)
				return false;

			Value result;
			result.m_scalar = composite->m_scalar;
			result.m_bits[0] = composite->m_bits[index];

			state.m_ssaValues[instruction.m_id] = result;
			return true;
		}

		bool ExecuteVectorExtractDynamic(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			std::optional<Value> vector = ResolveValue(state, instruction.m_args[0]);
			std::optional<Value> index = ResolveValue(state, instruction.m_args[1]);
			if (!vector || !index)
				return false;

			const cct::UInt32 indexValue = static_cast<cct::UInt32>(index->GetInt32());
			if (indexValue >= vector->m_rows * vector->m_columns)
				return false;

			Value result;
			result.m_scalar = vector->m_scalar;
			result.m_bits[0] = vector->m_bits[indexValue];

			state.m_ssaValues[instruction.m_id] = result;
			return true;
		}

		bool ExecuteVectorInsertDynamic(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 3)
				return false;

			std::optional<Value> vector = ResolveValue(state, instruction.m_args[0]);
			std::optional<Value> component = ResolveValue(state, instruction.m_args[1]);
			std::optional<Value> index = ResolveValue(state, instruction.m_args[2]);
			if (!vector || !component || !index)
				return false;

			const cct::UInt32 indexValue = static_cast<cct::UInt32>(index->GetInt32());
			if (indexValue >= vector->m_rows * vector->m_columns)
				return false;

			Value result = *vector;
			result.m_bits[indexValue] = component->m_bits[0];

			state.m_ssaValues[instruction.m_id] = result;
			return true;
		}

		bool ExecuteArithmetic(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			std::optional<Value> lhs = ResolveValue(state, instruction.m_args[0]);
			std::optional<Value> rhs = ResolveValue(state, instruction.m_args[1]);
			if (!lhs || !rhs || lhs->m_rows != rhs->m_rows || lhs->m_scalar != rhs->m_scalar)
				return false;

			Value result;
			result.m_scalar = lhs->m_scalar;
			result.m_rows = lhs->m_rows;

			for (cct::UInt32 i = 0; i < result.m_rows; ++i)
			{
				if (result.m_scalar == ir::ScalarKind::Int32)
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
						default:
							return false;
					}
					result.SetInt32(i, r);
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
					result.SetFloat(i, r);
				}
			}

			state.m_ssaValues[instruction.m_id] = result;
			return true;
		}

		bool ExecuteNegate(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.empty())
				return false;

			std::optional<Value> operand = ResolveValue(state, instruction.m_args[0]);
			if (!operand)
				return false;

			Value result = *operand;
			for (cct::UInt32 i = 0; i < result.m_rows * result.m_columns; ++i)
			{
				if (result.m_scalar == ir::ScalarKind::Int32)
					result.SetInt32(i, -operand->GetInt32(i));
				else
					result.SetFloat(i, -operand->GetFloat(i));
			}

			state.m_ssaValues[instruction.m_id] = result;
			return true;
		}

		bool ExecuteConvert(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.empty())
				return false;

			std::optional<Value> operand = ResolveValue(state, instruction.m_args[0]);
			if (!operand)
				return false;

			Value result = *operand;
			result.m_scalar = (instruction.m_op == ir::Op::ConvertSignedToFloat || instruction.m_op == ir::Op::ConvertUnsignedToFloat) ? ir::ScalarKind::Float32 : ir::ScalarKind::Int32;
			for (cct::UInt32 i = 0; i < result.m_rows * result.m_columns; ++i)
			{
				switch (instruction.m_op)
				{
					case ir::Op::ConvertSignedToFloat:
						result.SetFloat(i, static_cast<float>(operand->GetInt32(i)));
						break;
					case ir::Op::ConvertUnsignedToFloat:
						result.SetFloat(i, static_cast<float>(static_cast<cct::UInt32>(operand->GetInt32(i))));
						break;
					case ir::Op::ConvertFloatToSigned:
						result.SetInt32(i, static_cast<cct::Int32>(operand->GetFloat(i)));
						break;
					case ir::Op::ConvertFloatToUnsigned:
						result.SetInt32(i, static_cast<cct::Int32>(static_cast<cct::UInt32>(operand->GetFloat(i))));
						break;
					default:
						return false;
				}
			}

			state.m_ssaValues[instruction.m_id] = result;
			return true;
		}

		bool ExecuteDot(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			std::optional<Value> lhs = ResolveValue(state, instruction.m_args[0]);
			std::optional<Value> rhs = ResolveValue(state, instruction.m_args[1]);
			if (!lhs || !rhs || lhs->m_rows != rhs->m_rows)
				return false;

			float sum = 0.0f;
			for (cct::UInt32 i = 0; i < lhs->m_rows; ++i)
				sum += lhs->GetFloat(i) * rhs->GetFloat(i);

			Value result;
			result.SetFloat(0, sum);
			state.m_ssaValues[instruction.m_id] = result;
			return true;
		}

		bool ExecuteMatrixTimesVector(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			std::optional<Value> matrix = ResolveValue(state, instruction.m_args[0]);
			std::optional<Value> vector = ResolveValue(state, instruction.m_args[1]);
			if (!matrix || !vector || matrix->m_columns != vector->m_rows)
				return false;

			Value result;
			result.m_rows = matrix->m_rows;

			for (cct::UInt32 row = 0; row < matrix->m_rows; ++row)
			{
				float sum = 0.0f;
				for (cct::UInt32 col = 0; col < matrix->m_columns; ++col)
					sum += matrix->GetFloat(col * matrix->m_rows + row) * vector->GetFloat(col);
				result.SetFloat(row, sum);
			}

			state.m_ssaValues[instruction.m_id] = result;
			return true;
		}

		bool ExecuteVectorTimesMatrix(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			std::optional<Value> vector = ResolveValue(state, instruction.m_args[0]);
			std::optional<Value> matrix = ResolveValue(state, instruction.m_args[1]);
			if (!vector || !matrix || vector->m_rows != matrix->m_rows)
				return false;

			Value result;
			result.m_rows = matrix->m_columns;

			for (cct::UInt32 col = 0; col < matrix->m_columns; ++col)
			{
				float sum = 0.0f;
				for (cct::UInt32 row = 0; row < matrix->m_rows; ++row)
					sum += vector->GetFloat(row) * matrix->GetFloat(col * matrix->m_rows + row);
				result.SetFloat(col, sum);
			}

			state.m_ssaValues[instruction.m_id] = result;
			return true;
		}

		bool ExecuteMatrixTimesMatrix(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			std::optional<Value> lhs = ResolveValue(state, instruction.m_args[0]);
			std::optional<Value> rhs = ResolveValue(state, instruction.m_args[1]);
			if (!lhs || !rhs || lhs->m_columns != rhs->m_rows)
				return false;

			Value result;
			result.m_rows = lhs->m_rows;
			result.m_columns = rhs->m_columns;

			for (cct::UInt32 col = 0; col < result.m_columns; ++col)
			{
				for (cct::UInt32 row = 0; row < result.m_rows; ++row)
				{
					float sum = 0.0f;
					for (cct::UInt32 k = 0; k < lhs->m_columns; ++k)
						sum += lhs->GetFloat(k * lhs->m_rows + row) * rhs->GetFloat(col * rhs->m_rows + k);
					result.SetFloat(col * result.m_rows + row, sum);
				}
			}

			state.m_ssaValues[instruction.m_id] = result;
			return true;
		}

		bool ExecuteCompare(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			std::optional<Value> lhs = ResolveValue(state, instruction.m_args[0]);
			std::optional<Value> rhs = ResolveValue(state, instruction.m_args[1]);
			if (!lhs || !rhs)
				return false;

			const bool isInt = lhs->m_scalar == ir::ScalarKind::Int32;

			Value result;
			result.m_scalar = ir::ScalarKind::Bool;
			result.m_rows = lhs->m_rows;
			for (cct::UInt32 i = 0; i < result.m_rows; ++i)
			{
				const float a = isInt ? static_cast<float>(lhs->GetInt32(i)) : lhs->GetFloat(i);
				const float b = isInt ? static_cast<float>(rhs->GetInt32(i)) : rhs->GetFloat(i);

				bool comparisonResult = false;
				switch (instruction.m_op)
				{
					case ir::Op::CompareEqual:
						comparisonResult = a == b;
						break;
					case ir::Op::CompareNotEqual:
						comparisonResult = a != b;
						break;
					case ir::Op::CompareLess:
						comparisonResult = a < b;
						break;
					case ir::Op::CompareLessEqual:
						comparisonResult = a <= b;
						break;
					case ir::Op::CompareGreater:
						comparisonResult = a > b;
						break;
					case ir::Op::CompareGreaterEqual:
						comparisonResult = a >= b;
						break;
					default:
						return false;
				}
				result.SetBool(i, comparisonResult);
			}

			state.m_ssaValues[instruction.m_id] = result;
			return true;
		}

		bool ExecuteLogicalBinary(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			std::optional<Value> lhs = ResolveValue(state, instruction.m_args[0]);
			std::optional<Value> rhs = ResolveValue(state, instruction.m_args[1]);
			if (!lhs || !rhs)
				return false;

			Value result;
			result.m_scalar = ir::ScalarKind::Bool;
			result.m_rows = lhs->m_rows;
			for (cct::UInt32 i = 0; i < result.m_rows; ++i)
			{
				const bool a = lhs->GetBool(i);
				const bool b = rhs->GetBool(i);
				switch (instruction.m_op)
				{
					case ir::Op::LogicalAnd:
						result.SetBool(i, a && b);
						break;
					case ir::Op::LogicalOr:
						result.SetBool(i, a || b);
						break;
					case ir::Op::LogicalEqual:
						result.SetBool(i, a == b);
						break;
					case ir::Op::LogicalNotEqual:
						result.SetBool(i, a != b);
						break;
					default:
						return false;
				}
			}

			state.m_ssaValues[instruction.m_id] = result;
			return true;
		}

		bool ExecuteLogicalNot(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.empty())
				return false;

			std::optional<Value> operand = ResolveValue(state, instruction.m_args[0]);
			if (!operand)
				return false;

			Value result;
			result.m_scalar = ir::ScalarKind::Bool;
			result.m_rows = operand->m_rows;
			for (cct::UInt32 i = 0; i < result.m_rows; ++i)
				result.SetBool(i, !operand->GetBool(i));

			state.m_ssaValues[instruction.m_id] = result;
			return true;
		}

		bool ExecuteAllAny(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.empty())
				return false;

			std::optional<Value> operand = ResolveValue(state, instruction.m_args[0]);
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

			Value result;
			result.m_scalar = ir::ScalarKind::Bool;
			result.SetBool(0, accumulator);
			state.m_ssaValues[instruction.m_id] = result;
			return true;
		}

		bool ExecuteSelect(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 3)
				return false;

			std::optional<Value> condition = ResolveValue(state, instruction.m_args[0]);
			std::optional<Value> whenTrue = ResolveValue(state, instruction.m_args[1]);
			std::optional<Value> whenFalse = ResolveValue(state, instruction.m_args[2]);
			if (!condition || !whenTrue || !whenFalse)
				return false;

			state.m_ssaValues[instruction.m_id] = condition->GetBool() ? *whenTrue : *whenFalse;
			return true;
		}

		bool ExecuteImageSample(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.size() < 2)
				return false;

			std::optional<Value> imageHandle = ResolveValue(state, instruction.m_args[0]);
			std::optional<Value> coord = ResolveValue(state, instruction.m_args[1]);
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

			Value result;
			result.m_rows = 4;
			for (cct::UInt32 i = 0; i < 4; ++i)
				result.SetFloat(i, static_cast<float>(texel[i]) / 255.0f);

			state.m_ssaValues[instruction.m_id] = result;
			return true;
		}

		bool ExecuteGlslStd450(RuntimeState& state, const ir::Instruction& instruction)
		{
			if (instruction.m_args.empty())
				return false;

			std::vector<Value> operands;
			operands.reserve(instruction.m_args.size() - 1);
			for (std::size_t i = 1; i < instruction.m_args.size(); ++i)
			{
				std::optional<Value> operand = ResolveValue(state, instruction.m_args[i]);
				if (!operand)
					return false;
				operands.push_back(*operand);
			}

			if (operands.empty())
				return false;

			const cct::UInt32 componentCount = operands[0].m_rows * operands[0].m_columns;
			Value result = operands[0];

			const auto floatUnary = [&](auto&& f)
			{
				for (cct::UInt32 i = 0; i < componentCount; ++i)
					result.SetFloat(i, f(operands[0].GetFloat(i)));
				return true;
			};
			const auto floatBinary = [&](auto&& f)
			{
				if (operands.size() < 2)
					return false;
				for (cct::UInt32 i = 0; i < componentCount; ++i)
					result.SetFloat(i, f(operands[0].GetFloat(i), operands[1].GetFloat(i)));
				return true;
			};
			const auto floatTernary = [&](auto&& f)
			{
				if (operands.size() < 3)
					return false;
				for (cct::UInt32 i = 0; i < componentCount; ++i)
					result.SetFloat(i, f(operands[0].GetFloat(i), operands[1].GetFloat(i), operands[2].GetFloat(i)));
				return true;
			};
			const auto intUnary = [&](auto&& f)
			{
				for (cct::UInt32 i = 0; i < componentCount; ++i)
					result.SetInt32(i, f(operands[0].GetInt32(i)));
				return true;
			};
			const auto intBinary = [&](auto&& f)
			{
				if (operands.size() < 2)
					return false;
				for (cct::UInt32 i = 0; i < componentCount; ++i)
					result.SetInt32(i, f(operands[0].GetInt32(i), operands[1].GetInt32(i)));
				return true;
			};
			const auto intTernary = [&](auto&& f)
			{
				if (operands.size() < 3)
					return false;
				for (cct::UInt32 i = 0; i < componentCount; ++i)
					result.SetInt32(i, f(operands[0].GetInt32(i), operands[1].GetInt32(i), operands[2].GetInt32(i)));
				return true;
			};
			const auto vectorLength = [](const Value& v)
			{
				float sum = 0.0f;
				for (cct::UInt32 i = 0; i < v.m_rows * v.m_columns; ++i)
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
					result = Value{};
					result.SetFloat(0, vectorLength(operands[0]));
					break;
				case GLSLstd450Distance:
				{
					if (operands.size() < 2)
						return false;
					float sum = 0.0f;
					for (cct::UInt32 i = 0; i < componentCount; ++i)
					{
						const float d = operands[0].GetFloat(i) - operands[1].GetFloat(i);
						sum += d * d;
					}
					result = Value{};
					result.SetFloat(0, std::sqrt(sum));
					break;
				}
				case GLSLstd450Cross:
				{
					if (operands.size() < 2 || componentCount != 3)
						return false;
					const Value& a = operands[0];
					const Value& b = operands[1];
					result.SetFloat(0, a.GetFloat(1) * b.GetFloat(2) - a.GetFloat(2) * b.GetFloat(1));
					result.SetFloat(1, a.GetFloat(2) * b.GetFloat(0) - a.GetFloat(0) * b.GetFloat(2));
					result.SetFloat(2, a.GetFloat(0) * b.GetFloat(1) - a.GetFloat(1) * b.GetFloat(0));
					break;
				}
				case GLSLstd450Normalize:
				{
					const float length = vectorLength(operands[0]);
					if (length == 0.0f)
						return false;
					for (cct::UInt32 i = 0; i < componentCount; ++i)
						result.SetFloat(i, operands[0].GetFloat(i) / length);
					break;
				}
				default:
					return false;
			}

			if (!ok)
				return false;

			state.m_ssaValues[instruction.m_id] = result;
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
				case ir::Op::Add:
				case ir::Op::Sub:
				case ir::Op::Mul:
				case ir::Op::Div:
				case ir::Op::Rem:
				case ir::Op::Mod:
				case ir::Op::UnsignedMod:
					return ExecuteArithmetic(state, instruction);
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
				case ir::Op::CompareEqual:
				case ir::Op::CompareNotEqual:
				case ir::Op::CompareLess:
				case ir::Op::CompareLessEqual:
				case ir::Op::CompareGreater:
				case ir::Op::CompareGreaterEqual:
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
						std::optional<Value> value = ResolveValue(state, block->m_terminatorOperand);
						if (!value)
							return {};
						return BlockWalkOutcome{.m_success = true, .m_returnValue = value};
					}
					case ir::Terminator::Branch:
						if (block->m_branchTargets.empty())
							return {};
						block = FindBlock(function, block->m_branchTargets[0]);
						break;
					case ir::Terminator::BranchConditional:
					{
						std::optional<Value> condition = ResolveValue(state, block->m_terminatorOperand);
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
			if (instruction.m_args.empty())
				return false;

			const ir::Function* callee = FindFunction(state.m_module, instruction.m_args[0]);
			if (!callee || instruction.m_args.size() - 1 != callee->m_parameters.size())
				return false;

			for (std::size_t i = 0; i < callee->m_parameters.size(); ++i)
			{
				const cct::UInt32 argId = instruction.m_args[i + 1];
				const cct::UInt32 paramId = callee->m_parameters[i];

				if (std::optional<Value> argument = ResolveValue(state, argId))
				{
					state.m_ssaValues[paramId] = *argument;
					continue;
				}

				auto argPointerIt = state.m_pointers.find(argId);
				if (argPointerIt == state.m_pointers.end())
					return false;

				std::optional<Value> copy = MakeZeroValue(state.m_module, argPointerIt->second.m_pointeeType);
				if (!copy)
					return false;

				const cct::UInt32 wordCount = copy->m_rows * copy->m_columns;
				const cct::UInt32* source = ResolveWords(state, argPointerIt->second.m_baseVariable, argPointerIt->second.m_wordOffset, wordCount);
				if (!source)
					return false;

				for (cct::UInt32 w = 0; w < wordCount && w < 16; ++w)
					copy->m_bits[w] = source[w];

				state.m_pointers[paramId] = PointerInfo{.m_baseVariable = paramId, .m_wordOffset = 0, .m_pointeeType = argPointerIt->second.m_pointeeType};
				state.m_variables[paramId] = *copy;
			}

			BlockWalkOutcome outcome = RunBlocks(state, *callee);
			if (!outcome.m_success)
				return false;

			if (outcome.m_returnValue)
				state.m_ssaValues[instruction.m_id] = *outcome.m_returnValue;

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
		for (cct::UInt32 i = 0; i < member.m_wordCount && i < 16; ++i)
			result->m_bits[i] = composite.m_bits[member.m_offset + i];

		return result;
	}

	std::optional<std::unordered_map<cct::UInt32, Value>> Run(const ir::Module& module, const ir::Function& function, std::unordered_map<cct::UInt32, Value> inputs,
															  const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
															  const std::unordered_map<cct::UInt32, ExternalImage>& externalImages)
	{
		RuntimeState state{.m_module = module, .m_variables = std::move(inputs), .m_externalBuffers = externalBuffers, .m_externalImages = externalImages};

		for (const ir::Instruction& global : module.m_globalInstructions)
			if (global.m_op == ir::Op::Variable)
				RegisterVariable(state, global.m_id, global.m_type);

		if (!RunBlocks(state, function).m_success)
			return std::nullopt;

		return state.m_variables;
	}
} // namespace see::exec
