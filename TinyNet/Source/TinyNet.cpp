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
	constexpr unsigned int kHeartId = (('N' << 16) | ('I' << 8) | ('X'));

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

	static stSockaddrIn* toSockaddrIn(char* n_szAddr) { return (stSockaddrIn*)n_szAddr; }

	static void BuildSockAddrIn(void* n_Addr, const std::string& n_sHost, const unsigned short n_nPort)
	{
		stSockaddrIn* pSockaddrIn = (stSockaddrIn*)n_Addr;

		toSockaddrIn(Addr)->sin_family = AF_INET;
		toSockaddrIn(Addr)->sin_port = htons(nPort);
		inet_pton(AF_INET, sHost, &toSockaddrIn(Addr)->sin_addr.s_addr);
	}

	////////////////////////////////////////////////////////////////////////////////
	void FNetNode::Init(const ENetType n_eType,
		const std::string& n_sHost, const int n_nPort)
	{
		eNetType = n_eType;

		BuildSockAddrIn(&Addr, n_sHost, n_nPort);
	}

	void FNetNode::Init(const ENetType n_eType, const stSockaddrIn* n_Addr)
	{
		if (!n_Addr) return;

		eNetType = n_eType;
		memcpy(Addr, n_Addr, sizeof(stSockaddrIn));
	}

	const unsigned short Port()
	{
		return ntohs(toSockaddrIn(Addr)->sin_port);
	}

	const std::string Ip()
	{
		char sHost[IPADDR_SIZE] = { 0 };

		inet_ntop(AF_INET,
			&toSockaddrIn(Addr)->sin_addr.s_addr,
			sHost,
			IPADDR_SIZE);

		return std::string(sHost);
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
		return Send(n_sData.c_str(), n_sData.size());
	}

	int FNetNode::Send(const char* n_szData, const int n_nSize,
		const std::string& n_sHost, const int n_nPort) const
	{
		if (!IsValid() || eNetType == ENetType::TCP) return 0;

		stSockaddrIn OtherAddr = { 0 };
		BuildSockAddrIn(&OtherAddr, n_sHost, n_nPort);

		return sendto(fd, n_szData, n_nSize, 0,
			(stSockaddr*)&OtherAddr, sizeof(stSockaddr));
	}

	int FNetNode::Send(const std::string& n_sData, 
		const std::string& n_sHost, const int n_nPort) const
	{
		return Send(n_sData.c_str(), n_sData.size(), n_sHost, n_nPort);
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

	////////////////////////////////////////////////////////////////////////////////
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

	////////////////////////////////////////////////////////////////////////////////
	CMulticast::CMulticast()
	{
	}

	CMulticast::~CMulticast()
	{
		Release();
	}

	bool CMulticast::Init(const std::string& n_sHost, const unsigned short n_nPort);
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
			BuildSockAddrIn(&Addr, m_sLocalIp, n_nPort);

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

	////////////////////////////////////////////////////////////////////////////////
#pragma region base

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

	void ITinyNet::EnableHeart(unsigned int n_nPeriod)
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

#pragma endregion

	////////////////////////////////////////////////////////////////////////////////
#pragma region server
#if defined(_WIN32) || defined(_WIN64)
	struct FNetHandle
	{
		WSAOVERLAPPED	Overlapped;
		WSABUF			DataBuf;
		FNetNode		NetNode;
		char*			Buffer = nullptr;
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

		if (fnEventCallback) fnEventCallback(this, ENetEvent::Quit, "");
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

		if (fnEventCallback) fnEventCallback(this, ENetEvent::Quit, "");
	}
#endif

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
				auto UdpHandle = AllocSocketNode(fd, nullptr);
				PostRecv(UdpHandle);
			}

			if (fnEventCallback) fnEventCallback(this, ENetEvent::Ready, ToString());

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

			auto TcpHandle = (FNetHandle*)AllocSocketNode(ClientSocket, &ClientAddr);

			if (fnEventCallback)
			{
				std::string str = TcpHandle->NetNode.ToString();
				fnEventCallback(&TcpHandle->NetNode, ENetEvent::Accept, str);
			}

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

			if (!bResult && nRecvBytes == 0)
			{
				DebugInfo("[INFO] SOCKET(%zd) 退出\n", pSocketInfo->NetNode.fd);
				FreeSocketNode((void**)&pSocketInfo);
				continue;
			}

			pSocketInfo->DataBuf.len = nRecvBytes;

			if (nRecvBytes == 0)
			{
				FreeSocketNode((void**)&pSocketInfo);
				continue;
			}
			else
			{
				if (*(unsigned int*)pSocketInfo->Buffer == kHeartId)
				{
					// 心跳包
					auto p = (unsigned int*)(pSocketInfo->Buffer + sizeof(unsigned int));
					if (fnEventCallback)
					{
						fnEventCallback(&pSocketInfo->NetNode,
							ENetEvent::Heart, 
							pSocketInfo->NetNode.ToString() + std::to_string(*p);
						);
					}

					if (*p == 0xFFFFFFFF) *p = 0;
					else (*p)++;

					pSocketInfo->NetNode.Send(pSocketInfo->Buffer, nRecvBytes);
				}
				else
				{
					if (fnRecvCallback)
					{
						fnRecvCallback(
							&pSocketInfo->NetNode,
							std::string(pSocketInfo->Buffer, nRecvBytes));
					}
				}

				memset(&(pSocketInfo->Overlapped), 0, sizeof(OVERLAPPED));
				pSocketInfo->DataBuf.len = m_nBuffSize;
				pSocketInfo->DataBuf.buf = pSocketInfo->Buffer;
				memset(pSocketInfo->Buffer, 0, m_nBuffSize);

				nResult = PostRecv(pSocketInfo);

				if (nResult == SOCKET_ERROR && LastError() != WSA_IO_PENDING)
				{
					DebugError("WSARecv 错误 : %d", LastError());
				}
			}
		}
	}

	int CTinyServer::PostRecv(void* n_Handle)
	{
		int		nResult = 0;
		DWORD	dwFlags = 0;
		DWORD	nRecvBytes = 0;
		auto	Handle = (FNetHandle*)(n_Handle);

		if (eNetType == ENetType::TCP)
		{
			nResult = WSARecv(
				Handle->NetNode.fd,
				&Handle->DataBuf,
				1,
				(LPDWORD)&nRecvBytes,
				&dwFlags,
				&(Handle->Overlapped),
				nullptr
			);
		}
		else if (eNetType == ENetType::UDP)
		{
			SockaddrLen nLen = sizeof(stSockaddrIn);
			nResult = WSARecvFrom(
				Handle->NetNode.fd,
				&Handle->DataBuf,
				1,
				(LPDWORD)&nRecvBytes,
				&dwFlags,
				(stSockaddr*)Handle->NetNode.Addr,
				&nLen,
				&(Handle->Overlapped),
				nullptr
			);
		}

		return nResult;
	}

	void* CTinyServer::AllocSocketNode(size_t n_fd, const stSockaddrIn* n_Addr)
	{
		auto Handle = (FNetHandle*)malloc(sizeof(FNetHandle));
		if (!Handle) return nullptr;

		Handle->NetNode.fd = n_fd;
		Handle->NetNode.Init(eNetType, n_Addr);
		memset(&(Handle->Overlapped), 0, sizeof(OVERLAPPED));

		// 分配接收数据缓存
		Handle->Buffer = (char*)malloc(m_nBuffSize);
		if (Handle->Buffer) memset(Handle->Buffer, 0, m_nBuffSize);
		Handle->DataBuf.len = m_nBuffSize;
		Handle->DataBuf.buf = Handle->Buffer;

		m_mNodes.insert({ &Handle->NetNode, Handle });

		return Handle;
	}

	void CTinyServer::FreeSocketNode(void** n_Handle)
	{
		if (!n_Handle || !*n_Handle) return;

		auto Handle = (FNetHandle*)(*n_Handle);
		auto it = m_mNodes.find(&Handle->NetNode);

		if (it != m_mNodes.end())
		{
			if (fnEventCallback && eNetType == ENetType::TCP)
			{
				// 抛出退出事件
				std::string str = Handle->NetNode.ToString();
				fnEventCallback(&Handle->NetNode, ENetEvent::Quit, str);
			}

			m_mNodes.erase(it);
			CloseSocket(Handle->NetNode.fd);
			free(Handle->Buffer);
			free(Handle);
			*n_Handle = nullptr;
		}
	}

	void CTinyServer::FreeSocketNodes()
	{
		auto it = m_mNodes.begin();
		while (it != m_mNodes.end())
		{
			auto Handle = (FNetHandle*)(it->second);
			m_mNodes.erase(it++);

			CloseSocket(Handle->NetNode.fd);
			free(Handle->Buffer);
			free(Handle);
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
			nResult = AddSocketIntoPoll(fd);
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

			if (fnEventCallback) fnEventCallback(this, ENetEvent::Ready, ToString());

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
				if (Event[i].data.fd == fd && eNetType == ENetType::TCP)
				{
					int nFd = accept(fd, (stSockaddr*)&RemoteAddr, &nLen);
					if (nFd == -1)
					{
						DebugError("accept error: %d\n", LastError());
						continue;
					}

					// 把这个新的客户端添加到内核事件列表
					AddSocketIntoPoll(nFd);

					FNetNode RemoteNetNode;
					RemoteNetNode.fd = nFd;
					RemoteNetNode.Init(ENetType::TCP, &RemoteAddr);
					auto pair = m_mNodes.insert({ nFd, RemoteNetNode });

					if (fnEventCallback)
					{
						std::string str = RemoteNetNode.ToString();
						fnEventCallback(&pair.first->second, ENetEvent::Accept, str);
					}
				}
				else
				{
					// 客户端唤醒, 处理用户发来的消息
					memset(szBuff, 0, m_nBuffSize);

					if (eNetType == ENetType::TCP)
						nResult = recv(Event[i].data.fd, szBuff, m_nBuffSize, 0);
					else if (eNetType == ENetType::UDP)
					{
						nResult = recvfrom(Event[i].data.fd, szBuff, m_nBuffSize, 0,
							(stSockaddr*)NetNode.Addr, &nLen);
						NetNode.ParseAddr();
					}

					if (nResult <= 0)
					{
						FreeSocketNode(Event[i].data.fd);
						if (m_bRun) DebugInfo("socket quit: %d\n", LastError());
						continue;
					}

					if (*(unsigned int*)szBuff == kHeartId)
					{
						// 心跳包
						auto p = (unsigned int*)(szBuff + sizeof(unsigned int));
						if (fnEventCallback) 
						{
							if (eNetType == ENetType::TCP)
							{
								auto it = m_mNodes.find(Event[i].data.fd);
								if (it != m_mNodes.end())
								{
									fnEventCallback(&it->second, 
										ENetEvent::Heart, 
										it->second.ToString() + std::to_string(*p));
								}
							}
							else if (eNetType == ENetType::UDP)
							{
								fnEventCallback(&NetNode, 
									ENetEvent::Heart, 
									NetNode.ToString() + std::to_string(*p));
							}
						}

						if (eNetType == ENetType::TCP)
							send(Event[i].data.fd, szBuff, nResult, 0);
						else if (eNetType == ENetType::UDP)
							NetNode.Send(szBuff, nResult);
					}
					else if (fnRecvCallback)
					{
						if (eNetType == ENetType::TCP)
						{
							auto it = m_mNodes.find(Event[i].data.fd);
							if (it != m_mNodes.end())
								fnRecvCallback(&it->second, std::string(szBuff, nResult));
						}
						else if (eNetType == ENetType::UDP)
							fnRecvCallback(&NetNode, std::string(szBuff, nResult));
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

	int CTinyServer::AddSocketIntoPoll(int n_nFd)
	{
		if (m_nEpfd == 0) return -1;

		struct epoll_event ev;
		ev.data.fd = n_nFd;
		ev.events = EPOLLIN;
		if (m_bEt) ev.events = EPOLLIN | EPOLLET;

		int nRet = epoll_ctl(m_nEpfd, EPOLL_CTL_ADD, n_nFd, &ev);
		if (nRet == -1) return nRet;

		return SetNonblock(n_nFd);
	}

	int CTinyServer::DelSocketFromPoll(int n_nFd)
	{
		if (m_nEpfd == 0) return -1;
		return epoll_ctl(m_nEpfd, EPOLL_CTL_DEL, n_nFd, NULL);
	}

	void CTinyServer::FreeSocketNode(int n_nFd)
	{
		auto it = m_mNodes.find(n_nFd);
		if (it != m_mNodes.end())
		{
			DelSocketFromPoll(it->first);
			CloseSocket(it->second.fd);
			m_mNodes.erase(it);
		}
	}

	void CTinyServer::FreeSocketNodes()
	{
		auto it = m_mNodes.begin();
		while (it != m_mNodes.end())
		{
			DelSocketFromPoll(it->first);
			CloseSocket(it->second.fd);
			it++;
		}

		m_mNodes.clear();
	}
#endif

#pragma endregion


////////////////////////////////////////////////////////////////////////////////
#pragma region client
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
		m_nHeartNo = 0;
		m_nHeartPeriod = 0;
		m_bRun = false;

		ITinyNet::Stop();

		// 关闭Socket
		CloseSocket(fd);

		Join();
	}

	void CTinyClient::EnableHeart(unsigned int n_nPeriod)
	{
		m_nHeartNo = 0;
		m_nHeartPeriod = n_nPeriod;
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

		if (fnEventCallback) fnEventCallback(this, ENetEvent::Ready, ToString());
		if (m_nHeartPeriod > 0)
			std::thread(&CTinyClient::HeartThread, this).detach();

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

 			if (*(unsigned int*)szBuff == kHeartId)
			{
				// 心跳包
				if (eNetType == ENetType::TCP || *this == NetNode)
					m_nHeart = 1;
			}
			else if (fnRecvCallback)
			{
				if (eNetType == ENetType::TCP)
					fnRecvCallback(this, std::string(szBuff, nResult));
				else if (eNetType == ENetType::UDP)
					fnRecvCallback(&NetNode, std::string(szBuff, nResult));
			}
		}

		free(szBuff);

		if (fnEventCallback) fnEventCallback(ENetEvent::Quit, ToString());
	}

	void CTinyClient::HeartThread()
	{
		int nRet = 0;

		const int nSlice = 500;
		unsigned int nCounter = m_nHeartPeriod;
		unsigned int nHeartNo = 0xFFFFFFFF;

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
			if (m_nHeartNo == nHeartNo) break;

			nRet = SendHeart();
			if (nRet < 0) break;

			nCounter = 0;
			nHeartNo = m_nHeartNo;

			if (fnEventCallback)
			{
				fnEventCallback(this, 
					ENetEvent::Heart, 
					ToString() + std::to_string(nHeartNo)
				);
			}
		}

		// 无法发送心跳或接收心跳超时，停止
		if (m_nHeartNo == nHeartNo || nRet < 0) Stop();
	}

	void CTinyClient::Join()
	{
		if (m_thread.joinable()) m_thread.join();
	}

	int CTinyClient::SendHeart()
	{
		return Send((const char*)&kHeartId, sizeof(unsigned int));
	}

#pragma endregion
}

