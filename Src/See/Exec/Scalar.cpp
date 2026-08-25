/**
 * @file Scalar.cpp
 * @brief Implementation of the scalar IR interpreter
 * @date 2026-08-24
 */

#include "See/Exec/Scalar.hpp"

#include <algorithm>
#include <cstring>

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

		// Word offset/count of each member, in declaration order. Members are packed sequentially
		// (Function/Private-storage locals don't need std140/std430 padding rules). Unbounded on
		// purpose: a struct backed by an external buffer (e.g. a uniform block) can be far bigger
		// than the 16-word Value cap - only materializing the *whole* struct as one Value is capped,
		// in MakeZeroValue below, since individual members are still loaded/stored one at a time.
		std::optional<std::vector<MemberLayout>> GetStructMemberLayout(const ir::Module& module, const ir::Type& structType)
		{
			std::vector<MemberLayout> layout;
			cct::UInt32 offset = 0;
			for (cct::UInt32 memberTypeId : structType.m_memberTypes)
			{
				std::optional<Value> memberShape = MakeZeroValue(module, memberTypeId);
				if (!memberShape)
					return std::nullopt;

				const cct::UInt32 wordCount = memberShape->m_rows * memberShape->m_columns;
				layout.push_back(MemberLayout{.m_offset = offset, .m_wordCount = wordCount});
				offset += wordCount;
			}
			return layout;
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
					std::optional<std::vector<MemberLayout>> layout = GetStructMemberLayout(module, type);
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

			value->m_bits[0] = constantIt->second.m_bits;
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
			if (instruction.m_args.size() >= 2)
			{
				std::optional<Value> index = ResolveValue(state, instruction.m_args[1]);
				if (!index)
					return false;
				const cct::UInt32 indexValue = static_cast<cct::UInt32>(index->GetInt32());

				auto typeIt = state.m_module.m_types.find(info.m_pointeeType);
				if (typeIt != state.m_module.m_types.end() && typeIt->second.m_kind == ir::TypeKind::Struct)
				{
					std::optional<std::vector<MemberLayout>> layout = GetStructMemberLayout(state.m_module, typeIt->second);
					if (!layout || indexValue >= layout->size())
						return false;
					info.m_wordOffset += (*layout)[indexValue].m_offset;
					info.m_pointeeType = typeIt->second.m_memberTypes[indexValue];
				}
				else
				{
					// Vector component: one word per index, pointee type unchanged (we don't track scalar element type ids).
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
				auto typeIt = state.m_module.m_types.find(composite->m_structType);
				if (typeIt == state.m_module.m_types.end())
					return false;

				std::optional<std::vector<MemberLayout>> layout = GetStructMemberLayout(state.m_module, typeIt->second);
				if (!layout || index >= layout->size())
					return false;

				std::optional<Value> result = MakeZeroValue(state.m_module, typeIt->second.m_memberTypes[index]);
				if (!result)
					return false;

				const MemberLayout& member = (*layout)[index];
				for (cct::UInt32 i = 0; i < member.m_wordCount && i < 16; ++i)
					result->m_bits[i] = composite->m_bits[member.m_offset + i];

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
						default:
							return false;
					}
					result.SetFloat(i, r);
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

			bool comparisonResult = false;
			const bool isInt = lhs->m_scalar == ir::ScalarKind::Int32;
			const float a = isInt ? static_cast<float>(lhs->GetInt32()) : lhs->GetFloat();
			const float b = isInt ? static_cast<float>(rhs->GetInt32()) : rhs->GetFloat();

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

			Value result;
			result.m_scalar = ir::ScalarKind::Bool;
			result.SetBool(0, comparisonResult);
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
				case ir::Op::Add:
				case ir::Op::Sub:
				case ir::Op::Mul:
				case ir::Op::Div:
					return ExecuteArithmetic(state, instruction);
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
				case ir::Op::Select:
					return ExecuteSelect(state, instruction);
				case ir::Op::ImageSample:
					return ExecuteImageSample(state, instruction);
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
	} // namespace

	std::optional<std::unordered_map<cct::UInt32, Value>> Run(const ir::Module& module, const ir::Function& function, std::unordered_map<cct::UInt32, Value> inputs,
															  const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
															  const std::unordered_map<cct::UInt32, ExternalImage>& externalImages)
	{
		if (function.m_blocks.empty())
			return std::nullopt;

		RuntimeState state{.m_module = module, .m_variables = std::move(inputs), .m_externalBuffers = externalBuffers, .m_externalImages = externalImages};

		for (const ir::Instruction& global : module.m_globalInstructions)
			if (global.m_op == ir::Op::Variable)
				RegisterVariable(state, global.m_id, global.m_type);

		const ir::BasicBlock* block = &function.m_blocks[0];
		while (block)
		{
			for (const ir::Instruction& instruction : block->m_instructions)
				if (!ExecuteInstruction(state, instruction))
					return std::nullopt;

			switch (block->m_terminator)
			{
				case ir::Terminator::Return:
				case ir::Terminator::ReturnValue:
					return state.m_variables;
				case ir::Terminator::Branch:
					if (block->m_branchTargets.empty())
						return std::nullopt;
					block = FindBlock(function, block->m_branchTargets[0]);
					break;
				case ir::Terminator::BranchConditional:
				{
					std::optional<Value> condition = ResolveValue(state, block->m_terminatorOperand);
					if (!condition || block->m_branchTargets.size() < 2)
						return std::nullopt;
					block = FindBlock(function, condition->GetBool() ? block->m_branchTargets[0] : block->m_branchTargets[1]);
					break;
				}
				default:
					return std::nullopt;
			}

			if (!block)
				return std::nullopt;
		}

		return state.m_variables;
	}
} // namespace see::exec
