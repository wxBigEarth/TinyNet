#ifndef __TINYNET_H__
#define __TINYNET_H__
#include <list>
#include <string>
#include <thread>
#include <mutex>
#include <functional>
#include "Debug.h"

struct sockaddr;
struct sockaddr_in;
#define stSockaddrIn struct sockaddr_in
#define stSockaddr struct sockaddr
#define stIpMreq struct ip_mreq

namespace tinynet
{
	enum class ENetType
	{
		None = 0,
		TCP,
		UDP,
		// 组播接收者
		UDP_MULTICAST,
	};

	enum class ENetEvent
	{
		// 启动就绪
		Ready = 0,
		// TCP 接收到客户端
		Accept,
		// 初始化，由客户端发送，服务端返回远端 sockaddr
		Hello,
		// 心跳包，事件消息返回unsigned int 数组，依次是：序号-失败次数
		Heart,
		// 退出
		Quit,
	};

	// 获取本机CPU核数
	inline const unsigned int GetCpuNum();
	// 获取本地IP
	const std::string GetLocalIPAddress();

	int LastError();

	// 从 sockaddr 获取端口号
	unsigned short SockaddrToPort(char* n_szAddr);

	// 从 sockaddr 获取 IP
	std::string SockaddrToIp(char* n_szAddr);

	// 从 sockaddr 获取整形 IP
	unsigned long SockaddrToLongIp(char* n_szAddr);

	// sockaddr 转换整形 (IP << 16) | Port
	unsigned long long SockaddrToInteger(char* n_szAddr);

	// 关闭 socket
	void CloseSocket(size_t& n_nSocket);

#if defined(_WIN32) || defined(_WIN64)
#define SockaddrLen int
#define ValType const char*
#else
#define SockaddrLen socklen_t
#define ValType const void*
#define SOCKET_ERROR -1
#endif

#define IPADDR_SIZE 16
#define SOCKADDR_SIZE 16
#define EVENTMSGDATA_SIZE 16

#pragma region 网络节点
	struct FNetNode
	{
		// 协议
		ENetType		eNetType = ENetType::TCP;
		// Socket
		size_t			fd = 0;
		// Sockaddr
		char			Addr[SOCKADDR_SIZE] = {0};
		// 用户数据，适用于TCP服务端，指向为客户端分配的数据地址
		void*			UserData = nullptr;

		void Init(const ENetType n_eType,
			const std::string& n_sHost, const unsigned short n_nPort);

		void Init(const ENetType n_eType, const stSockaddrIn* n_Addr);

		// 端口号
		const unsigned short Port();

		// IP
		const std::string Ip();

		/// <summary>
		/// 发送消息
		/// </summary>
		/// <param name="n_szData">消息内容</param>
		/// <param name="n_nSize">消息长度</param>
		/// <returns></returns>
		int Send(const char* n_szData, const int n_nSize) const;
		int Send(const std::string& n_sData) const;

		/// <summary>
		/// 发送UDP消息给服务端外的用户
		/// </summary>
		/// <param name="n_szData">消息内容</param>
		/// <param name="n_nSize">消息长度</param>
		/// <param name="n_sHost">目标IP</param>
		/// <param name="n_nPort">目标端口</param>
		/// <returns></returns>
		int Send(const char* n_szData, const int n_nSize,
			const std::string& n_sHost, const unsigned short n_nPort) const;
		int Send(const std::string& n_sData,
			const std::string& n_sHost, const unsigned short n_nPort) const;

		const bool IsValid() const;

		const std::string ToString();

		bool operator==(const FNetNode& n_NetNode);

		void Clear();

	protected:
		/// <summary>
		/// 初始化，由客户端发送
		/// </summary>
		int Hello();

		/// <summary>
		/// 发送心跳包
		/// </summary>
		/// <param name="n_nNo">心跳序号</param>
		/// <param name="n_nFailCnt">序号失败次数</param>
		/// <returns></returns>
		/// 心跳包数据为2个unsigned int 组成，分别是 序号-失败次数
		int Heart(unsigned int n_nNo, unsigned int n_nFailCnt);

