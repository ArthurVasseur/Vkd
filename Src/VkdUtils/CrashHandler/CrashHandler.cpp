/**
 * @file CrashHandler.cpp
 * @brief Platform-specific crash handler implementations with dump generation
 * @date 2025-12-05
 *
 * Generates crash dumps (minidumps on Windows, core dumps on POSIX) for debugging
 */

#include "VkdUtils/CrashHandler/CrashHandler.hpp"

#include <csignal>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include <Concerto/Core/Logger/Logger.hpp>

#include <cpptrace/cpptrace.hpp>

#if defined(CCT_PLATFORM_POSIX)
#include <sys/resource.h>
#elif defined(CCT_PLATFORM_WINDOWS)
#define NOMINMAX
#include <dbghelp.h>
#include <windows.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "kernel32.lib")
#endif

namespace vkd
{
	namespace
	{

#if defined(CCT_PLATFORM_POSIX)

		class PosixCrashHandler : public CrashHandler
		{
		private:
			struct sigaction m_oldSigabrtAction;
			struct sigaction m_oldSigsegvAction;
			struct sigaction m_oldSigbusAction;
			struct sigaction m_oldSigfpeAction;
			std::string m_dumpPath;
			bool m_installed;

			static inline PosixCrashHandler* s_instance = nullptr;

		public:
			PosixCrashHandler() noexcept
				: m_dumpPath("./dumps"),
				  m_installed(false)
			{
			}

			~PosixCrashHandler() override
			{
				if (m_installed)
					Uninstall();
				s_instance = nullptr;
			}

			bool CCT_CALL Install(std::string_view dumpPath = "./dumps") override
			{
				if (m_installed)
					return true;

				m_dumpPath = std::filesystem::absolute(dumpPath).string();
				s_instance = this;

				try
				{
					std::filesystem::create_directories(m_dumpPath);
				}
				catch (const std::filesystem::filesystem_error& e)
				{
					cct::Logger::Warning("Failed to create dump directory '{}': {}", m_dumpPath, e.what());
				}

				EnableCoreDumps();

				struct sigaction action;
				action.sa_handler = PosixCrashHandler::SignalHandler;
				sigemptyset(&action.sa_mask);
				action.sa_flags = SA_NODEFER;

				if (sigaction(SIGABRT, &action, &m_oldSigabrtAction) != 0)
					return false;

				if (sigaction(SIGSEGV, &action, &m_oldSigsegvAction) != 0)
				{
					sigaction(SIGABRT, &m_oldSigabrtAction, nullptr);
					return false;
				}

				if (sigaction(SIGBUS, &action, &m_oldSigbusAction) != 0)
				{
					sigaction(SIGABRT, &m_oldSigabrtAction, nullptr);
					sigaction(SIGSEGV, &m_oldSigsegvAction, nullptr);
					return false;
				}

				if (sigaction(SIGFPE, &action, &m_oldSigfpeAction) != 0)
				{
					sigaction(SIGABRT, &m_oldSigabrtAction, nullptr);
					sigaction(SIGSEGV, &m_oldSigsegvAction, nullptr);
					sigaction(SIGBUS, &m_oldSigbusAction, nullptr);
					return false;
				}

				m_installed = true;
				cct::Logger::Info("Crash handler installed (POSIX - Core dumps enabled, dumps to: {})", m_dumpPath);
				return true;
			}

			void CCT_CALL Uninstall() override
			{
				if (!m_installed)
					return;

				sigaction(SIGABRT, &m_oldSigabrtAction, nullptr);
				sigaction(SIGSEGV, &m_oldSigsegvAction, nullptr);
				sigaction(SIGBUS, &m_oldSigbusAction, nullptr);
				sigaction(SIGFPE, &m_oldSigfpeAction, nullptr);

				m_installed = false;
			}

