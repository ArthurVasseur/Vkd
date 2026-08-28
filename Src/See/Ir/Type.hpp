/**
 * @file Type.hpp
 * @brief IR type system: scalar kinds and composite type descriptors
 * @date 2026-08-27
 */

#pragma once

#include <vector>

#include <Concerto/Core/Types/Types.hpp>

namespace see::ir
{
	enum class ScalarKind
	{
		Bool,
		Int32,
		Float32
	};

	enum class TypeKind
	{
		Void,
		Scalar,
		Vector,
		Matrix,
		Pointer,
		Struct,
		Array,
		Image,
		SampledImage
	};

	struct Type
	{
		TypeKind m_kind = TypeKind::Void;
		ScalarKind m_scalar = ScalarKind::Float32; // meaningful for Scalar/Vector/Matrix
		cct::UInt32 m_componentCount = 0; // Vector: component count. Matrix: column count.
		cct::UInt32 m_pointee = 0; // Pointer: pointee type id. Vector: scalar component type id. Matrix: column (Vector) type id. SampledImage: image type id. Array: element type id.
		cct::UInt32 m_arrayLength = 0; // Array: element count (0 for OpTypeRuntimeArray, whose length isn't known at compile time).
		std::vector<cct::UInt32> m_memberTypes; // Struct: member type ids, in declaration order.
	};
} // namespace see::ir
