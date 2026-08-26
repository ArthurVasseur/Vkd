/**
 * @file Stage.hpp
 * @brief Vertex/fragment stage wrappers around see::exec::Run
 * @date 2026-08-24
 *
 * Locate BuiltIn VertexIndex/Position and Location(N) variables via decorations instead of making the caller do it.
 */

#pragma once

#include <optional>
#include <unordered_map>

#include <Concerto/Core/Types/Types.hpp>

#include "See/Exec/Scalar.hpp"
#include "See/Ir/Module.hpp"

namespace see::exec
{
	struct VertexStageOutput
	{
		Value m_position;
		std::unordered_map<cct::UInt32, Value> m_locations; // location number -> value
	};

	[[nodiscard]] std::optional<VertexStageOutput> RunVertexStage(const ir::Module& module, const ir::Function& function, cct::UInt32 vertexIndex,
																  cct::UInt32 instanceIndex = 0,
																  const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers = {},
																  const std::unordered_map<cct::UInt32, ExternalImage>& externalImages = {});

	// `locationInputs` is keyed by location number (e.g. interpolated varyings).
	[[nodiscard]] std::optional<std::unordered_map<cct::UInt32, Value>> RunFragmentStage(
		const ir::Module& module, const ir::Function& function, const std::unordered_map<cct::UInt32, Value>& locationInputs,
		const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers = {},
		const std::unordered_map<cct::UInt32, ExternalImage>& externalImages = {});
} // namespace see::exec
