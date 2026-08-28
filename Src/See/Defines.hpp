/**
 * @file Defines.hpp
 * @brief Common definitions and macros for the See shader execution engine
 * @date 2026-08-27
 */

#pragma once

#ifdef SEE_PROFILING
#ifndef TRACY_ENABLE
#define TRACY_ENABLE
#endif
#include <source_location>

#include <tracy/Tracy.hpp>
#define SEE_PROFILER_SCOPE(name) ZoneScopedN(name)
#define SEE_AUTO_PROFILER_SCOPE() ZoneScoped
#else
#define SEE_PROFILER_SCOPE(name)
#define SEE_AUTO_PROFILER_SCOPE()
#endif
