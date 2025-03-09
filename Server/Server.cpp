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

int main()
{
	CTinyServer rs;

	rs.fnEventCallback = [&](const ENetEvent e, const std::string& s) {

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

	rs.fnRecvCallback = [&](FNetNode* pNode, const std::string& sData) {

		cout << "Recv [" << sData.length() << "]: " << sData.c_str() << endl;

		pNode->Send(sData);
	};

	if (rs.Start(TYPE, HOST, PORT))
	{
		while (getchar() != '\n');
	}

	return 0;
}
