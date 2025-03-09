#include <cstdarg>
#include "Debug.h"

#if !defined(_WIN32) && !defined(_WIN64)
#include <stdio.h>		// linux: perror
#include <string.h>		// linux: memset
#endif

namespace tinynet
{
	// Debug ÎÄ±¾»º´æ
	const size_t kBuffer2M = 2048;
	char kDebugBuffer[kBuffer2M + 1] = { 0 };

	const char* kSzLabel[] =
	{
		"[Info] ",
		"[Error] ",
		"[Warn] ",
		"[Sequence] ",
	};

	void DebugString(const char* szBuff) { STD_Out << szBuff; }
	void DebugLine(const char* szBuff) { STD_Out << szBuff << STD_Endl; }

	std::shared_ptr<CNetDebug> CNetDebug::m_hInst = std::make_shared<CNetDebug>();

	CNetDebug::CNetDebug()
	{
	}

	CNetDebug::~CNetDebug()
	{
#ifdef ENABLE_DEBUG_CONSOLE
		if (m_ofs.is_open()) m_ofs.close();
#endif
	}

	std::shared_ptr<CNetDebug> CNetDebug::GetInstance()
	{
		return m_hInst;
	}

	void CNetDebug::SetupLogFile(const std::string& n_sFileName)
	{
#ifdef ENABLE_DEBUG_CONSOLE
		if (!n_sFileName.empty()) m_ofs.open(n_sFileName, std::ios_base::out);
#endif
	}

	void CNetDebug::Debug(EDebugType type, const char* func, int line, const char* format, ...)
	{
#ifdef ENABLE_DEBUG_CONSOLE
		std::unique_lock<std::mutex> lock(m_mutex);

		int nSize = 0;
		va_list args;

		memset(kDebugBuffer, 0, sizeof(kDebugBuffer));
#if defined(_WIN32) || defined(_WIN64)
		sprintf_s(kDebugBuffer, kBuffer2M, "%s%s: %d \t", kSzLabel[(int)type], func, line);
#else
		sprintf(kDebugBuffer, "%s%s: %d \t", kSzLabel[(int)type], func, line);
#endif
		DebugString(kDebugBuffer);

		if (m_ofs.is_open())
		{
			m_ofs << kSzLabel[(int)type] << func << ": " << line << "\t";
		}

		va_start(args, format);

#if defined(_WIN32) || defined(_WIN64)
		nSize = _vscprintf(format, args);

		if ((size_t)nSize > kBuffer2M)
		{
			char* buffer = (char*)malloc(((size_t)nSize + 1) * sizeof(char));

			if (buffer != nullptr)
			{
				vsprintf_s(buffer, nSize, format, args);
				buffer[nSize] = '\0';

				DebugString(buffer);
				if (m_ofs.is_open()) m_ofs << buffer;

				free(buffer);
			}
		}
		else
		{
			memset(kDebugBuffer, 0, sizeof(kDebugBuffer));
			nSize = vsnprintf_s(kDebugBuffer, kBuffer2M, format, args);

			DebugString(kDebugBuffer);
			if (m_ofs.is_open()) m_ofs << kDebugBuffer;
		}
#else
		memset(kDebugBuffer, 0, sizeof(kDebugBuffer));
		vsnprintf(kDebugBuffer, kBuffer2M, format, args);

		DebugString(kDebugBuffer);
		if (m_ofs.is_open()) m_ofs << kDebugBuffer;
		perror("");
#endif

		va_end(args);
#endif
	}
}



