#ifndef __NETDEBUG_H__
#define __NETDEBUG_H__
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>

#if !defined(_WIN32) && !defined(_WIN64)
#include <stdio.h>
#endif

namespace tinynet
{
#define ENABLE_DEBUG_CONSOLE
#define STD_Out		std::cout 
#define STD_Endl	std::endl

	enum class EDebugType
	{
		// 普通日志
		Info = 0,
		// 错误日志
		Error,
		// 警告日志
		Warn,
		// 流程日志
		Sequence,
	};

	class CNetDebug
	{
	public:
		CNetDebug();
		~CNetDebug();

		static std::shared_ptr<CNetDebug> GetInstance();

		void SetupLogFile(const std::string& n_sFileName = "");
		void Debug(EDebugType type, const char* func, int line, const char* format, ...);

	protected:
		std::ofstream	m_ofs;
		std::mutex		m_mutex;

		static std::shared_ptr<CNetDebug> m_hInst;
	};

//#if defined(_WIN32) || defined(_WIN64)
#define DebugSequence(format, ...) CNetDebug::GetInstance()->Debug(EDebugType::Sequence, __FUNCTION__, __LINE__, format, __VA_ARGS__)
#define DebugInfo(format, ...) CNetDebug::GetInstance()->Debug(EDebugType::Info, __FUNCTION__, __LINE__, format, __VA_ARGS__)
#define DebugError(format, ...) CNetDebug::GetInstance()->Debug(EDebugType::Error, __FUNCTION__, __LINE__, format, __VA_ARGS__)
#define DebugWarn(format, ...) CNetDebug::GetInstance()->Debug(EDebugType::Warn, __FUNCTION__, __LINE__, format, __VA_ARGS__)
//#else
//#define DebugInfo(format) perror(format)
//#define DebugError(format) perror(format)
//#endif
}

#endif // !__NETDEBUG_H__


