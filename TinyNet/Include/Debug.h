#ifndef __DEBUG_H__
#define __DEBUG_H__
#include <iostream>

namespace tinynet
{
#define STD_Out		std::cout 
#define STD_Endl	std::endl

	void LogDebug(const char* func, int line, const char* format, ...);
#define DebugLog(format, ...) LogDebug(__FUNCTION__, __LINE__, format, __VA_ARGS__)

#if !defined(_WIN32) && !defined(_WIN64)
	void ErrDebug(const char* func, int line, const char* message);
#define DebugError(message) ErrDebug(__FUNCTION__, __LINE__, message)
#endif
}

#endif // !__DEBUG_H__


