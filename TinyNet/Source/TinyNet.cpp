#include <vector>
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
	// TCP 数据最大可缓存长度
	constexpr int kMaxTCPBufferSize = 1024 * 1024 * 64;
	constexpr unsigned int kHelloId = (('0' << 24) | ('L' << 16) | ('E' << 8) | ('H'));
	constexpr unsigned int kHeartId = (('R' << 24) | ('A' << 16) | ('E' << 8) | ('H'));
	constexpr unsigned int kQuitId = (('T' << 24) | ('I' << 16) | ('U' << 8) | ('Q'));

	const std::string kNetTypeString[] =
	{
		"None",
		"TCP",
		"UDP",
		"MULTICAST",
	};

	inline const unsigned int GetCpuNum()
	{
		return std::thread::hardware_concurrency();
	}

	static int NetType2SockType(const ENetType n_eType)
	{
		int nType = 0;

		if (n_eType == ENetType::TCP)
			nType = SOCK_STREAM;
		else if (n_eType == ENetType::UDP)
			nType = SOCK_DGRAM;

		return nType;
	}

	inline static stSockaddrIn* toSockaddrIn(void* n_szAddr) { return (stSockaddrIn*)n_szAddr; }

	static void BuildSockAddrIn(void* n_Addr, const std::string& n_sHost, const unsigned short n_nPort)
	{
		stSockaddrIn* pSockaddrIn = (stSockaddrIn*)n_Addr;

		toSockaddrIn(n_Addr)->sin_family = AF_INET;
		toSockaddrIn(n_Addr)->sin_port = htons(n_nPort);
		inet_pton(AF_INET, n_sHost.data(), &toSockaddrIn(n_Addr)->sin_addr.s_addr);
	}

#if defined(_WIN32) || defined(_WIN64)
	const std::string GetLocalIPAddress()
	{
		std::string sResult;
		int nResult = 0;
		char ipstr[32] = { 0 };

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
				inet_ntop(AF_INET, &(ipv4->sin_addr), ipstr, sizeof(ipstr));
				sResult = ipstr;
				break;
			}
		}

		freeaddrinfo(res);

		return sResult;
	}
#else
	const std::string GetLocalIPAddress()
	{
		std::string sResult;
		int nResult = 0;
		char ipstr[32] = { 0 };
		
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
				inet_ntop(AF_INET, &(ipAddr->sin_addr), ipstr, sizeof(ipstr));
				sResult = ipstr;
				break;
			}
		}

		freeifaddrs(ifaddr);

		return sResult;
	}