			void CCT_CALL HandleUnhandledException(std::exception_ptr eptr) override
			{
				cct::Logger::Error("Unhandled exception caught!");

				if (eptr)
				{
					try
					{
						std::rethrow_exception(eptr);
					}
					catch (const std::exception& e)
					{
						cct::Logger::Error("Exception: {}", e.what());
					}
					catch (...)
					{
						cct::Logger::Error("Unknown exception type");
					}
				}

				PrintStackTraceToFile();
				std::exit(EXIT_FAILURE);
			}

		private:
			static void EnableCoreDumps()
			{
				struct rlimit limit;
				limit.rlim_cur = RLIM_INFINITY;
				limit.rlim_max = RLIM_INFINITY;
				if (setrlimit(RLIMIT_CORE, &limit) == 0)
					cct::Logger::Info("Core dumps enabled (unlimited size)");
				else
					cct::Logger::Warning("Failed to enable core dumps");
			}

			static void SignalHandler(int signal)
			{
				cct::Logger::Error("Signal {} caught!", signal);
				if (s_instance != nullptr)
				{
					s_instance->PrintStackTraceToFile();
				}
				std::exit(EXIT_FAILURE);
			}

			void PrintStackTraceToFile()
			{
				auto trace = cpptrace::generate_trace();
				cct::Logger::Error("Stack trace:\n{}", trace.to_string());

				std::time_t now = std::time(nullptr);
				std::tm* localTime = std::localtime(&now);

				std::ostringstream filename;
				filename << m_dumpPath << "/vkd_crash_"
						 << std::put_time(localTime, "%Y%m%d_%H%M%S")
						 << ".log";

				try
				{
					std::ofstream file(filename.str());
					if (file.is_open())
					{
						file << "VKD Crash Report\n";
						file << "================\n";
						file << "Time: " << std::asctime(localTime);
						file << "\nStack Trace:\n";
						file << trace.to_string();
						file.close();

						cct::Logger::Info("Crash log written to: {}", filename.str());
					}
					else
					{
						cct::Logger::Error("Failed to open crash log file: {}", filename.str());
					}
				}
				catch (const std::exception& e)
				{
					cct::Logger::Error("Exception writing crash log: {}", e.what());
				}
			}
		};

#elif defined(CCT_PLATFORM_WINDOWS)

		class Win32CrashHandler : public CrashHandler
		{
		private:
			LPTOP_LEVEL_EXCEPTION_FILTER m_oldExceptionFilter;
			std::string m_dumpPath;
			bool m_installed;

			static inline Win32CrashHandler* s_instance = nullptr;

		public:
			Win32CrashHandler() noexcept
				: m_oldExceptionFilter(nullptr),
				  m_dumpPath("./dumps"),
				  m_installed(false)
			{
			}

			~Win32CrashHandler() override
			{
				if (m_installed)
					Uninstall();
				s_instance = nullptr;
			}

			bool CCT_CALL Install(std::string_view dumpPath = "./dumps") override
			{
				if (m_installed)
					return true;

				m_dumpPath = std::filesystem::absolute(dumpPath).string();
				s_instance = this;

				try
				{
					std::filesystem::create_directories(m_dumpPath);
				}
				catch (const std::filesystem::filesystem_error& e)
				{
					cct::Logger::Warning("Failed to create dump directory '{}': {}", m_dumpPath, e.what());
				}

				m_oldExceptionFilter = SetUnhandledExceptionFilter(Win32CrashHandler::ExceptionFilter);
				m_installed = true;
				cct::Logger::Info("Crash handler installed (Windows - Minidump enabled, dumps to: {})", m_dumpPath);
				return true;
			}

			void CCT_CALL Uninstall() override
			{
				if (!m_installed)
					return;

				SetUnhandledExceptionFilter(m_oldExceptionFilter);
				m_installed = false;
			}

			void CCT_CALL HandleUnhandledException(std::exception_ptr eptr) override
			{
				cct::Logger::Error("Unhandled exception caught!");

				if (eptr)
				{
					try
					{
						std::rethrow_exception(eptr);
					}
					catch (const std::exception& e)
					{
						cct::Logger::Error("Exception: {}", e.what());
					}
					catch (...)
					{
						cct::Logger::Error("Unknown exception type");
					}
				}

				PrintStackTraceAndDump();
				std::exit(EXIT_FAILURE);
			}

