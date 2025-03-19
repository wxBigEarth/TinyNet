#include "TinyNet.h"

#if defined(_WIN32) || defined(_WIN64)
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h> //for socket
#include <arpa/inet.h>  //for htonl htons
#include <sys/epoll.h>  //for epoll_ctl
#include <unistd.h>     //for close
#include <fcntl.h>      //for fcntl
#include <errno.h>      //for errno
#include <ifaddrs.h>
#include <string.h>
#endif

//#if defined(_WIN32) || defined(_WIN64)
//#else
//#endif

namespace tinynet
{
	constexpr unsigned int kHelloId = (('0' << 24) | ('L' << 16) | ('E' << 8) | ('H'));
	constexpr unsigned int kHeartId = (('N' << 16) | ('I' << 8) | ('X'));
	constexpr unsigned int kQuitId = (('T' << 24) | ('I' << 16) | ('U' << 8) | ('Q'));

	const std::string kNetTypeString[] =
	{
		"TCP",
		"UDP",
		"MULTICAST",
	};

	inline const unsigned int GetCpuNum()
	{
		return std::thread::hardware_concurrency();
	}

	inline static int NetType2SockType(const ENetType n_eType)
	{
		int nType = SOCK_STREAM;

		if (n_eType == ENetType::TCP)
			nType = SOCK_STREAM;
		else if (n_eType == ENetType::UDP)
			nType = SOCK_DGRAM;

		return nType;
	}

	inline static stSockaddrIn* toSockaddrIn(void* n_szAddr) { return (stSockaddrIn*)n_szAddr; }

	inline static void BuildSockAddrIn(void* n_Addr, const std::string& n_sHost, const unsigned short n_nPort)
	{
		stSockaddrIn* pSockaddrIn = (stSockaddrIn*)n_Addr;

		toSockaddrIn(n_Addr)->sin_family = AF_INET;
		toSockaddrIn(n_Addr)->sin_port = htons(n_nPort);
		inet_pton(AF_INET, n_sHost.data(), &toSockaddrIn(n_Addr)->sin_addr.s_addr);
	}