#endif

	int LastError()
	{
#if defined(_WIN32) || defined(_WIN64)
		return WSAGetLastError();
#else
		return errno;
#endif
	}

	unsigned short SockaddrToPort(char* n_szAddr)
	{
		if (!n_szAddr) return 0;
		return ntohs(toSockaddrIn(n_szAddr)->sin_port);
	}

	std::string SockaddrToIp(char* n_szAddr)
	{
		if (!n_szAddr) return "";
		char sHost[IPADDR_SIZE] = { 0 };

		inet_ntop(AF_INET,
			&toSockaddrIn(n_szAddr)->sin_addr.s_addr,
			sHost,
			IPADDR_SIZE);

		return std::string(sHost);
	}

	unsigned long SockaddrToLongIp(char* n_szAddr)
	{
		if (!n_szAddr) return 0;
		return toSockaddrIn(n_szAddr)->sin_addr.s_addr;
	}

	unsigned long long SockaddrToInteger(char* n_szAddr)
	{
		if (!n_szAddr) return 0;
		unsigned long long nResult = SockaddrToLongIp(n_szAddr);
		return (nResult << 16) | SockaddrToPort(n_szAddr);
	}

	void CloseSocket(size_t& n_nSocket)
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
	// 设置广播
	static int SetSocketBroadcast(const size_t n_nFd, bool n_bEnable = true)
	{
		auto ret = setsockopt(n_nFd, 
			SOL_SOCKET, SO_BROADCAST, 
			(ValType)&n_bEnable, sizeof(bool));
		
		if (ret == -1) 
			DebugError("setsockopt IP_TTL error: %d\n", LastError());
		return ret;
	}

	// 设置Time-To-Live
	static int SetSocketTTL(const size_t n_nFd, int n_nOpt, int n_nTTL)
	{
		if (n_nTTL < 0 || n_nTTL > 255) return -1;
		auto ret = setsockopt(n_nFd, 
			IPPROTO_IP, n_nOpt, 
			(ValType)&n_nTTL, sizeof(int));

		if (ret == -1)
			DebugError("setsockopt IP_TTL error: %d\n", LastError());
		return ret;
	}

	// 设置加入组播
	static int SetSocketAddMemberShip(const size_t n_nFd, ValType n_IpMreq)
	{
		auto ret = setsockopt(n_nFd,
			IPPROTO_IP, IP_ADD_MEMBERSHIP,
			n_IpMreq, sizeof(stIpMreq));

		if (ret == -1)
			DebugError("setsockopt IP_ADD_MEMBERSHIP error: %d\n", LastError());
		return ret;
	}

	// 设置移除组播
	static int SetSocketDropMemberShip(const size_t n_nFd, ValType n_IpMreq)
	{
		auto ret = setsockopt(n_nFd,
			IPPROTO_IP, IP_DROP_MEMBERSHIP,
			n_IpMreq, sizeof(stIpMreq));

		if (ret == -1)
			DebugError("setsockopt IP_DROP_MEMBERSHIP error: %d\n", LastError());
		return ret;
	}

	// 设置KeepAlive
	static int SetSocketKeepAlive(const size_t n_nFd, int n_nKeeyAlive = 1) 
	{
		//int keepAlive = 1;
		//int keepIdle = 60; // 空闲时间
		//int keepInterval = 5; // 探测间隔
		//int keepCount = 3; // 探测次数

		auto ret = setsockopt(n_nFd,
			SOL_SOCKET, SO_KEEPALIVE,
			(ValType)&n_nKeeyAlive, sizeof(int));

		if (ret == -1)
			DebugError("setsockopt KeepAlive error: %d\n", LastError());
		// ... 设置 TCP_KEEPIDLE, TCP_KEEPINTVL 和 TCP_KEEPCNT
		return ret;
	}

	// 设置Socket重用
	static int SetSocketReuseAddr(const size_t n_nFd, int n_nReuse = 1)
	{
		auto ret = setsockopt(n_nFd,
			SOL_SOCKET, SO_REUSEADDR,
			(ValType)&n_nReuse, sizeof(int));

		if (ret == -1)
			DebugError("setsockopt SO_REUSEADDR error: %d\n", LastError());
		return ret;
	}

	static int SetSocketTimeout(const size_t n_nFd, int n_nType, const int n_nMilliSeconds)
	{
		if (n_nMilliSeconds < 0) return -1;

#if defined(_WIN32) || defined(_WIN64)
		int tv = n_nMilliSeconds;
#else
		struct timeval tv;

		// Set timeout for sending and receiving
		tv.tv_sec = n_nMilliSeconds / 1000;	// Timeout in seconds
		tv.tv_usec = 0;						// Timeout in microseconds
#endif
		// 设置发送超时
		auto ret = setsockopt(n_nFd,
			SOL_SOCKET,n_nType,
			(ValType)&tv, sizeof(tv));

		if (ret == -1)
		{
			if (n_nType == SO_SNDTIMEO)
				DebugError("setsockopt SO_SNDTIMEO error: %d\n", LastError());
			else
				DebugError("setsockopt SO_RCVTIMEO error: %d\n", LastError());
		}
		return ret;
	}

	// 设置发送超时
	int SetSocketSendTimeout(const size_t n_nFd, const int n_nMilliSeconds)
	{
		return SetSocketTimeout(n_nFd, SO_SNDTIMEO, n_nMilliSeconds);
	}

	// 设置接收超时
	int SetSocketRecvTimeout(const size_t n_nFd, const int n_nMilliSeconds)
	{
		return SetSocketTimeout(n_nFd, SO_RCVTIMEO, n_nMilliSeconds);
	}

	////////////////////////////////////////////////////////////////////////////////
