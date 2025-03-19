// RTSDataStream.cpp: 定义应用程序的入口点。
//

#include "Server.h"
#include "TinyNet.h"

#if !defined(_WIN32) && !defined(_WIN64)
#include <string.h>
#endif

using namespace std;
using namespace tinynet;


//#define TYPE ENetType::TCP
#define TYPE ENetType::UDP

#define HOST "127.0.0.1"
#define PORT 8000
#define MULTICAST "239.0.0.1"

// 组播发送端
void MulticastSender()
{
	CMulticast mc;

	mc.Init(MULTICAST, PORT);
	mc.Sender();

	int nSendLen = 0;
	char szOutMsg[1024] = { 0 };

	while (true)
	{
		std::cout << ">>";
		std::cin >> szOutMsg;
		if (strcmp(szOutMsg, "quit") == 0) break;

		nSendLen = mc.Send(szOutMsg, (int)strlen(szOutMsg));

		if (nSendLen < 0) std::cout << ">> Error: " << LastError() << std::endl;
		memset(szOutMsg, 0, 1024);
	}

	mc.Release();
}


int main()
{
	CTinyServer rs;

	rs.fnEventCallback = [&](FNetNode* pNode, const ENetEvent e, const std::string& s) {

		switch (e)
		{
		case ENetEvent::Ready:
			cout << "Socket Ready: " << pNode->ToString() << endl;
			break;
		case ENetEvent::Accept:
			cout << "Accept: " << pNode->ToString() << endl;
			break;
		case ENetEvent::Heart:
		{
			auto p = (unsigned int*)s.data();
			cout << "Heart Beat: " << pNode->ToString() << ": [" << p[0] << ":" << p[1] << "]" << endl;
		}
			break;
		case ENetEvent::Quit:
			cout << "Socket Quit: " << pNode->ToString() << endl;
			break;
		}
	};

	rs.fnRecvCallback = [&](FNetNode* pNode, const std::string& sData) {

		cout << "Recv [" << sData.length() << "]: " << sData << endl;

		pNode->Send(sData);
	};

	if (rs.Start(TYPE, HOST, PORT))
	{
		getchar();
	}

	return 0;
}