	const std::string GetLocalIPAddress()
	{
		std::string sResult;
		int nResult = 0;
		char ipstr[32] = { 0 };

#if defined(_WIN32) || defined(_WIN64)
		char hostname[256] = { 0 };

		gethostname(hostname, sizeof(hostname));

		addrinfo hints = {}, * res = nullptr;

		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		nResult = getaddrinfo(hostname, nullptr, &hints, &res);
		if (nResult != 0) return sResult;

		for (addrinfo* ptr = res; ptr != nullptr; ptr = ptr->ai_next)
		{
			sockaddr_in* ipv4 = reinterpret_cast<sockaddr_in*>(ptr->ai_addr);

			if (ptr->ai_family == AF_INET)
			{
				inet_ntop(AF_INET, &(ipv4->sin_addr), ipstr, 32);
				sResult = ipstr;
				break;
			}
		}

		freeaddrinfo(res);
#else
		struct ifaddrs* ifaddr = nullptr;
		nResult = getifaddrs(&ifaddr);
		if (nResult != 0) return sResult;

		for (auto ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
		{
			if (ifa->ifa_addr &&
				ifa->ifa_addr->sa_family == AF_INET &&
				strcmp(ifa->ifa_name, "lo") != 0)
			{
				stSockaddrIn* ipAddr = reinterpret_cast<stSockaddrIn*>(ifa->ifa_addr);
				inet_ntop(AF_INET, &(ipAddr->sin_addr), ipstr, 32);
				sResult = ipstr;
				break;
			}
		}

		freeifaddrs(ifaddr);
#endif

		return sResult;
	}

	int LastError()
	{
#if defined(_WIN32) || defined(_WIN64)
		return WSAGetLastError();
#else
		return errno;
#endif
	}

	inline unsigned short GetPort(char* n_szAddr)
	{
		return ntohs(toSockaddrIn(n_szAddr)->sin_port);
	}

	inline std::string GetIp(char* n_szAddr)
	{
		char sHost[IPADDR_SIZE] = { 0 };

		inet_ntop(AF_INET,
			&toSockaddrIn(n_szAddr)->sin_addr.s_addr,
			sHost,
			IPADDR_SIZE);

		return std::string(sHost);
	}

	inline void CloseSocket(size_t& n_nSocket)
	{
		if (n_nSocket > 0)
		{
#if defined(_WIN32) || defined(_WIN64)
			closesocket(n_nSocket);
#else
			close(n_nSocket);
#endif
			n_nSocket = 0;
		}
	}

	////////////////////////////////////////////////////////////////////////////////
#pragma region 事件消息
	struct FEventMsg
	{
		// 消息Id，系统维护
		unsigned int Id = 0;
	};
	struct FHeart : public FEventMsg
	{
		// 发送者，系统维护
		unsigned int Sender = 0;
		// 序号，若心跳返回，则序号会递增
		unsigned int No = 0;
		// 失败次数，更新序号时需重置
		unsigned int Cnt = 0;
	};
#pragma endregion

	////////////////////////////////////////////////////////////////////////////////
#pragma region 节点
	void FNetNode::Init(const ENetType n_eType,
		const std::string& n_sHost, const unsigned short n_nPort)
	{
		eNetType = n_eType;

		BuildSockAddrIn(&Addr, n_sHost, n_nPort);
	}

	void FNetNode::Init(const ENetType n_eType, const stSockaddrIn* n_Addr)
	{
		eNetType = n_eType;
		if (n_Addr) memcpy(Addr, n_Addr, sizeof(stSockaddrIn));
	}

	const unsigned short FNetNode::Port()
	{
		return GetPort(Addr);
	}

	const std::string FNetNode::Ip()
	{
		return GetIp(Addr);
	}

	int FNetNode::Send(const char* n_szData, const int n_nSize) const
	{
		int nResult = 0;

		if (!IsValid()) return nResult;

		if (eNetType == ENetType::TCP)
		{
			nResult = send(fd, n_szData, n_nSize, 0);
		}
		else if (eNetType == ENetType::UDP)
		{
			nResult = sendto(fd, n_szData, n_nSize, 0,
				(stSockaddr*)Addr, sizeof(stSockaddr));
		}

		return nResult;
	}

	int FNetNode::Send(const std::string& n_sData) const
	{
		return Send(n_sData.c_str(), (int)n_sData.size());
	}

	int FNetNode::Send(const char* n_szData, const int n_nSize,
		const std::string& n_sHost, const unsigned short n_nPort) const
	{
		if (!IsValid() || eNetType == ENetType::TCP) return 0;

		stSockaddrIn OtherAddr = { 0 };
		BuildSockAddrIn(&OtherAddr, n_sHost, n_nPort);

		return sendto(fd, n_szData, n_nSize, 0,
			(stSockaddr*)&OtherAddr, sizeof(stSockaddr));
	}

	int FNetNode::Send(const std::string& n_sData,
		const std::string& n_sHost, const unsigned short n_nPort) const
	{
		return Send(n_sData.c_str(), (int)n_sData.size(), n_sHost, n_nPort);
	}

	const bool FNetNode::IsValid() const
	{
		return fd > 0;
	}

	const std::string FNetNode::ToString()
	{
		char szBuff[64] = { 0 };

#if defined(_WIN32) || defined(_WIN64)
		int nLen = sprintf_s(szBuff, sizeof(szBuff), "%s %s:%d ",
			kNetTypeString[(int)eNetType].data(),
			Ip().data(),
			Port()
		);
#else
		int nLen = sprintf(szBuff, "%s %s:%d ",
			kNetTypeString[(int)eNetType].data(),
			Ip().data(),
			Port()
		);
#endif

		return std::string(szBuff, nLen);
	}

	bool FNetNode::operator==(const FNetNode& n_NetNode)
	{
		return memcmp(Addr, n_NetNode.Addr, 0) == 0;
	}

	void FNetNode::Clear()
	{
		fd = 0;
		memset(Addr, 0, sizeof(stSockaddrIn));
	}

	int FNetNode::Hello()
	{
		FEventMsg EventMsg;
		EventMsg.Id = kHelloId;

		return Send((const char*)&EventMsg, sizeof(EventMsg));
	}

	int FNetNode::Heart(unsigned int n_nNo, unsigned int n_nFailCnt)
	{
		FHeart Heart;
		Heart.Id = kHeartId;
		Heart.Sender = (unsigned int)fd;
		Heart.No = n_nNo;
		Heart.Cnt = n_nFailCnt;

		return Send((const char*)&Heart, sizeof(FHeart));
	}

	int FNetNode::Quit()
	{
		if (eNetType != ENetType::UDP) return 0;
		// TCP 不用通知
		FEventMsg EventMsg;
		EventMsg.Id = kQuitId;

		return Send((const char*)&EventMsg, sizeof(EventMsg));
	}
#pragma endregion

	////////////////////////////////////////////////////////////////////////////////
#pragma region 公共基类
#if defined(_WIN32) || defined(_WIN64)
	int INetImpl::m_nRef = 0;
#endif

	INetImpl::INetImpl()
	{
		Startup();
	}

	INetImpl::~INetImpl()
	{
		Cleanup();
	}

	void INetImpl::Startup()
	{
#if defined(_WIN32) || defined(_WIN64)
		if (m_nRef == 0)
		{
			WSADATA wsaData;
			int nResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
			if (nResult != 0) DebugError("初始化 Socket 失败\n");
		}

		m_nRef++;
#endif
	}

	void INetImpl::Cleanup()
	{
#if defined(_WIN32) || defined(_WIN64)
		m_nRef--;

		if (m_nRef <= 0)
		{
			WSACleanup();
			m_nRef = 0;
		}
#endif
	}
#pragma endregion

	////////////////////////////////////////////////////////////////////////////////
#pragma region 组播
	CMulticast::CMulticast()
	{
	}

	CMulticast::~CMulticast()
	{
		Release();
	}

	bool CMulticast::Init(const std::string& n_sHost, const unsigned short n_nPort)
	{
		if (m_NetNode.IsValid()) return true;

		m_NetNode.fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (m_NetNode.fd == SOCKET_ERROR)
		{
			DebugError("create Socket error: %d\n", LastError());
		}
		else
		{
			BuildSockAddrIn(&m_NetNode.Addr, n_sHost, n_nPort);
		}

		return m_NetNode.IsValid();
	}

	int CMulticast::Sender()
	{
		if (!m_NetNode.IsValid()) return -1;
		m_NetNode.eNetType = ENetType::UDP;
		return 0;
	}

	int CMulticast::Receiver(const std::string& n_sLocalIP)
	{
		int nResult = -1;
		if (!m_NetNode.IsValid()) return nResult;

		do
		{
			m_NetNode.eNetType = ENetType::UDP_MULTICAST;

			m_sLocalIp = n_sLocalIP;
			if (m_sLocalIp.empty()) m_sLocalIp = GetLocalIPAddress();

			stSockaddrIn Addr = { 0 };
			BuildSockAddrIn(&Addr, m_sLocalIp, m_NetNode.Port());

			nResult = bind(m_NetNode.fd, (stSockaddr*)&Addr, sizeof(stSockaddr));
			if (nResult == -1)
			{
				DebugError("multicast bind Socket error: %d\n", LastError());
				break;
			}

			stIpMreq IpMreq = { 0 };
			memcpy(&IpMreq.imr_multiaddr,
				&toSockaddrIn(m_NetNode.Addr)->sin_addr,
				sizeof(struct in_addr));
			inet_pton(AF_INET, m_sLocalIp.c_str(), &IpMreq.imr_interface.s_addr);

			nResult = setsockopt(
				m_NetNode.fd,
				IPPROTO_IP,
				IP_ADD_MEMBERSHIP,
				(ValType)&IpMreq,
				sizeof(stIpMreq));

			if (nResult < 0)
			{
				DebugError("setsockopt IP_ADD_MEMBERSHIP error: %d\n", LastError());
				break;
			}

			std::thread(&CMulticast::MulticastThread, this).detach();

		} while (false);

		return nResult;
	}

	int CMulticast::Release()
	{
		if (!m_NetNode.IsValid()) return -1;

		if (m_NetNode.eNetType == ENetType::UDP_MULTICAST)
		{
			stIpMreq IpMreq = { 0 };
			memcpy(&IpMreq.imr_multiaddr,
				&toSockaddrIn(m_NetNode.Addr)->sin_addr,
				sizeof(struct in_addr));
			inet_pton(AF_INET, m_sLocalIp.c_str(), &IpMreq.imr_interface.s_addr);

			auto nResult = setsockopt(
				m_NetNode.fd,
				IPPROTO_IP,
				IP_DROP_MEMBERSHIP,
				(ValType)&IpMreq,
				sizeof(stIpMreq));

			if (nResult < 0)
				DebugError("setsockopt IP_DROP_MEMBERSHIP error: %d\n", LastError());
		}

		CloseSocket(m_NetNode.fd);
		m_NetNode.Clear();

		return 0;
	}

	int CMulticast::Send(const char* n_szData, const int n_nSize)
	{
		if (m_NetNode.eNetType != ENetType::UDP) return 0;
		return m_NetNode.Send(n_szData, n_nSize);
	}

	int CMulticast::Send(const std::string n_sData)
	{
		if (m_NetNode.eNetType != ENetType::UDP) return 0;
		return m_NetNode.Send(n_sData);
	}

	void CMulticast::SetRecvBuffSize(const int n_nSize)
	{
		m_nBuffSize = n_nSize;
	}

	void CMulticast::MulticastThread() const
	{
		int nResult = 0;

		char* szBuff = (char*)malloc(m_nBuffSize);
		if (!szBuff) return;

		stSockaddrIn	Addr = { 0 };
		SockaddrLen		nLen = sizeof(stSockaddrIn);

		while (true)
		{
			nResult = recvfrom(m_NetNode.fd,
				szBuff, m_nBuffSize, 0, (stSockaddr*)&Addr, &nLen);
			if (nResult <= 0) break;

			if (fnRecvCallback) fnRecvCallback(std::string(szBuff, nResult));
			memset(szBuff, 0, m_nBuffSize);
		}

		memset(szBuff, 0, m_nBuffSize);
	}
#pragma endregion

	////////////////////////////////////////////////////////////////////////////////
#pragma region Socket基类

	ITinyNet::ITinyNet()
	{
	}

	ITinyNet::~ITinyNet()
	{
	}

	bool ITinyNet::Start()
	{
		return true;
	}

	void ITinyNet::Stop()
	{
	}

	void ITinyNet::EnableHeart(unsigned int n_nPeriod, unsigned int n_nTimeoutCnt)
	{
	}

	int ITinyNet::KeepAlive(int n_nAlive) const
	{
		if (!IsValid()) return 0;
		if (eNetType != ENetType::TCP) return 0;

		int keepAlive = 1;
		//int keepIdle = 60; // 空闲时间
		//int keepInterval = 5; // 探测间隔
		//int keepCount = 3; // 探测次数

		int nResult = setsockopt(
			fd,
			SOL_SOCKET,
			SO_KEEPALIVE,
			(ValType)&keepAlive,
			sizeof(keepAlive));

		if (nResult == -1)
			DebugError("setsockopt KeepAlive error: %d\n", LastError());

		// ... 设置 TCP_KEEPIDLE, TCP_KEEPINTVL 和 TCP_KEEPCNT
		return nResult;
	}

	int ITinyNet::SetTimeout(int n_nMilliSeconds)
	{
		if (n_nMilliSeconds > 0)
			m_nTimeout = n_nMilliSeconds;
		if (!IsValid()) return 0;

		int nResult = 0;

		do
		{
#if defined(_WIN32) || defined(_WIN64)
			int tv = m_nTimeout;
#else
			struct timeval tv;

			// Set timeout for sending and receiving
			tv.tv_sec = m_nTimeout / 1000;  // Timeout in seconds
			tv.tv_usec = 0;					// Timeout in microseconds
#endif
			// 设置接收超时
			nResult = setsockopt(
				fd,
				SOL_SOCKET,
				SO_RCVTIMEO,
				(ValType)&tv,
				sizeof(tv));

			if (nResult == -1)
			{
				DebugError("setsockopt SO_RCVTIMEO error: %d\n", LastError());
				break;
			}

			// 设置发送超时
			nResult = setsockopt(
				fd,
				SOL_SOCKET,
				SO_SNDTIMEO,
				(ValType)&tv,
				sizeof(tv));

			if (nResult == -1)
			{
				DebugError("setsockopt SO_SNDTIMEO error: %d\n", LastError());
				break;
			}

		} while (false);

		return nResult;
	}

	int ITinyNet::ReuseAddr(int n_nReuse) const
	{
		if (!IsValid()) return 0;

		int nResult = setsockopt(
			fd,
			SOL_SOCKET,
			SO_REUSEADDR,
			(ValType)&n_nReuse,
			sizeof(n_nReuse));

		if (nResult == -1)
			DebugError("setsockopt SO_REUSEADDR error: %d\n", LastError());

		return nResult;
	}

	void ITinyNet::SetRecvBuffSize(const int n_nSize)
	{
		m_nBuffSize = n_nSize;
	}

	bool ITinyNet::OnEventMessage(FNetNode* n_pNetNode, const std::string& n_sData)
	{
		return false;
	}

	void ITinyNet::OnEventCallback(FNetNode* n_pNetNode,
		const ENetEvent n_eNetEvent, const std::string& n_sData)
	{
		if (fnEventCallback) fnEventCallback(n_pNetNode, n_eNetEvent, n_sData);
	}

	void ITinyNet::OnRecvCallback(FNetNode* n_pNetNode, const std::string& n_sData)
	{
		if (fnRecvCallback) fnRecvCallback(n_pNetNode, n_sData);
	}

#pragma endregion

	////////////////////////////////////////////////////////////////////////////////
#pragma region 服务端
#if defined(_WIN32) || defined(_WIN64)
	struct FNetHandle
	{
		WSAOVERLAPPED	Overlapped = { 0 };
		WSABUF			DataBuf = { 0 };
		FNetNode* NetNode = nullptr;
		char* Buffer = nullptr;
	};

	struct FIOCPNetNode : public FNetNode
	{
		FNetHandle* Handle;
	};
#else
	static int EPOLL_SIZE = 4096;
#endif

	CTinyServer::CTinyServer()
	{
	}

	CTinyServer::~CTinyServer()
	{
		Stop();
	}

	bool CTinyServer::Start()
	{
		if (IsValid()) return false;

		return InitSock();
	}

#if defined(_WIN32) || defined(_WIN64)
	void CTinyServer::Stop()
	{
		if (!m_hIocp) return;
		ITinyNet::Stop();

		// 退出所有工作线程
		auto nNum = GetCpuNum() * 2;
		for (unsigned int i = 0; i < nNum; i++)
			PostQueuedCompletionStatus(m_hIocp, 0, 0, 0);

		for (unsigned int i = 0; i < nNum; i++)
			m_threads[i].join();

		delete[] m_threads;
		m_threads = nullptr;

		m_bRun = false;
		// 关闭Socket
		CloseSocket(fd);
		// 释放所有为客户端分配的内存
		FreeSocketNodes();
		// 关闭完成端口
		CloseHandle(m_hIocp);
		m_hIocp = nullptr;

		OnEventCallback(this, ENetEvent::Quit, "");
	}
#else
	void CTinyServer::Stop()
	{
		if (!IsValid()) return;
		ITinyNet::Stop();

		m_bRun = false;
		FreeSocketNodes();

		CloseSocket(fd);
		close(m_nEpfd);
		m_nEpfd = 0;

		OnEventCallback(this, ENetEvent::Quit, "");
	}
#endif

	const std::list<FNetNode*>& CTinyServer::GetClients()
	{
		return m_lstNodes;
	}

#if defined(_WIN32) || defined(_WIN64)
	bool CTinyServer::InitSock()
	{
		int nResult = 0;

		int nSockType = NetType2SockType(eNetType);

		do
		{
			m_bRun = false;

			m_hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
			if (!m_hIocp)
			{
				DebugError("创建完成端口失败\n");
				break;
			}

			fd = WSASocketW(AF_INET, nSockType, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			if (fd == SOCKET_ERROR)
			{
				DebugError("创建 Socket 失败\n");
				break;
			}

			KeepAlive(1);
			SetTimeout(-1);
			ReuseAddr(1);

			nResult = bind(fd, (stSockaddr*)Addr, sizeof(stSockaddr));
			if (nResult == SOCKET_ERROR)
			{
				DebugError("绑定 Socket 失败\n");
				break;
			}

			if (eNetType == ENetType::TCP)
			{
				nResult = listen(fd, SOMAXCONN);
				if (nResult == SOCKET_ERROR)
				{
					DebugError("监听 Socket 失败\n");
					break;
				}
			}
			else if (eNetType == ENetType::UDP)
			{
				CreateIoCompletionPort(
					(HANDLE)fd, m_hIocp, (ULONG_PTR)fd, 0
				);
			}

			if (!CreateWorkerThread()) break;

			if (eNetType == ENetType::TCP)
			{
				std::thread(&CTinyServer::Accept, this).detach();
			}
			else if (eNetType == ENetType::UDP)
			{
				auto UdpHandle = (FIOCPNetNode*)AllocSocketNode(fd, nullptr);
				PostRecv(UdpHandle);
			}

			OnEventCallback(this, ENetEvent::Ready, "");

			m_bRun = true;

		} while (false);

		if (!m_bRun) CloseSocket(fd);

		return m_bRun;
	}

	void CTinyServer::Accept()
	{
		int				nResult = 0;

		SOCKET			ClientSocket = -1;
		stSockaddrIn	ClientAddr = { 0 };
		SockaddrLen		AddrLen = sizeof(stSockaddrIn);

		while (true)
		{
			ClientSocket = WSAAccept(
				fd, (stSockaddr*)&ClientAddr, &AddrLen, NULL, NULL
			);

			if (ClientSocket == SOCKET_ERROR)
			{
				if (m_bRun) DebugError("Accept 错误: %d\n", LastError());
				break;
			}

			auto TcpHandle = (FIOCPNetNode*)AllocSocketNode(ClientSocket, &ClientAddr);

			OnEventCallback(TcpHandle, ENetEvent::Accept, "");

			CreateIoCompletionPort(
				(HANDLE)ClientSocket, m_hIocp, (ULONG_PTR)TcpHandle, 0
			);

			nResult = PostRecv(TcpHandle);

			if (nResult == SOCKET_ERROR && LastError() != WSA_IO_PENDING)
			{
				DebugError("IO Pending 错误 : %d", LastError());
				break;
			}
		}
	}

	bool CTinyServer::CreateWorkerThread()
	{
		auto nNum = GetCpuNum() * 2;
		m_threads = new std::thread[nNum];

		for (unsigned int i = 0; i < nNum; i++)
		{
			m_threads[i] = std::thread(&CTinyServer::WorkerThread, this);
		}

		return true;
	}

	void CTinyServer::WorkerThread()
	{
		BOOL	bResult = FALSE;
		int		nResult = 0;
		DWORD	nRecvBytes = 0;

		FNetHandle* pCompletionKey = nullptr;
		FNetHandle* pSocketInfo = nullptr;

		while (true)
		{
			bResult = GetQueuedCompletionStatus(m_hIocp,
				&nRecvBytes,
				(PULONG_PTR)&pCompletionKey,		// completion key
				(LPOVERLAPPED*)&pSocketInfo,		// overlapped I/O
				INFINITE
			);

			if (!pCompletionKey || !pSocketInfo)
				break;

			pSocketInfo->DataBuf.len = nRecvBytes;

			if (!bResult || nRecvBytes == 0)
			{
				FreeSocketNode(&pSocketInfo->NetNode);
				continue;
			}
			else
			{
				auto sData = std::string(pSocketInfo->Buffer, nRecvBytes);
				if (!OnEventMessage(pSocketInfo->NetNode, sData))
				{
					OnRecvCallback(pSocketInfo->NetNode, sData);
				}

				memset(&(pSocketInfo->Overlapped), 0, sizeof(OVERLAPPED));
				pSocketInfo->DataBuf.len = m_nBuffSize;
				pSocketInfo->DataBuf.buf = pSocketInfo->Buffer;
				memset(pSocketInfo->Buffer, 0, m_nBuffSize);

				nResult = PostRecv(pSocketInfo->NetNode);

				if (nResult == SOCKET_ERROR && LastError() != WSA_IO_PENDING)
				{
					DebugError("WSARecv 错误 : %d", LastError());
				}
			}
		}
	}

	int CTinyServer::PostRecv(FNetNode* n_pNetNode)
	{
		int		nResult = 0;
		if (!n_pNetNode) return nResult;

		auto pNetNode = (FIOCPNetNode*)n_pNetNode;
		if (!pNetNode->Handle) return nResult;

		DWORD	dwFlags = 0;
		DWORD	nRecvBytes = 0;

		if (eNetType == ENetType::TCP)
		{
			nResult = WSARecv(
				pNetNode->fd,
				&pNetNode->Handle->DataBuf,
				1,
				(LPDWORD)&nRecvBytes,
				&dwFlags,
				&(pNetNode->Handle->Overlapped),
				nullptr
			);
		}
		else if (eNetType == ENetType::UDP)
		{
			SockaddrLen nLen = sizeof(stSockaddrIn);
			nResult = WSARecvFrom(
				pNetNode->fd,
				&pNetNode->Handle->DataBuf,
				1,
				(LPDWORD)&nRecvBytes,
				&dwFlags,
				(stSockaddr*)pNetNode->Addr,
				&nLen,
				&(pNetNode->Handle->Overlapped),
				nullptr
			);
		}

		return nResult;
	}

	void* CTinyServer::AllocSocketNode(size_t n_fd, const stSockaddrIn* n_Addr)
	{
		auto NetNode = (FIOCPNetNode*)malloc(sizeof(FIOCPNetNode));
		if (!NetNode) return nullptr;

		NetNode->Handle = (FNetHandle*)malloc(sizeof(FNetHandle));
		if (!NetNode->Handle)
		{
			free(NetNode);
			return nullptr;
		}

		NetNode->Handle->NetNode = NetNode;

		NetNode->fd = n_fd;
		NetNode->Init(eNetType, n_Addr);
		memset(&(NetNode->Handle->Overlapped), 0, sizeof(OVERLAPPED));

		// 分配接收数据缓存
		NetNode->Handle->Buffer = (char*)malloc(m_nBuffSize);
		if (NetNode->Handle->Buffer) memset(NetNode->Handle->Buffer, 0, m_nBuffSize);
		NetNode->Handle->DataBuf.len = m_nBuffSize;
		NetNode->Handle->DataBuf.buf = NetNode->Handle->Buffer;

		std::unique_lock<std::mutex> lock(m_mutex);
		m_lstNodes.push_back(NetNode);

		return NetNode;
	}

	void CTinyServer::FreeSocketNode(FNetNode** n_pNetNode)
	{
		if (!n_pNetNode || !*n_pNetNode) return;

		auto pNetNode = (FIOCPNetNode*)(*n_pNetNode);
		if (!pNetNode->Handle) return;

		// 抛出退出事件
		OnEventCallback(*n_pNetNode, ENetEvent::Quit, "");

		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_lstNodes.remove(*n_pNetNode);
		}

		CloseSocket(pNetNode->fd);
		free(pNetNode->Handle->Buffer);
		free(pNetNode->Handle);
		free(pNetNode);
		*n_pNetNode = nullptr;
	}

	void CTinyServer::FreeSocketNodes()
	{
		std::unique_lock<std::mutex> lock(m_mutex);

		auto it = m_lstNodes.begin();
		while (it != m_lstNodes.end())
		{
			auto pNetNode = (FIOCPNetNode*)(*it);
			m_lstNodes.erase(it++);

			CloseSocket(pNetNode->fd);
			free(pNetNode->Handle->Buffer);
			free(pNetNode->Handle);
			free(pNetNode);
		}
	}
#else

	bool CTinyServer::InitSock()
	{
		int nResult = 0;

		int nSockType = NetType2SockType(eNetType);

		do
		{
			m_bRun = false;

			fd = socket(AF_INET, nSockType, 0);
			if (fd == -1)
			{
				DebugError("create Socket error: %d\n", LastError());
				break;
			}

			KeepAlive(1);
			SetTimeout(-1);
			ReuseAddr(1);

			m_nEpfd = epoll_create(1);
			if (m_nEpfd == -1)
			{
				DebugError("create EPoll error: %d\n", LastError());
				break;
			}

			/** 添加Epoll事件. */
			nResult = AddSocketIntoPoll(this);
			if (nResult == -1)
			{
				DebugError("Add EPoll eventl error: %d\n", LastError());
				break;
			}

			nResult = bind(fd, (stSockaddr*)Addr, sizeof(stSockaddr));
			if (nResult == -1)
			{
				DebugError("bind Socket error: %d\n", LastError());
				break;
			}

			if (eNetType == ENetType::TCP)
			{
				nResult = listen(fd, SOMAXCONN);
				if (nResult == -1)
				{
					DebugError("listen Socket error: %d\n", LastError());
					break;
				}
			}

			std::thread(&CTinyServer::WorkerThread, this).detach();

			OnEventCallback(this, ENetEvent::Ready, "");

			m_bRun = true;

		} while (false);

		if (!m_bRun) CloseSocket(fd);

		return m_bRun;
	}

	void CTinyServer::WorkerThread()
	{
		if (m_nEpfd == 0) return;

		int					nResult = 0;
		struct epoll_event	Event[EPOLL_SIZE];

		stSockaddrIn		RemoteAddr = { 0 };
		SockaddrLen			nLen = sizeof(stSockaddrIn);

		FNetNode			NetNode;
		NetNode.fd = fd;
		NetNode.eNetType = eNetType;

		char* szBuff = (char*)malloc(m_nBuffSize);
		if (!szBuff) return;
		memset(szBuff, 0, m_nBuffSize);

		while (true)
		{
			// nCount表示就绪事件的数目
			int nCount = epoll_wait(m_nEpfd, Event, EPOLL_SIZE, -1);
			if (nCount < 0)
			{
				if (m_bRun) DebugError("epoll_wait error: %d\n", LastError());
				break;
			}

			for (int i = 0; i < nCount; ++i)
			{
				if (!(Event[i].events & EPOLLIN)) continue;

				// 新用户连接
				auto pNetNode = (FNetNode*)Event[i].data.ptr;
				if (pNetNode->fd == fd && eNetType == ENetType::TCP)
				{
					int nFd = accept(fd, (stSockaddr*)&RemoteAddr, &nLen);
					if (nFd == -1)
					{
						DebugError("accept error: %d\n", LastError());
						continue;
					}

					auto RemoteNetNode = (FNetNode*)malloc(sizeof(FNetNode));
					if (!RemoteNetNode) continue;
					RemoteNetNode->fd = nFd;
					RemoteNetNode->Init(ENetType::TCP, &RemoteAddr);

					{
						std::unique_lock<std::mutex> lock(m_mutex);
						m_lstNodes.push_back(RemoteNetNode);
					}

					// 把这个新的客户端添加到内核事件列表
					AddSocketIntoPoll(RemoteNetNode);

					OnEventCallback(RemoteNetNode, ENetEvent::Accept, "");
				}
				else
				{
					// 客户端唤醒, 处理用户发来的消息
					memset(szBuff, 0, m_nBuffSize);

					if (eNetType == ENetType::TCP)
						nResult = recv(pNetNode->fd, szBuff, m_nBuffSize, 0);
					else if (eNetType == ENetType::UDP)
					{
						nResult = recvfrom(pNetNode->fd, szBuff, m_nBuffSize, 0,
							(stSockaddr*)NetNode.Addr, &nLen);
					}

					if (nResult <= 0)
					{
						FreeSocketNode(pNetNode);
						if (m_bRun) DebugInfo("socket quit: %d\n", LastError());
						continue;
					}

					auto p = pNetNode;
					if (p->eNetType == ENetType::UDP) p = &NetNode;

					auto sData = std::string(szBuff, nResult);
					if (!OnEventMessage(p, sData))
					{
						OnRecvCallback(p, sData);
					}
				}
			}
		}
	}

	void CTinyServer::SetEt(const bool et)
	{
		m_bEt = et;
	}

	int CTinyServer::SetNonblock(int n_nFd)
	{
		/** 设置为非阻塞. */
		int nFlag = fcntl(n_nFd, F_GETFL, 0);
		nFlag |= O_NONBLOCK;

		return fcntl(n_nFd, F_SETFL, nFlag);
	}

	int CTinyServer::AddSocketIntoPoll(FNetNode* n_pNetNode)
	{
		if (!n_pNetNode->IsValid()) return -1;

		struct epoll_event ev;
		ev.data.ptr = n_pNetNode;
		ev.events = EPOLLIN;
		if (m_bEt) ev.events = EPOLLIN | EPOLLET;

		int nRet = epoll_ctl(m_nEpfd, EPOLL_CTL_ADD, n_pNetNode->fd, &ev);
		if (nRet == -1) return nRet;

		return SetNonblock(n_pNetNode->fd);
	}

	int CTinyServer::DelSocketFromPoll(int n_nFd)
	{
		if (m_nEpfd == 0) return -1;
		return epoll_ctl(m_nEpfd, EPOLL_CTL_DEL, n_nFd, NULL);
	}

	void CTinyServer::FreeSocketNode(FNetNode* n_pNetNode)
	{
		if (!n_pNetNode) return;

		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_lstNodes.remove(n_pNetNode);
		}

		DelSocketFromPoll(n_pNetNode->fd);
		CloseSocket(n_pNetNode->fd);
	}

