# TinyNet

轻量级跨平台 C++ Socket，支持TCP, UDP及UDP 组播。

Windows 服务端使用 IOCP 模型，Linux 服务端使用 EPoll 模型。

运行过程中，通过定义 fnEventCallback 回调函数响应事件及消息，捕获消息类型及消息内容；

消息类型定义如下：
enum class ENetEvent
{
	Ready = 0,	// Socket 就绪
	Accept,		// TCP 服务端接收到客户端
	Heart,		// 客户端发送或服务端接收心跳包
	Quit,		// Socket 退出
};

定义 fnRecvCallback 回调函数接收数据，参数定义如下：
FNetNode*: 产生数据的Socket结点
const std::string& : 数据内容

Socket 结点封装在结构体 FNetNode，可通过该对象发送数据。

CMulticast 封装组播功能，服务端和客户端可用该类定义发送端和接收端；但不可同时创建发送端和接收端。
CMulticast 同样通过定义 fnRecvCallback 回调函数接收数据。