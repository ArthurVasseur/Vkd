/**
 * @file CrashHandler.hpp
 * @brief Platform-specific crash handling interface
 * @date 2025-12-05
 *
 * Provides a unified interface for installing platform-specific crash handlers.
 * Supports POSIX (signal-based) and Windows (SEH-based) exception handling.
 */

#pragma once

#include <exception>
#include <memory>
#include <string_view>

namespace vkd
{

	/**
	 * @class CrashHandler
	 * @brief Abstract base class for platform-specific crash handling
	 *
	 * Defines the interface for installing and managing crash handlers.
	 * Use Create() to instantiate the appropriate platform-specific implementation.
	 */
	class CrashHandler
	{
	public:
		virtual ~CrashHandler() = default;

		/**
		 * @brief Install the crash handler for the current platform
		 * @return true if installation succeeded, false otherwise
		 */
		virtual bool Install(std::string_view dumpPath = "./dumps") = 0;

		/**
		 * @brief Uninstall the crash handler and restore previous state
		 */
		virtual void Uninstall() = 0;

		/**
		 * @brief Handle an unhandled exception
		 * @param eptr Exception pointer to handle
		 */
		virtual void HandleUnhandledException(std::exception_ptr eptr) = 0;

		/**
		 * @brief Create a platform-specific CrashHandler instance
		 * @return Unique pointer to a CrashHandler implementation
		 */
		static std::unique_ptr<CrashHandler> Create();
	};

} // namespace vkd