	void CTinyServer::FreeSocketNodes()
	{
		auto it = m_lstNodes.begin();
		while (it != m_lstNodes.end())
		{
			auto pNetNode = *it;
			m_lstNodes.erase(it++);

			DelSocketFromPoll(pNetNode->fd);
			CloseSocket(pNetNode->fd);
			free(pNetNode);
		}
	}
#endif

	bool CTinyServer::OnEventMessage(FNetNode* n_pNetNode, const std::string& n_sData)
	{
		bool bResult = true;

		auto pEventMsg = (FEventMsg*)n_sData.data();
		switch (pEventMsg->Id)
		{
		case kHelloId:
		{
			OnEventCallback(n_pNetNode, ENetEvent::Hello,
				std::string(n_pNetNode->Addr, SOCKADDR_SIZE)
			);

			const size_t nLen = sizeof(unsigned int) + SOCKADDR_SIZE;
			char szBuff[nLen] = { 0 };
			memcpy(szBuff, &kHelloId, sizeof(unsigned int));
			memcpy(szBuff + sizeof(unsigned int), n_pNetNode->Addr, SOCKADDR_SIZE);
			n_pNetNode->Send(szBuff, nLen);
		}
		break;
		case kHeartId:
		{
			auto pHeart = (FHeart*)pEventMsg;
			OnEventCallback(n_pNetNode,
				ENetEvent::Heart,
				std::string(n_sData.data() + sizeof(unsigned int) * 2, sizeof(unsigned int) * 2)
			);

			// 客户端发的心跳包需回消息，否则通过事件处理后续逻辑
			if (pHeart->Sender != (unsigned int)fd)
			{
				pHeart->No++;
				n_pNetNode->Send(n_sData);
			}
		}
		break;
		case kQuitId:
		{
			OnEventCallback(n_pNetNode, ENetEvent::Quit, "");
		}
		break;
		default:
			bResult = false;
			break;
		}

		return bResult;
	}

#pragma endregion


