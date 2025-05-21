#include <cstdarg>
#include "Debug.h"

#if !defined(_WIN32) && !defined(_WIN64)
#include <stdio.h>		// linux: perror
#include <string.h>		// linux: memset
#endif

namespace tinynet
{
	const size_t kBufferSize = 1024;
	char kDebugBuffer[kBufferSize + 1] = { 0 };

	void LogDebug(const char* func, int line, const char* format, ...)
	{
		memset(kDebugBuffer, 0, kBufferSize);

		va_list args;

		va_start(args, format);
		std::vsnprintf(kDebugBuffer, kBufferSize, format, args);
		va_end(args);

		STD_Out << kDebugBuffer;
	}

#if !defined(_WIN32) && !defined(_WIN64)
	void ErrDebug(const char* func, int line, const char* message)
	{
		STD_Out << func << " [" << line << "]: ";
		perror(message);
		STD_Out << STD_Endl;
	}
#endif
}