		private:
			static LONG WINAPI ExceptionFilter(EXCEPTION_POINTERS* exceptionPointers)
			{
				cct::Logger::Error("Structured exception caught! Code: 0x{:X}",
								   exceptionPointers->ExceptionRecord->ExceptionCode);

				if (s_instance != nullptr)
				{
					s_instance->PrintStackTraceAndDump(exceptionPointers);
				}
				std::exit(EXIT_FAILURE);

				return EXCEPTION_EXECUTE_HANDLER;
			}

			void PrintStackTraceAndDump(EXCEPTION_POINTERS* exceptionPointers = nullptr)
			{
				auto trace = cpptrace::generate_trace();
				cct::Logger::Error("Stack trace:\n{}", trace.to_string());

				std::time_t now = std::time(nullptr);
				std::tm* localTime = std::localtime(&now);

				std::ostringstream dumpFilename;
				dumpFilename << m_dumpPath << "/vkd_crash_"
							 << std::put_time(localTime, "%Y%m%d_%H%M%S")
							 << ".dmp";

				std::ostringstream logFilename;
				logFilename << m_dumpPath << "/vkd_crash_"
						   << std::put_time(localTime, "%Y%m%d_%H%M%S")
						   << ".log";

				HANDLE dumpFile = CreateFileA(dumpFilename.str().c_str(),
											  GENERIC_READ | GENERIC_WRITE,
											  0,
											  nullptr,
											  CREATE_ALWAYS,
											  FILE_ATTRIBUTE_NORMAL,
											  nullptr);

				if (dumpFile != INVALID_HANDLE_VALUE)
				{
					MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;
					if (exceptionPointers)
					{
						exceptionInfo.ThreadId = GetCurrentThreadId();
						exceptionInfo.ExceptionPointers = exceptionPointers;
						exceptionInfo.ClientPointers = TRUE;
					}

					MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
						MiniDumpWithFullMemory |
						MiniDumpWithFullMemoryInfo |
						MiniDumpWithHandleData |
						MiniDumpWithThreadInfo |
						MiniDumpWithUnloadedModules);

					BOOL success = MiniDumpWriteDump(
						GetCurrentProcess(),
						GetCurrentProcessId(),
						dumpFile,
						dumpType,
						exceptionPointers ? &exceptionInfo : nullptr,
						nullptr,
						nullptr);

					CloseHandle(dumpFile);

					if (success)
						cct::Logger::Info("Minidump written to: {}", dumpFilename.str());
					else
						cct::Logger::Warning("Failed to write minidump");
				}
				else
				{
					cct::Logger::Warning("Failed to create dump file");
				}

			try
			{
				std::ofstream logFile(logFilename.str());
				if (logFile.is_open())
				{
					logFile << "VKD Crash Report\n";
					logFile << "================\n";
					logFile << "Time: " << std::asctime(localTime);
					logFile << "\nStack Trace:\n";
					logFile << trace.to_string();
					logFile.close();

					cct::Logger::Info("Crash log written to: {}", logFilename.str());
				}
				else
				{
					cct::Logger::Error("Failed to open crash log file: {}", logFilename.str());
				}
			}
			catch (const std::exception& e)
			{
				cct::Logger::Error("Exception writing crash log: {}", e.what());
			}
			}
		};

#endif

	} // namespace

	std::unique_ptr<CrashHandler> CCT_CALL CrashHandler::Create()
	{
#if defined(CCT_PLATFORM_WINDOWS)
		return std::make_unique<Win32CrashHandler>();
#elif defined(CCT_PLATFORM_POSIX)
		return std::make_unique<PosixCrashHandler>();
#else
#error "No CrashHandler implementation for this platform"
#endif
	}

} // namespace vkd