	////////////////////////////////////////////////////////////////////////////////
#pragma region 客户端
	CTinyClient::CTinyClient()
	{
	}

	CTinyClient::~CTinyClient()
	{
		Stop();
	}

	bool CTinyClient::Start()
	{
		if (IsValid()) return false;

		return InitSock();
	}

	void CTinyClient::Stop()
	{
		if (!IsValid()) return;

		ITinyNet::Stop();

		// 通知服务端退出
		//Quit();
		QuitEvent();
		Join();
	}

	void CTinyClient::EnableHeart(unsigned int n_nPeriod, unsigned int n_nTimeoutCnt)
	{
		m_nHeartNo = 0;
		m_nHeartPeriod = n_nPeriod;
		m_nHeartTimeoutCnt = n_nTimeoutCnt;
	}

	const unsigned short CTinyClient::RemotePort()
	{
		return GetPort(m_szRemoteAddr);
	}

	const std::string CTinyClient::RemoteIp()
	{
		return GetIp(m_szRemoteAddr);
	}

	bool CTinyClient::InitSock()
	{
		int nResult = 0;

		int nSockType = NetType2SockType(eNetType);

		do
		{
			m_bRun = false;

			fd = socket(AF_INET, nSockType, 0);
			if (fd == SOCKET_ERROR)
			{
				DebugError("create Socket error: %d\n", LastError());
				break;
			}

			KeepAlive(1);
			//SetTimeout(-1);
			ReuseAddr(1);

			nResult = connect(fd, (stSockaddr*)Addr, sizeof(stSockaddr));
			if (nResult == SOCKET_ERROR)
			{
				DebugError("connect Socket error: %d\n", LastError());
				break;
			}

			Join();
			m_thread = std::thread(&CTinyClient::WorkerThread, this);

			m_bRun = true;

		} while (false);

		return m_bRun;
	}

