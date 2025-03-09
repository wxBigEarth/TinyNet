// RTSDataStream.cpp: 定义应用程序的入口点。
//

#include "Client.h"
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

int main()
{
	CTinyClient rc;

	auto ip = GetLocalIPAddress();
	cout << "ip: " << ip.c_str() << endl;

	rc.fnEventCallback = [&](const ENetEvent e, const std::string& s) {

		switch (e)
		{
		case ENetEvent::Ready:
			cout << "Socket Ready" << endl;
			break;
		case ENetEvent::Accept:
			cout << "Accept: " << s.c_str() << endl;
			break;
		case ENetEvent::Quit:
			cout << "Socket Quit: " << s.c_str() << endl;
			break;
		case ENetEvent::Heart:
			cout << "Heart Beat" << endl;
			break;
		}
	};

	rc.fnRecvCallback = [&](FNetNode* pNode, const std::string& sData) {

		cout << "Recv [" << sData.length() << "]: " << sData.c_str() << endl;
	};
	//rc.SetRecvBuffSize(8192);
	if (rc.Start(TYPE, HOST, PORT))
	{
		char szOutMsg[1024] = { 0 };
		rc.EnableHeart(3000);

		int nSendLen = 0;
		while (true)
		{
			std::cout << ">>";
			std::cin >> szOutMsg;
			if (strcmp(szOutMsg, "quit") == 0) break;

			nSendLen = rc.Send(szOutMsg, (int)strlen(szOutMsg));

			if (nSendLen < 0) std::cout << ">> Error: " << LastError() << std::endl;
			memset(szOutMsg, 0, 1024);
		}

		rc.Stop();
	}

	return 0;
}
