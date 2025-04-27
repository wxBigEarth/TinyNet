# TinyNet

轻量级跨平台 C++ Socket，支持TCP, UDP及UDP 广播、组播。

Windows 服务端使用 IOCP 模型，Linux 服务端使用 EPoll 模型。

运行过程中，通过定义 fnEventCallback 回调函数响应事件及消息，捕获消息类型及消息内容；
fnEventCallback 参数定义如下：
FNetNode*: 产生数据的Socket结点
const ENetEvent: 事件类型
const std::string& : 数据内容

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

定义 fnRecvCallback 回调函数接收数据，参数定义如下：
FNetNode*: 产生数据的Socket结点
const char* : 接收的数据
int : 接收的数据的长度

Socket 结点封装在结构体 FNetNode，可通过该对象发送数据。

对TCP，为了解决TCP粘包问题，每次发送消息，内部自动添加4个字节消息头，内容为该消息的长度
若接收到的数据不足消息头记录的长度，则在内部缓存（最大支持64M，超出则丢弃），否则通过 
fnRecvCallback 回调返回给用户

内部事件

客户端初始化成功时，自动给服务端发送 kHelloId 消息，服务端返回远端 sockaddr，触发 ENetEvent::Hello 事件

客户端启用心跳机制时，发送 kHeartId 消息，触发 ENetEvent::Heart 事件

若是UDP通信，客户端退出时，发送 kQuitId 消息，触发 ENetEvent::Quit 事件

CMulticast 封装组播功能，服务端和客户端可用该类定义发送端和接收端；但不可同时创建发送端和接收端。
CMulticast 同样通过定义 fnRecvCallback 回调函数接收数据。
