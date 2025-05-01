//
// Created by arthur on 23/04/2025.
//

#pragma once
#include <memory>

namespace wddmDump
{
	class CommandQueue
	{
	public:
		enum class Type
		{
			Compute,
			Direct,
			Copy
		};
		virtual ~CommandQueue() = default;
	};
}
