# TinyNet

	轻量级跨平台 C++ Socket，支持TCP, UDP及UDP 广播、组播。

FNetBuffer

	Socket通讯数据Buffer，主要用于TCP防粘包，及内部事件
	Buffer定义数据头（包括数据长度、内部事件ID）

	为了解决TCP粘包问题，每次发送消息，数据头记录该消息的长度，
	若接收到的数据不足消息头记录的长度，则在FNetSocket的sCache缓存，
	最大支持 kMaxTCPBufferSize（64M），超出则丢弃，
	否则将数据拼接成完整长度，回调返回给用户

	消息类型定义如下：
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

	内部事件

	kHelloId：上线事件ID
	kHeartId：心跳事件ID
	kQuitId：退出事件ID

	客户端初始化成功时，自动给服务端发送 kHelloId 消息，服务端返回远端 sockaddr，触发 ENetEvent::Hello 事件
	客户端启用心跳机制时，发送 kHeartId 消息，触发 ENetEvent::Heart 事件
	若是UDP通信，客户端退出时，发送 kQuitId 消息，触发 ENetEvent::Quit 事件

FNetNode

	封装Socket对象，包含IP及端口号基本信息；
	客户端、服务端及服务端接收到的客户端都为该类型；
	支持TCP, UDP发送消息；

ITinyCallback

	回调函数接口，可处理事件回调及Socket消息回调

	事件回调接口 OnEventCallback 参数定义如下：
		FNetNode*: 产生数据的Socket结点
		const ENetEvent: 事件类型
		const std::string& : 数据内容

	数据消息回调接口 OnReceiveCallback 参数定义如下：
		FNetNode*: 产生数据的Socket结点
		const char* : 接收的数据
		int : 接收的数据的长度

CMulticast

	封装组播功能，服务端和客户端可用该类定义发送端和接收端；但不可同时创建发送端和接收端。
	可通过定义 fnRecvCallback 回调函数接收数据；也可设置 ITinyCallback 对象接收数据；
	fnRecvCallback 定义与 ITinyCallback 中数据消息接口一致；
	因无粘包问题，且不产生事件，组播消息不使用FNetBuffer对象

ITinyNet

	定义客户端和服务端基础功能

CTinyServer

	服务端，支持TCP, UDP；需先调用 FNetNode的 Init 方法初始化；
	Windows 服务端使用 IOCP 模型，Linux 服务端使用 EPoll 模型；
	可设置 ITinyCallback 对象接收数据和事件；
	也可设置 fnRecvCallback 和 fnEventCallback 接收数据和事件；
	fnRecvCallback 和 fnEventCallback 定义与 ITinyCallback 中接口一致；

CTinyClient

	客户端，支持TCP, UDP；需先调用 FNetNode的 Init 方法初始化；
	使用单线程接口服务端数据；
	可设置 ITinyCallback 对象接收数据和事件；
	也可设置 fnRecvCallback 和 fnEventCallback 接收数据和事件；
	fnRecvCallback 和 fnEventCallback 定义与 ITinyCallback 中接口一致；