#pragma region 数据缓存
	struct FHeader
	{
		// 数据包总长度(含数据头)
		unsigned int nLength = 0;
		// 事件Id
		unsigned int nEventId = 0;
	};

	FNetBuffer::FNetBuffer(const FNetBuffer& other)
	{
		*this = other;
	}

	FNetBuffer::FNetBuffer(FNetBuffer&& other) noexcept
	{
		*this = std::move(other);
	}

	FNetBuffer::FNetBuffer(const char* n_szData, const size_t n_nSize)
	{
		SetData(n_szData, n_nSize);
	}

	FNetBuffer::FNetBuffer(const std::string& n_sData)
	{
		SetData(n_sData);
	}

	FNetBuffer::~FNetBuffer()
	{
		Free();
	};

	void FNetBuffer::Alloc(const size_t n_nSize)
	{
		auto nSize = n_nSize + sizeof(FHeader);
		if (nSize > nCapacity || !Buffer)
		{
			Free();

			Buffer = (char*)malloc(nSize);
			if (Buffer)
			{
				nLength = nSize;
				nCapacity = nLength;
			}
		}
		else nLength = nSize;

		if (Buffer)
		{
			auto Header = (FHeader*)Buffer;
			// 将长度转换为网络字节序（大端）
			Header->nLength = (unsigned int)htonl((unsigned long)nLength);
			Header->nEventId = 0;
		}

		Zero();
	};

	void FNetBuffer::Free()
	{
		if (nCapacity > 0 && Buffer) free(Buffer);
		
		Buffer = nullptr;
		nLength = 0;
		nCapacity = 0;
	};

	void FNetBuffer::Zero()
	{
		if (!Buffer) return;

		auto nSize = DataCapacity();
		if (nSize > 0) memset((char*)GetData(), 0, nSize);
	}

	const size_t FNetBuffer::DataSize() const
	{
		if (nLength < sizeof(FHeader)) return 0;
		return nLength - sizeof(FHeader);
	}

	const size_t FNetBuffer::DataCapacity() const
	{
		if (nCapacity < sizeof(FHeader)) return 0;
		return nCapacity - sizeof(FHeader);
	}

	void FNetBuffer::SetEventId(unsigned int n_nEventId)
	{
		if (!Buffer) return;
		auto Header = (FHeader*)Buffer;
		Header->nEventId = n_nEventId;
	}

	const unsigned int FNetBuffer::GetEventId() const
	{
		if (!Buffer) return 0;
		auto Header = (FHeader*)Buffer;
		return Header->nEventId;
	}

	const unsigned int FNetBuffer::GetDataPacketSize()
	{
		if (!Buffer) return 0;
		auto Header = (FHeader*)Buffer;
		return ntohl(Header->nLength);
	}

	void FNetBuffer::SetData(const char* n_szData, const size_t n_nSize)
	{
		if (!n_szData || n_nSize == 0) return;

		Alloc(n_nSize);
		memcpy((char*)GetData(), n_szData, n_nSize);
	}

	void FNetBuffer::SetData(const std::string& n_sData)
	{
		SetData(n_sData.data(), n_sData.size());
	}

	const char* FNetBuffer::GetData()
	{
		if (!Buffer) return nullptr;
		return Buffer + sizeof(FHeader);
	}

	bool FNetBuffer::PointTo(const char* n_szData, const size_t n_nSize)
	{
		if (!n_szData || n_nSize < sizeof(FHeader)) return false;

		Free();
		Buffer = (char*)n_szData;
		nLength = n_nSize;

		return true;
	}

	bool FNetBuffer::PointTo(const std::string& n_sData)
	{
		return PointTo(n_sData.data(), n_sData.size());
	}

	FNetBuffer& FNetBuffer::operator=(const FNetBuffer& other)
	{
		if (!other.Buffer || !other.nLength) return *this;

		Alloc(other.DataSize());
		memcpy(Buffer, other.Buffer, other.nLength);

		return *this;
	}

	FNetBuffer& FNetBuffer::operator=(const std::string& n_sData)
	{
		SetData(n_sData);
		return *this;
	}
#pragma endregion
	////////////////////////////////////////////////////////////////////////////////
