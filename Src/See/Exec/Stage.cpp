/**
 * @file Stage.cpp
 * @brief Implementation of the vertex/fragment stage wrappers
 * @date 2026-08-24
 */

#include "See/Exec/Stage.hpp"

#include <spirv/unified1/spirv.h>

namespace see::exec
{
	namespace
	{
		cct::UInt32 GetStorageClass(const ir::Instruction& variable)
		{
			return variable.m_args.empty() ? 0 : variable.m_args[0];
		}

		struct BuiltinLocation
		{
			cct::UInt32 m_variable = 0;
			std::optional<cct::UInt32> m_member;
		};

		std::optional<BuiltinLocation> FindBuiltinVariable(const ir::Module& module, SpvStorageClass storageClass, SpvBuiltIn builtin)
		{
			for (const ir::Instruction& global : module.m_globalInstructions)
			{
				if (global.m_op != ir::Op::Variable || GetStorageClass(global) != static_cast<cct::UInt32>(storageClass))
					continue;

				if (auto decorationsIt = module.m_decorations.find(global.m_id); decorationsIt != module.m_decorations.end())
					for (const ir::Decoration& decoration : decorationsIt->second)
						if (decoration.m_kind == SpvDecorationBuiltIn && !decoration.m_literals.empty() && decoration.m_literals[0] == static_cast<cct::UInt32>(builtin))
							return BuiltinLocation{.m_variable = global.m_id};

				auto pointerTypeIt = module.m_types.find(global.m_type);
				if (pointerTypeIt == module.m_types.end() || pointerTypeIt->second.m_kind != ir::TypeKind::Pointer)
					continue;

				auto memberDecorationsIt = module.m_memberDecorations.find(pointerTypeIt->second.m_pointee);
				if (memberDecorationsIt == module.m_memberDecorations.end())
					continue;

				for (const ir::MemberDecoration& decoration : memberDecorationsIt->second)
					if (decoration.m_kind == SpvDecorationBuiltIn && !decoration.m_literals.empty() && decoration.m_literals[0] == static_cast<cct::UInt32>(builtin))
						return BuiltinLocation{.m_variable = global.m_id, .m_member = decoration.m_member};
			}
			return std::nullopt;
		}

		// variable id -> location number, for every Variable of the given storage class decorated Location.
		std::unordered_map<cct::UInt32, cct::UInt32> FindLocationVariables(const ir::Module& module, SpvStorageClass storageClass)
		{
			std::unordered_map<cct::UInt32, cct::UInt32> result;
			for (const ir::Instruction& global : module.m_globalInstructions)
			{
				if (global.m_op != ir::Op::Variable || GetStorageClass(global) != static_cast<cct::UInt32>(storageClass))
					continue;

				auto decorationsIt = module.m_decorations.find(global.m_id);
				if (decorationsIt == module.m_decorations.end())
					continue;

				for (const ir::Decoration& decoration : decorationsIt->second)
					if (decoration.m_kind == SpvDecorationLocation && !decoration.m_literals.empty())
						result[global.m_id] = decoration.m_literals[0];
			}
			return result;
		}
	} // namespace

	std::optional<VertexStageOutput> RunVertexStage(const ir::Module& module, const ir::Function& function, cct::UInt32 vertexIndex, cct::UInt32 instanceIndex,
													const std::unordered_map<cct::UInt32, Value>& locationInputs,
													const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
													const std::unordered_map<cct::UInt32, ExternalImage>& externalImages)
	{
		std::unordered_map<cct::UInt32, Value> inputs;
		for (const auto& [variableId, location] : FindLocationVariables(module, SpvStorageClassInput))
		{
			auto valueIt = locationInputs.find(location);
			if (valueIt != locationInputs.end())
				inputs[variableId] = valueIt->second;
		}

		if (std::optional<BuiltinLocation> vertexIndexVar = FindBuiltinVariable(module, SpvStorageClassInput, SpvBuiltInVertexIndex))
		{
			Value value;
			value.m_scalar = ir::ScalarKind::Int32;
			value.SetInt32(0, static_cast<cct::Int32>(vertexIndex));
			inputs[vertexIndexVar->m_variable] = value;
		}

		if (std::optional<BuiltinLocation> instanceIndexVar = FindBuiltinVariable(module, SpvStorageClassInput, SpvBuiltInInstanceIndex))
		{
			Value value;
			value.m_scalar = ir::ScalarKind::Int32;
			value.SetInt32(0, static_cast<cct::Int32>(instanceIndex));
			inputs[instanceIndexVar->m_variable] = value;
		}

		std::optional<std::unordered_map<cct::UInt32, Value>> result = Run(module, function, std::move(inputs), externalBuffers, externalImages);
		if (!result)
			return std::nullopt;

		std::optional<BuiltinLocation> positionVar = FindBuiltinVariable(module, SpvStorageClassOutput, SpvBuiltInPosition);
		if (!positionVar)
			return std::nullopt;

		auto positionIt = result->find(positionVar->m_variable);
		if (positionIt == result->end())
			return std::nullopt;

		VertexStageOutput output;
		if (positionVar->m_member)
		{
			std::optional<Value> member = ExtractStructMember(module, positionIt->second, *positionVar->m_member);
			if (!member)
				return std::nullopt;
			output.m_position = *member;
		}
		else
			output.m_position = positionIt->second;

		for (const auto& [variableId, location] : FindLocationVariables(module, SpvStorageClassOutput))
		{
			auto valueIt = result->find(variableId);
			if (valueIt != result->end())
				output.m_locations[location] = valueIt->second;
		}

		return output;
	}

	std::optional<std::unordered_map<cct::UInt32, Value>> RunFragmentStage(
		const ir::Module& module, const ir::Function& function, const std::unordered_map<cct::UInt32, Value>& locationInputs,
		const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
		const std::unordered_map<cct::UInt32, ExternalImage>& externalImages)
	{
		std::unordered_map<cct::UInt32, Value> inputs;
		for (const auto& [variableId, location] : FindLocationVariables(module, SpvStorageClassInput))
		{
			auto valueIt = locationInputs.find(location);
			if (valueIt != locationInputs.end())
				inputs[variableId] = valueIt->second;
		}

		std::optional<std::unordered_map<cct::UInt32, Value>> result = Run(module, function, std::move(inputs), externalBuffers, externalImages);
		if (!result)
			return std::nullopt;

		std::unordered_map<cct::UInt32, Value> outputs;
		for (const auto& [variableId, location] : FindLocationVariables(module, SpvStorageClassOutput))
		{
			auto valueIt = result->find(variableId);
			if (valueIt != result->end())
				outputs[location] = valueIt->second;
		}

		return outputs;
	}
} // namespace see::exec
