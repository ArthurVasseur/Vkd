/**
 * @file Stage.cpp
 * @brief Implementation of the vertex/fragment stage wrappers
 * @date 2026-08-24
 */

#include "See/Exec/Stage.hpp"

#include "See/Defines.hpp"
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

		struct VariableShape
		{
			cct::UInt32 m_rows = 1;
			cct::UInt32 m_columns = 1;
		};

		// A matrix consumes one location per column, so its value is split across consecutive locations.
		VariableShape FindVariableShape(const ir::Module& module, cct::UInt32 variableId)
		{
			for (const ir::Instruction& global : module.m_globalInstructions)
			{
				if (global.m_id != variableId)
					continue;

				auto pointerIt = module.m_types.find(global.m_type);
				if (pointerIt == module.m_types.end() || pointerIt->second.m_kind != ir::TypeKind::Pointer)
					break;

				auto valueIt = module.m_types.find(pointerIt->second.m_pointee);
				if (valueIt == module.m_types.end() || valueIt->second.m_kind != ir::TypeKind::Matrix)
					break;

				auto columnIt = module.m_types.find(valueIt->second.m_pointee);
				if (columnIt == module.m_types.end())
					break;

				return VariableShape{.m_rows = columnIt->second.m_componentCount, .m_columns = valueIt->second.m_componentCount};
			}
			return VariableShape{};
		}

		std::optional<Value> GatherLocationValue(const ir::Module& module, cct::UInt32 variableId, cct::UInt32 location, const std::unordered_map<cct::UInt32, Value>& locationInputs)
		{
			const VariableShape shape = FindVariableShape(module, variableId);
			if (shape.m_columns <= 1)
			{
				auto it = locationInputs.find(location);
				if (it == locationInputs.end())
					return std::nullopt;

				return it->second;
			}

			Value result;
			result.m_rows = shape.m_rows;
			result.m_columns = shape.m_columns;
			bool any = false;
			for (cct::UInt32 column = 0; column < shape.m_columns; ++column)
			{
				auto it = locationInputs.find(location + column);
				if (it == locationInputs.end())
					continue;

				any = true;
				result.m_scalar = it->second.m_scalar;
				for (cct::UInt32 row = 0; row < shape.m_rows; ++row)
					result.Words()[column * shape.m_rows + row] = it->second.Words()[row];
			}

			if (!any)
				return std::nullopt;

			return result;
		}

		void ScatterLocationValue(const ir::Module& module, cct::UInt32 variableId, cct::UInt32 location, const Value& value, std::unordered_map<cct::UInt32, Value>& outLocations)
		{
			const VariableShape shape = FindVariableShape(module, variableId);
			if (shape.m_columns <= 1)
			{
				CopyValue(outLocations[location], value);
				return;
			}

			for (cct::UInt32 column = 0; column < shape.m_columns; ++column)
			{
				Value columnValue;
				columnValue.m_scalar = value.m_scalar;
				columnValue.m_rows = shape.m_rows;
				for (cct::UInt32 row = 0; row < shape.m_rows; ++row)
					columnValue.Words()[row] = value.Words()[column * shape.m_rows + row];

				CopyValue(outLocations[location + column], columnValue);
			}
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
		return RunVertexStage(module, function, vertexIndex, instanceIndex, locationInputs, externalBuffers, externalImages, BuildPreparedModule(module));
	}

	std::optional<VertexStageOutput> RunVertexStage(const ir::Module& module, const ir::Function& function, cct::UInt32 vertexIndex, cct::UInt32 instanceIndex,
													const std::unordered_map<cct::UInt32, Value>& locationInputs,
													const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
													const std::unordered_map<cct::UInt32, ExternalImage>& externalImages, const PreparedModule& prepared)
	{
		ExecutionStorage storage;
		return RunVertexStage(module, function, vertexIndex, instanceIndex, locationInputs, externalBuffers, externalImages, prepared, storage);
	}

	std::optional<VertexStageOutput> RunVertexStage(const ir::Module& module, const ir::Function& function, cct::UInt32 vertexIndex, cct::UInt32 instanceIndex,
													const std::unordered_map<cct::UInt32, Value>& locationInputs,
													const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
													const std::unordered_map<cct::UInt32, ExternalImage>& externalImages, const PreparedModule& prepared,
													ExecutionStorage& storage)
	{
		SEE_AUTO_PROFILER_SCOPE();
		std::unordered_map<cct::UInt32, Value> inputs;
		for (const auto& [variableId, location] : FindLocationVariables(module, SpvStorageClassInput))
			if (std::optional<Value> gathered = GatherLocationValue(module, variableId, location, locationInputs))
				CopyValue(inputs[variableId], *gathered);

		if (std::optional<BuiltinLocation> vertexIndexVar = FindBuiltinVariable(module, SpvStorageClassInput, SpvBuiltInVertexIndex))
		{
			Value value;
			value.m_scalar = ir::ScalarKind::Int32;
			value.SetInt32(0, static_cast<cct::Int32>(vertexIndex));
			CopyValue(inputs[vertexIndexVar->m_variable], value);
		}

		if (std::optional<BuiltinLocation> instanceIndexVar = FindBuiltinVariable(module, SpvStorageClassInput, SpvBuiltInInstanceIndex))
		{
			Value value;
			value.m_scalar = ir::ScalarKind::Int32;
			value.SetInt32(0, static_cast<cct::Int32>(instanceIndex));
			CopyValue(inputs[instanceIndexVar->m_variable], value);
		}

		std::optional<std::unordered_map<cct::UInt32, Value>> result = Run(module, function, std::move(inputs), externalBuffers, externalImages, prepared, storage);
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
			CopyValue(output.m_position, *member);
		}
		else
			CopyValue(output.m_position, positionIt->second);

		for (const auto& [variableId, location] : FindLocationVariables(module, SpvStorageClassOutput))
		{
			auto valueIt = result->find(variableId);
			if (valueIt != result->end())
				ScatterLocationValue(module, variableId, location, valueIt->second, output.m_locations);
		}

		return output;
	}

	StageLocations BuildStageLocations(const ir::Module& module)
	{
		return StageLocations{.m_inputs = FindLocationVariables(module, SpvStorageClassInput), .m_outputs = FindLocationVariables(module, SpvStorageClassOutput)};
	}

	std::optional<std::unordered_map<cct::UInt32, Value>> RunFragmentStage(
		const ir::Module& module, const ir::Function& function, const StageLocations& locations,
		const std::unordered_map<cct::UInt32, Value>& locationInputs,
		const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
		const std::unordered_map<cct::UInt32, ExternalImage>& externalImages)
	{
		return RunFragmentStage(module, function, locations, locationInputs, externalBuffers, externalImages, BuildPreparedModule(module));
	}

	std::optional<std::unordered_map<cct::UInt32, Value>> RunFragmentStage(
		const ir::Module& module, const ir::Function& function, const StageLocations& locations,
		const std::unordered_map<cct::UInt32, Value>& locationInputs,
		const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
		const std::unordered_map<cct::UInt32, ExternalImage>& externalImages, const PreparedModule& prepared)
	{
		ExecutionStorage storage;
		return RunFragmentStage(module, function, locations, locationInputs, externalBuffers, externalImages, prepared, storage);
	}

	std::optional<std::unordered_map<cct::UInt32, Value>> RunFragmentStage(
		const ir::Module& module, const ir::Function& function, const StageLocations& locations,
		const std::unordered_map<cct::UInt32, Value>& locationInputs,
		const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
		const std::unordered_map<cct::UInt32, ExternalImage>& externalImages, const PreparedModule& prepared, ExecutionStorage& storage)
	{
		SEE_AUTO_PROFILER_SCOPE();
		std::unordered_map<cct::UInt32, Value> inputs;
		for (const auto& [variableId, location] : locations.m_inputs)
			if (std::optional<Value> gathered = GatherLocationValue(module, variableId, location, locationInputs))
				CopyValue(inputs[variableId], *gathered);

		std::optional<std::unordered_map<cct::UInt32, Value>> result = Run(module, function, std::move(inputs), externalBuffers, externalImages, prepared, storage);
		if (!result)
			return std::nullopt;

		std::unordered_map<cct::UInt32, Value> outputs;
		for (const auto& [variableId, location] : locations.m_outputs)
		{
			auto valueIt = result->find(variableId);
			if (valueIt != result->end())
				ScatterLocationValue(module, variableId, location, valueIt->second, outputs);
		}

		return outputs;
	}

	std::optional<std::unordered_map<cct::UInt32, Value>> RunFragmentStage(
		const ir::Module& module, const ir::Function& function, const std::unordered_map<cct::UInt32, Value>& locationInputs,
		const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
		const std::unordered_map<cct::UInt32, ExternalImage>& externalImages)
	{
		return RunFragmentStage(module, function, BuildStageLocations(module), locationInputs, externalBuffers, externalImages);
	}
} // namespace see::exec