#pragma region 事件消息
	struct FHeart
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
		memset(Addr, 0, sizeof(stSockaddrIn));
		BuildSockAddrIn(&Addr, n_sHost, n_nPort);
	}

	void FNetNode::Init(const ENetType n_eType, const stSockaddrIn* n_Addr)
	{
		eNetType = n_eType;
		if (n_Addr) memcpy(Addr, n_Addr, sizeof(stSockaddrIn));
		else memset(Addr, 0, sizeof(stSockaddrIn));
	}

	const unsigned short FNetNode::Port()
	{
		return SockaddrToPort(Addr);
	}

	const std::string FNetNode::Ip()
	{
		return SockaddrToIp(Addr);
	}

	int FNetNode::Send(const char* n_szData, const int n_nSize) const
	{
		int nResult = 0;

		if (!IsValid() || n_nSize == 0) return nResult;

		if (eNetType == ENetType::TCP)
		{
			FNetBuffer NetBuffer(n_szData, n_nSize);			 
			nResult = send(fd, NetBuffer.Buffer, (int)NetBuffer.nLength, 0);
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

	int FNetNode::Send(const FNetBuffer& n_Buffer) const
	{
		int nResult = 0;

		if (!IsValid() || !n_Buffer.Buffer || !n_Buffer.nLength)
			return nResult;

		if (eNetType == ENetType::TCP)
		{
			nResult = send(fd, n_Buffer.Buffer, (int)n_Buffer.nLength, 0);
		}
		else if (eNetType == ENetType::UDP)
		{
			nResult = sendto(fd, n_Buffer.Buffer, (int)n_Buffer.nLength, 0,
				(stSockaddr*)Addr, sizeof(stSockaddr));
		}

		return nResult;
	}

	int FNetNode::Send(const char* n_szData, const int n_nSize,
		const std::string& n_sHost, const unsigned short n_nPort) const
	{
		if (!IsValid() || n_nSize == 0 || eNetType == ENetType::TCP) return 0;

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

	int FNetNode::Send(const FNetBuffer& n_Buffer,
		const std::string& n_sHost, const unsigned short n_nPort) const
	{
		if (!n_Buffer.Buffer || !n_Buffer.nLength) return 0;
		return Send(n_Buffer.Buffer, (int)n_Buffer.nLength, n_sHost, n_nPort);
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
#else
		int nLen = sprintf(szBuff, "%s %s:%d ",
#endif
			kNetTypeString[(int)eNetType].data(),
			Ip().data(),
			Port()
		);

		return std::string(szBuff, nLen);
	}

	bool FNetNode::operator==(const FNetNode& n_NetNode)
	{
		return memcmp(Addr, n_NetNode.Addr, 8) == 0;
	}

	void FNetNode::Clear()
	{
		fd = 0;
		memset(Addr, 0, sizeof(stSockaddrIn));
	}

	int FNetNode::Hello()
	{
		FNetBuffer NetBuffer;
		NetBuffer.Alloc(0);
		NetBuffer.SetEventId(kHelloId);

		return Send(NetBuffer);
	}

	int FNetNode::Heart(unsigned int n_nNo, unsigned int n_nFailCnt)
	{
		FHeart Heart = { 0 };
		Heart.Sender = (unsigned int)fd;
		Heart.No = htonl(n_nNo);
		Heart.Cnt = htonl(n_nFailCnt);

		FNetBuffer NetBuffer;
		NetBuffer.SetData((const char*)&Heart, sizeof(FHeart));
		NetBuffer.SetEventId(kHeartId);

		return Send(NetBuffer);
	}

	int FNetNode::Quit()
	{
		if (eNetType != ENetType::UDP) return 0;
		// TCP 不用通知
		FNetBuffer NetBuffer;
		NetBuffer.Alloc(0);
		NetBuffer.SetEventId(kQuitId);

		return Send(NetBuffer);
	}
#pragma endregion

	////////////////////////////////////////////////////////////////////////////////
#pragma region 网络接口
#if defined(_WIN32) || defined(_WIN64)
	int ITinyImpl::m_nRef = 0;
#endif

	ITinyImpl::ITinyImpl()
	{
		Startup();
	}

	ITinyImpl::~ITinyImpl()
	{
		Cleanup();
	}

	void ITinyImpl::SetRecvBuffSize(const int n_nSize)
	{
		m_nBuffSize = n_nSize;
	}

	void ITinyImpl::SetTinyCallback(ITinyCallback* n_TinyCallback)
	{
		m_TinyCallback = n_TinyCallback;
	}

	void ITinyImpl::Startup()
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

	void ITinyImpl::Cleanup()
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

	int CMulticast::SetTTL(unsigned char n_nTTL)
	{
		if (!m_NetNode.IsValid()) return -1;
		return SetSocketTTL(m_NetNode.fd, IP_MULTICAST_TTL, n_nTTL);
	}

	int CMulticast::SetBroadcast(bool n_bEnable)
	{
		if (!m_NetNode.IsValid()) return -1;
		return SetSocketBroadcast(m_NetNode.fd, n_bEnable);
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

			if (SetSocketAddMemberShip(m_NetNode.fd, (ValType)&IpMreq) < 0)
				break;

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

			SetSocketDropMemberShip(m_NetNode.fd, (ValType)&IpMreq);
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

	void CMulticast::MulticastThread() const
	{
		int nResult = 0;

		char* szBuff = (char*)malloc(m_nBuffSize);
		if (!szBuff) return;

		stSockaddrIn	Addr = { 0 };
		SockaddrLen		nLen = sizeof(stSockaddrIn);

		while (true)
		{
			memset(szBuff, 0, m_nBuffSize);
			nResult = recvfrom(m_NetNode.fd,
				szBuff, m_nBuffSize, 0, (stSockaddr*)&Addr, &nLen);
			if (nResult <= 0) break;

			if (fnRecvCallback) fnRecvCallback(szBuff, nResult);
			if (m_TinyCallback) m_TinyCallback->OnReceiveCallback(nullptr, szBuff, nResult);
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

	void ITinyNet::SetTimeout(int n_nMilliSeconds)
	{
		m_nTimeout = n_nMilliSeconds;
	}

	void ITinyNet::SetTTL(int n_nTTL)
	{
		if (n_nTTL > 255) return;
		m_nTTL = n_nTTL;
	}

	int ITinyNet::KeepAlive(const size_t n_nFd) const
	{
		if (!IsValid()) return -1;
		if (eNetType != ENetType::TCP) return 0;
		return SetSocketKeepAlive(n_nFd, 1);
	}

	bool ITinyNet::OnEventMessage(FNetNode* n_pNetNode, const char* n_szData, const int n_nSize)
	{
		return false;
	}

	void ITinyNet::ReceiveTcpMessage(FNetNode* n_pNetNode, 
		std::string& n_sLast, const char* n_szData, const int n_nSize)
	{
		if (eNetType != ENetType::TCP) return;
		if (n_nSize == 0) return;

		FNetBuffer NetBuffer;
		if (!n_sLast.empty())
		{
			if (!NetBuffer.PointTo(n_sLast)) 
			{
				n_sLast.append(n_szData, 1);
				ReceiveTcpMessage(n_pNetNode, n_sLast, n_szData + 1, n_nSize - 1);
				return;
			}

			// 数据包原长度
			auto nSize = NetBuffer.GetDataPacketSize();
			if (nSize > kMaxTCPBufferSize)
			{
				// 长度头异常，丢弃
				std::string().swap(n_sLast);
				//ReceiveTcpMessage(n_pNetNode, n_sLast, n_szData, n_nSize);
				return;
			}
			else if (nSize > NetBuffer.nLength + n_nSize)
			{
				// 加上新的数据仍不足一个完整数据包
				n_sLast.append(n_szData, n_nSize);
				return;
			}

			// 完整数据包仍需长度
			auto nLack = nSize - NetBuffer.nLength;
			// 补足完整数据包
			n_sLast.append(n_szData, nLack);
			
			if (!OnEventMessage(n_pNetNode, n_sLast.data(), (int)nSize))
			{
				// 更新Buffer长度
				NetBuffer.PointTo(n_sLast);
				OnRecvCallback(n_pNetNode, NetBuffer.GetData(), (int)NetBuffer.DataSize());
			}

			n_sLast.clear();

			// 处理剩余数据
			ReceiveTcpMessage(n_pNetNode, n_sLast, n_szData + nLack, n_nSize - (int)nLack);
		}
		else
		{
			if (!NetBuffer.PointTo(n_szData, n_nSize))
			{
				n_sLast.append(n_szData, n_nSize);
				return;
			}

			// 数据包原长度
			auto nSize = NetBuffer.GetDataPacketSize();
			if (nSize < NetBuffer.nLength)
			{
				// 数据不足一个完整数据包
				n_sLast.append(n_szData, n_nSize);
				return;
			}

			if (!OnEventMessage(n_pNetNode, n_szData, nSize))
			{
				OnRecvCallback(n_pNetNode, NetBuffer.GetData(), (int)NetBuffer.DataSize());
			}

			// 处理剩余数据 此时 nLack <= 0
			ReceiveTcpMessage(n_pNetNode, n_sLast, n_szData + nSize, n_nSize - nSize);
		}
	}

	void ITinyNet::ReceiveUdpMessage(FNetNode* n_pNetNode, const char* n_szData, const int n_nSize)
	{
		if (eNetType != ENetType::UDP) return;
		if (!OnEventMessage(n_pNetNode, n_szData, n_nSize))
		{
			OnRecvCallback(n_pNetNode, n_szData, n_nSize);
		}
	}

	void ITinyNet::OnEventCallback(FNetNode* n_pNetNode,
		const ENetEvent n_eNetEvent, const std::string& n_sData)
	{
		if (fnEventCallback) fnEventCallback(n_pNetNode, n_eNetEvent, n_sData);
		if (m_TinyCallback) m_TinyCallback->OnEventCallback(n_pNetNode, n_eNetEvent, n_sData);
	}

	void ITinyNet::OnRecvCallback(FNetNode* n_pNetNode, const char* n_szData, int n_nSize)
	{
		if (fnRecvCallback) fnRecvCallback(n_pNetNode, n_szData, n_nSize);
		if (m_TinyCallback) m_TinyCallback->OnReceiveCallback(n_pNetNode, n_szData, n_nSize);
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
		int nSockType = NetType2SockType(eNetType);
		if (nSockType == 0) return false;

		do
		{
			m_bRun = false;

			fd = WSASocketW(AF_INET, nSockType, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			if (fd == SOCKET_ERROR)
			{
				DebugError("创建 Socket 失败\n");
				break;
			}

			SetSocketTTL(fd, IP_TTL, (unsigned char)m_nTTL);
			SetSocketSendTimeout(fd, m_nTimeout);
			SetSocketRecvTimeout(fd, m_nTimeout);
			SetSocketReuseAddr(fd, 1);

			auto nResult = bind(fd, (stSockaddr*)Addr, sizeof(stSockaddr));
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

			if (!CreateWorkerThread()) break;

			m_hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
			if (!m_hIocp)
			{
				DebugError("创建完成端口失败\n");
				break;
			}

			if (eNetType == ENetType::TCP)
			{
				std::thread(&CTinyServer::Accept, this).detach();
			}
			else if (eNetType == ENetType::UDP)
			{
				auto UdpHandle = (FIOCPNetNode*)AllocSocketNode(fd, nullptr);
				CreateIoCompletionPort(
					(HANDLE)fd, m_hIocp, (ULONG_PTR)UdpHandle, 0
				);
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
			if (!TcpHandle) continue;

			KeepAlive(ClientSocket);
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

		FIOCPNetNode* 	pCompletionKey = nullptr;
		FNetHandle* 	pSocketInfo = nullptr;

		while (true)
		{
			bResult = GetQueuedCompletionStatus(m_hIocp,
				&nRecvBytes,
				(PULONG_PTR)&pCompletionKey,		// completion key
				(LPOVERLAPPED*)&pSocketInfo,		// overlapped I/O
				INFINITE
			);

			if (!pCompletionKey || !pSocketInfo) break;

			pSocketInfo->DataBuf.len = nRecvBytes;

			if (!bResult || nRecvBytes == 0)
			{
				if (eNetType == ENetType::TCP) FreeSocketNode(&pSocketInfo->NetNode);
				continue;
			}
			else
			{
				ReceiveTcpMessage(pSocketInfo->NetNode, 
					pSocketInfo->NetNode->sCache, pSocketInfo->Buffer, nRecvBytes);
				ReceiveUdpMessage(pSocketInfo->NetNode, pSocketInfo->Buffer, nRecvBytes);

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
		int	nResult = 0;
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
		auto NetNode = new FIOCPNetNode;
		if (!NetNode) return nullptr;

		NetNode->Handle = (FNetHandle*)malloc(sizeof(FNetHandle));
		if (!NetNode->Handle)
		{
			delete NetNode;
			return nullptr;
		}

		NetNode->Handle->NetNode = NetNode;

		NetNode->fd = n_fd;
		NetNode->Init(eNetType, n_Addr);
		NetNode->UserData = nullptr;
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
		delete pNetNode;
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
			delete pNetNode;
		}
	}
#else

	bool CTinyServer::InitSock()
	{
		int nSockType = NetType2SockType(eNetType);
		if (nSockType == 0) return false;

		do
		{
			m_bRun = false;

			fd = socket(AF_INET, nSockType, 0);
			if (fd == -1)
			{
				DebugError("create Socket error: %d\n", LastError());
				break;
			}

			SetSocketTTL(fd, IP_TTL, (unsigned char)m_nTTL);
			SetSocketSendTimeout(fd, m_nTimeout)
			SetSocketRecvTimeout(fd, m_nTimeout);
			SetSocketReuseAddr(fd, 1);

			auto nResult = bind(fd, (stSockaddr*)Addr, sizeof(stSockaddr));
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

		std::string 		sData;

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

					auto RemoteNetNode = new FNetNode;
					if (!RemoteNetNode) continue;
					KeepAlive(nFd);

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
							(stSockaddr*)pNetNode->Addr, &nLen);
					}

					if (nResult <= 0)
					{
						if (eNetType == ENetType::TCP) FreeSocketNode(&pNetNode);
						if (m_bRun) DebugInfo("socket quit: %d\n", LastError());
						continue;
					}

					ReceiveTcpMessage(pNetNode, pNetNode->sCache, szBuff, nResult);
					ReceiveUdpMessage(pNetNode, szBuff, nResult);
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

	void CTinyServer::FreeSocketNode(FNetNode** n_pNetNode)
	{
		if (!n_pNetNode || !*n_pNetNode) return;

		auto pNetNode = *n_pNetNode;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_lstNodes.remove(pNetNode);
		}

		DelSocketFromPoll(pNetNode->fd);
		CloseSocket(pNetNode->fd);
		delete pNetNode;
		*n_pNetNode = nullptr;
	}

	void CTinyServer::FreeSocketNodes()
	{
		std::unique_lock<std::mutex> lock(m_mutex);

		auto it = m_lstNodes.begin();
		while (it != m_lstNodes.end())
		{
			auto pNetNode = *it;
			m_lstNodes.erase(it++);

			DelSocketFromPoll(pNetNode->fd);
			CloseSocket(pNetNode->fd);
			delete pNetNode;
		}
	}
#endif

	bool CTinyServer::OnEventMessage(FNetNode* n_pNetNode, const char* n_szData, const int n_nSize)
	{
		bool bResult = true;

		FNetBuffer NetBuffer;
		if (!NetBuffer.PointTo(n_szData, n_nSize)) return false;
		auto nEventId = NetBuffer.GetEventId();

		switch (nEventId)
		{
		case kHelloId:
		{
			OnEventCallback(n_pNetNode, ENetEvent::Hello,
				std::string(n_pNetNode->Addr, SOCKADDR_SIZE)
			);

			// 返回客户端对应的远端 sockaddr
			NetBuffer.SetData(n_pNetNode->Addr, SOCKADDR_SIZE);
			NetBuffer.SetEventId(kHelloId);
			
			n_pNetNode->Send(NetBuffer);
		}
		break;
		case kHeartId:
		{
			auto pHeart = (FHeart*)NetBuffer.GetData();

			char szBuff[sizeof(unsigned int) * 2] = { 0 };
			auto nNo = ntohl(pHeart->No);
			auto nCnt = ntohl(pHeart->Cnt);
			memcpy(szBuff, &nNo, sizeof(unsigned int));
			memcpy(szBuff + sizeof(unsigned int), &nCnt, sizeof(unsigned int));

			OnEventCallback(n_pNetNode, ENetEvent::Heart,
				// 心跳序号及失败次数
				std::string(szBuff, sizeof(unsigned int) * 2)
			);

			// 客户端发的心跳包需回消息，否则通过事件处理后续逻辑
			if (pHeart->Sender != (unsigned int)fd)
			{
				pHeart->No = htonl(nNo + 1);
				n_pNetNode->Send(NetBuffer);
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
		return SockaddrToPort(m_szRemoteAddr);
	}

	const std::string CTinyClient::RemoteIp()
	{
		return SockaddrToIp(m_szRemoteAddr);
	}

	int CTinyClient::SendHeart()
	{
		return Heart(m_nHeartNo, 0);
	}

	bool CTinyClient::InitSock()
	{
		int nSockType = NetType2SockType(eNetType);
		if (nSockType == 0) return false;

		do
		{
			m_bRun = false;

			fd = socket(AF_INET, nSockType, 0);
			if (fd == SOCKET_ERROR)
			{
				DebugError("create Socket error: %d\n", LastError());
				break;
			}

			SetSocketTTL(fd, (unsigned char)m_nTTL);
			SetSocketSendTimeout(fd, m_nTimeout);
			
			auto nResult = connect(fd, (stSockaddr*)Addr, sizeof(stSockaddr));
			if (nResult == SOCKET_ERROR)
			{
				DebugError("connect Socket error: %d\n", LastError());
				break;
			}

			KeepAlive(fd);

			Join();
			m_thread = std::thread(&CTinyClient::WorkerThread, this);

			m_bRun = true;

		} while (false);

		if (!m_bRun) CloseSocket(fd);

		return m_bRun;
	}

	void CTinyClient::WorkerThread()
	{
		int nResult = 0;

		char* szBuff = (char*)malloc(m_nBuffSize);
		if (!szBuff) return;
		memset(szBuff, 0, m_nBuffSize);

		SockaddrLen	nLen = sizeof(stSockaddrIn);

		// 通知服务端初始化
		Hello();

		OnEventCallback(this, ENetEvent::Ready, "");
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
					(stSockaddr*)Addr, &nLen);
			}

			if (nResult == 0) break;
			if (nResult == SOCKET_ERROR)
			{
				// 操作被中断，重新尝试或执行其他操作
#if defined(_WIN32) || defined(_WIN64)
				if (WSAEINTR == LastError()) continue;
#else
				if (EINTR == LastError()) continue;
#endif
				if (m_bRun) DebugInfo("socket quit: %d\n", LastError());
				break;
			}

			ReceiveTcpMessage(this, sCache, szBuff, nResult);
			ReceiveUdpMessage(this, szBuff, nResult);
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

	bool CTinyClient::OnEventMessage(FNetNode* n_pNetNode, const char* n_szData, const int n_nSize)
	{
		bool bResult = true;

		FNetBuffer NetBuffer;
		if (!NetBuffer.PointTo(n_szData, n_nSize)) return false;
		auto nEventId = NetBuffer.GetEventId();

		switch (nEventId)
		{
		case kHelloId:
		{
			// 获取远端 sockaddr
			memcpy(m_szRemoteAddr, NetBuffer.GetData(), SOCKADDR_SIZE);

			OnEventCallback(n_pNetNode, ENetEvent::Hello, "");
		}
		break;
		case kHeartId:
		{
			auto pHeart = (FHeart*)NetBuffer.GetData();

			char szBuff[sizeof(unsigned int) * 2] = { 0 };
			auto nNo = ntohl(pHeart->No);
			auto nCnt = ntohl(pHeart->Cnt);
			memcpy(szBuff, &nNo, sizeof(unsigned int));
			memcpy(szBuff + sizeof(unsigned int), &nCnt, sizeof(unsigned int));

			OnEventCallback(n_pNetNode, ENetEvent::Heart,
				// 心跳序号及失败次数
				std::string(szBuff, sizeof(unsigned int) * 2)
			);

			// 服务端发的心跳包需回消息
			if (pHeart->Sender != (unsigned int)fd)
			{
				pHeart->No = htonl(nNo + 1);
				n_pNetNode->Send(NetBuffer);
			}
			else
			{
				m_nHeartNo = nNo;
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
		// 通知服务端退出
		Quit();

		m_bRun = false;
		m_nHeartNo = 0;
		m_nHeartPeriod = 0;

		// 关闭Socket
		CloseSocket(fd);
	}

#pragma endregion
}

