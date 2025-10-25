//
// Created by arthur on 25/11/2025.
//

#ifndef VKD_SOFTWARE_DEFINE_HPP
#define VKD_SOFTWARE_DEFINE_HPP

#include <Concerto/Core/Types/Types.hpp>
#include <Concerto/Core/Assert.hpp>

#ifdef VKD_SOFTWARE_STATIC
	#define VKD_SOFTWARE_API
#else
	#ifdef VKD_SOFTWARE_BUILD
		#define VKD_SOFTWARE_API CCT_EXPORT
	#else
		#define VKD_SOFTWARE_API CCT_IMPORT
	#endif // VKD_SOFTWARE_BUILD
#endif // VKD_SOFTWARE_STATIC

#endif // VKD_SOFTWARE_DEFINE_HPP