		/// <summary>
		/// 通知服务端或客户端退出
		/// </summary>
		int Quit();
	};
#pragma endregion

#pragma region 网络接口
	class INetImpl
	{
	public:
		INetImpl();
		virtual ~INetImpl();

	protected:
		void Startup();
		void Cleanup();

	protected:
#if defined(_WIN32) || defined(_WIN64)
		static int m_nRef;
#endif
	};
#pragma endregion

#pragma region 组播
	class CMulticast : public INetImpl
	{
	public:
		CMulticast();
		~CMulticast();

		/// <summary>
		/// 初始化
		/// </summary>
		/// <param name="n_sHost">组播IP</param>
		/// <param name="n_nPort">组播端口号</param>
		/*
		地址范围					含义

		224.0.0.0～224.0.0.255		永久组地址。IANA为路由协议预留的IP地址（也称为保留组地址），
									用于标识一组特定的网络设备，供路由协议、拓扑查找等使用，不用于组播转发。

		224.0.1.0～231.255.255.255	ASM组播地址，全网范围内有效。
		233.0.0.0～238.255.255.255	说明：其中，224.0.1.39和224.0.1.40是保留地址，不建议使用。

		232.0.0.0～232.255.255.255	缺省情况下的SSM组播地址，全网范围内有效。

		239.0.0.0～239.255.255.255	本地管理组地址，仅在本地管理域内有效。在不同的管理域内重复使用相同的本地管理组地址不会导致冲突。
		*/
		bool Init(const std::string& n_sHost, const unsigned short n_nPort);

		/// <summary>
		/// 启动组播(发送端)
		/// </summary>
		int Sender();

		/// <summary>
		/// 加入组播(接收端)
		/// </summary>
		/// <param name="n_sLocalIP">本机IP，为空则自动获取</param>
		/// <returns></returns>
		int Receiver(const std::string& n_sLocalIP = "");

		/// <summary>
		/// 退出组播
		/// </summary>
		/// <returns></returns>
		int Release();

		/// <summary>
		/// 发送组播消息
		/// </summary>
		/// <param name="n_szData">消息内容</param>
		/// <param name="n_nSize">消息长度</param>
		/// <returns></returns>
		int Send(const char* n_szData, const int n_nSize);
		int Send(const std::string n_sData);

		void SetRecvBuffSize(const int n_nSize);

		// 数据接收回调
		std::function<void(const std::string& )> fnRecvCallback = nullptr;
	protected:
		void MulticastThread() const;

	protected:
		int			m_nBuffSize = 1024;
		FNetNode	m_NetNode;

		std::string m_sLocalIp;
	};
#pragma endregion

#pragma region Socket 基类
	class ITinyNet : public INetImpl, public FNetNode
	{
	public:
		ITinyNet();
		virtual ~ITinyNet();

		/// <summary>
		/// 启动Socket
		/// </summary>
		/// <returns></returns>
		virtual bool Start();
		/// <summary>
		/// 退出
		/// </summary>
		virtual void Stop();

		/// <summary>
		/// 启用心跳包
		/// </summary>
		/// <param name="n_nPeriod">心跳周期(毫秒)</param>
		/// <param name="n_nTimeoutCnt">心跳允许超时次数</param>
		virtual void EnableHeart(unsigned int n_nPeriod, unsigned int n_nTimeoutCnt);
		
		int KeepAlive(int n_nAlive = 1) const;
		// 设置超时，建议在Start前设置
		int SetTimeout(int n_nMilliSeconds);
		// 设置 SO_REUSEADDR 
		int ReuseAddr(int n_nReuse) const;

		void SetRecvBuffSize(const int n_nSize);

		const bool IsRunning() const { return m_bRun; }

		/// <summary>
		/// 事件回调
		/// </summary>
		/// <param name="FNetNode*">产生事件的Socket节点</param>
		/// <param name="const ENetEvent">事件类型</param>
		/// <param name="const std::string&">事件消息</param>
		std::function<void(FNetNode*, const ENetEvent, const std::string&)> fnEventCallback = nullptr;