	void CTinyClient::WorkerThread()
	{
		int nResult = 0;

		char* szBuff = (char*)malloc(m_nBuffSize);
		if (!szBuff) return;
		memset(szBuff, 0, m_nBuffSize);

		FNetNode	NetNode;
		NetNode.fd = fd;
		NetNode.eNetType = eNetType;

		SockaddrLen	nLen = sizeof(stSockaddrIn);

		OnEventCallback(this, ENetEvent::Ready, "");
		if (m_nHeartPeriod > 0)
			std::thread(&CTinyClient::HeartThread, this).detach();

		// 通知服务端初始化
		Hello();

		while (true)
		{
			memset(szBuff, 0, m_nBuffSize);

			if (eNetType == ENetType::TCP)
				nResult = recv(fd, szBuff, m_nBuffSize, 0);
			else if (eNetType == ENetType::UDP)
			{
				nResult = recvfrom(fd, szBuff, m_nBuffSize, 0,
					(stSockaddr*)NetNode.Addr, &nLen);
			}

			if (nResult == 0) break;
			if (nResult == SOCKET_ERROR)
			{
				//if (WSAEINTR == nErrorCode) {
				//	// 操作被中断，重新尝试或执行其他操作
				//	continue;
				//}
				if (m_bRun) DebugInfo("socket quit: %d\n", LastError());
				break;
			}

			auto sData = std::string(szBuff, nResult);
			if (!OnEventMessage(this, sData))
			{
				if (eNetType == ENetType::TCP) OnRecvCallback(this, sData);
				else if (eNetType == ENetType::UDP) OnRecvCallback(&NetNode, sData);
			}
		}

		free(szBuff);

		OnEventCallback(this, ENetEvent::Quit, "");
	}

