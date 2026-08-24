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

		std::optional<cct::UInt32> FindBuiltinVariable(const ir::Module& module, SpvStorageClass storageClass, SpvBuiltIn builtin)
		{
			for (const ir::Instruction& global : module.m_globalInstructions)
			{
				if (global.m_op != ir::Op::Variable || GetStorageClass(global) != static_cast<cct::UInt32>(storageClass))
					continue;

				auto decorationsIt = module.m_decorations.find(global.m_id);
				if (decorationsIt == module.m_decorations.end())
					continue;

				for (const ir::Decoration& decoration : decorationsIt->second)
					if (decoration.m_kind == SpvDecorationBuiltIn && !decoration.m_literals.empty() && decoration.m_literals[0] == static_cast<cct::UInt32>(builtin))
						return global.m_id;
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

	std::optional<VertexStageOutput> RunVertexStage(const ir::Module& module, const ir::Function& function, cct::UInt32 vertexIndex)
	{
		std::unordered_map<cct::UInt32, Value> inputs;
		if (std::optional<cct::UInt32> vertexIndexVar = FindBuiltinVariable(module, SpvStorageClassInput, SpvBuiltInVertexIndex))
		{
			Value value;
			value.m_scalar = ir::ScalarKind::Int32;
			value.SetInt32(0, static_cast<cct::Int32>(vertexIndex));
			inputs[*vertexIndexVar] = value;
		}

		std::optional<std::unordered_map<cct::UInt32, Value>> result = Run(module, function, std::move(inputs));
		if (!result)
			return std::nullopt;

		std::optional<cct::UInt32> positionVar = FindBuiltinVariable(module, SpvStorageClassOutput, SpvBuiltInPosition);
		if (!positionVar)
			return std::nullopt;

		auto positionIt = result->find(*positionVar);
		if (positionIt == result->end())
			return std::nullopt;

		VertexStageOutput output;
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
		const ir::Module& module, const ir::Function& function, const std::unordered_map<cct::UInt32, Value>& locationInputs)
	{
		std::unordered_map<cct::UInt32, Value> inputs;
		for (const auto& [variableId, location] : FindLocationVariables(module, SpvStorageClassInput))
		{
			auto valueIt = locationInputs.find(location);
			if (valueIt != locationInputs.end())
				inputs[variableId] = valueIt->second;
		}

		std::optional<std::unordered_map<cct::UInt32, Value>> result = Run(module, function, std::move(inputs));
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