		/// <summary>
		/// 数据接收回调
		/// </summary>
		/// <param name="FNetNode*">产生数据的Socket节点</param>
		/// <param name="const std::string&">接收的数据</param>
		std::function<void(FNetNode*, const std::string&)> fnRecvCallback = nullptr;

	protected:
		// 事件消息
		virtual bool OnEventMessage(FNetNode* n_pNetNode, const std::string& n_sData);

		void OnEventCallback(FNetNode* n_pNetNode, 
			const ENetEvent n_eNetEvent, const std::string& n_sData);

		void OnRecvCallback(FNetNode* n_pNetNode, const std::string& n_sData);

	protected:
		int			m_nBuffSize = 1024;
		// 默认3秒超时
		int			m_nTimeout = 3000;

		bool		m_bRun = false;
	};
#pragma endregion

	////////////////////////////////////////////////////////////////////////////////
#pragma region 服务端
	class CTinyServer : public ITinyNet
	{
	public:
		CTinyServer();
		~CTinyServer();

		bool Start() override;
		void Stop() override;

		// 获取 TCP 客户端
		const std::list<FNetNode*>& GetClients();

	protected:
#if defined(_WIN32) || defined(_WIN64)
		bool InitSock();
		void Accept();
		bool CreateWorkerThread();
		void WorkerThread();

		// 投递接收
		int PostRecv(FNetNode* n_pNetNode);

		// 创建完成键
		void* AllocSocketNode(size_t n_fd, const stSockaddrIn* n_Addr);
		// 释放完成键
		void FreeSocketNode(FNetNode** n_pNetNode);
		// 释放所有完成键
		void FreeSocketNodes();
#else
		bool InitSock();
		void WorkerThread();

	public:
		void SetEt(const bool et = true);
	protected:
		int SetNonblock(int n_nFd);
		int AddSocketIntoPoll(FNetNode* n_pNetNode);
		int DelSocketFromPoll(int n_nFd);
		void FreeSocketNode(FNetNode* n_pNetNode);
		void FreeSocketNodes();
#endif

	protected:
		// 事件消息
		bool OnEventMessage(FNetNode* n_pNetNode, const std::string& n_sData);

	protected:
#if defined(_WIN32) || defined(_WIN64)
		void*			m_hIocp = nullptr;
		std::thread*	m_threads = nullptr;
#else
		int				m_nEpfd = 0;
		bool			m_bEt = true;
#endif
		std::mutex		m_mutex;
		std::list<FNetNode*> m_lstNodes;
	};
#pragma endregion

	////////////////////////////////////////////////////////////////////////////////
#pragma region 客户端
	class CTinyClient : public ITinyNet
	{
	public:
		CTinyClient();
		~CTinyClient();

		bool Start() override;
		void Stop() override;

		/// <summary>
		/// 启用心跳包
		/// </summary>
		/// <param name="n_nPeriod">心跳周期(毫秒)</param>
		/// <param name="n_nTimeoutCnt">心跳允许超时次数</param>
		/// 周期为0 表示禁用，默认禁用
		void EnableHeart(unsigned int n_nPeriod, unsigned int n_nTimeoutCnt) override;

		// 获取在服务端的端口号
		const unsigned short RemotePort();

		// 获取在服务端的 IP
		const std::string RemoteIp();

		// 手动发送心跳包
		int SendHeart();

	protected:
		bool InitSock();
		void WorkerThread();
		// 心跳线程
		void HeartThread();
		void Join();

		// 事件消息
		bool OnEventMessage(FNetNode* n_pNetNode, const std::string& n_sData);
		void QuitEvent();

	protected:
		std::thread	m_thread;

		// 在服务端的 IP 和 端口号信息
		char			m_szRemoteAddr[SOCKADDR_SIZE] = {0};
		// 心跳序号
		unsigned int	m_nHeartNo = 0;
		// 心跳周期
		unsigned int	m_nHeartPeriod = 0;
		// 心跳允许超时次数
		unsigned int 	m_nHeartTimeoutCnt = 0;
	};
#pragma endregion
}


#endif // !__TINYNET_H__