	void CTinyClient::HeartThread()
	{
		int nRet = 0;

		const int nSlice = 500;
		unsigned int nCounter = m_nHeartPeriod;
		unsigned int nHeartNo = 0xFFFFFFFF;
		unsigned int nFail = 0;

		m_nHeartNo = 0;

		while (m_bRun && m_nHeartPeriod > 0)
		{
			if (nCounter < m_nHeartPeriod)
			{
				nCounter += nSlice;
				std::this_thread::sleep_for(std::chrono::milliseconds(nSlice));
				continue;
			}

			// 未接收到返回心跳，默认连接异常，退出
			if (m_nHeartNo == nHeartNo)
			{
				if (nFail >= m_nHeartTimeoutCnt) break;
				else {
					nFail++;
				}
			}
			else nFail = 0;

			nRet = Heart(m_nHeartNo, nFail);
			if (nRet < 0) break;

			nCounter = 0;
			nHeartNo = m_nHeartNo;
		}

		// 无法发送心跳或接收心跳超时，停止
		if (m_nHeartNo == nHeartNo || nRet < 0) Stop();
	}

	void CTinyClient::Join()
	{
		if (m_thread.joinable()) m_thread.join();
	}

	bool CTinyClient::OnEventMessage(FNetNode* n_pNetNode, const std::string& n_sData)
	{
		bool bResult = true;

		auto pEventMsg = (FEventMsg*)n_sData.data();
		switch (pEventMsg->Id)
		{
		case kHelloId:
		{
			auto pData = (char*)(n_sData.data() + sizeof(unsigned int));
			memcpy(m_szRemoteAddr, pData, SOCKADDR_SIZE);

			OnEventCallback(n_pNetNode, ENetEvent::Hello, "");
		}
		break;
		case kHeartId:
		{
			auto pHeart = (FHeart*)pEventMsg;
			OnEventCallback(n_pNetNode, ENetEvent::Heart,
				std::string(n_sData.data() + sizeof(unsigned int) * 2, sizeof(unsigned int) * 2)
			);

			// 服务端发的心跳包需回消息
			if (pHeart->Sender != (unsigned int)fd)
			{
				pHeart->No++;
				n_pNetNode->Send(n_sData);
			}
			else
			{
				m_nHeartNo = pHeart->No;
			}
		}
		break;
		case kQuitId:
			QuitEvent();
			// 工作线程退出后，会触发退出事件，这里无需发送退出事件
			break;
		default:
			bResult = false;
			break;
		}

		return bResult;
	}

	void CTinyClient::QuitEvent()
	{
		m_bRun = false;
		m_nHeartNo = 0;
		m_nHeartPeriod = 0;

		// 关闭Socket
		CloseSocket(fd);
	}

#pragma endregion
}

