/*
 *  VideoProto.cpp
 *  UDP Video Transfer Protocol with low latency and no ACK
 *
 *  Created by Zhongzhi Yu on July 7 2016.
 *
 * Copyright (c) 2016 Zhongzhi Yu.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, this list of
 *   conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice, this list of
 *   conditions and the following disclaimer in the documentation and/or other materials
 *   provided with the distribution.
 * - Neither the name of the Author nor the names of its contributors may be used to
 *   endorse or promote products derived from this software without specific prior written
 *   permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include "../inc/VideoProto.h"
//#include <memory>

using std::shared_ptr;

// std::atomic<size_t> numClients = 0;

// BOOKMARK NetworkThread

typedef struct 
{
	SOCKET listenSock;
	NetBufferApi* api;
	sockaddr_in clientAddr;
} ClientInfo;

//#include <stdlib.h>    
#include <time.h>       
//#include <iostream>
// #include <condition_variable> "Real blocking" requires system call and could increase latency

// DEFINE constant values for configurations
const uint64_t ACK_TIMEOUT_TIMES = 3;

namespace serveClient_private
{
class A { public: static const char* name; }; const char* A::name = "[BUFS] Begin";
class B { public: static const char* name; }; const char* B::name = "[BUFS] Loop begin";
class C { public: static const char* name; }; const char* C::name = "[BUFS] Loop end";
class D { public: static const char* name; }; const char* D::name = "[BUFS] End";


class AF { public: static const char* name; }; const char* AF::name = "[BUFS] ASDF";
class BE { public: static const char* name; }; const char* BE::name = "[BUFS] BEFORE BEFORE INNER Loop begin";
class E { public: static const char* name; }; const char* E::name = "[BUFS] INNER Loop begin";
class F { public: static const char* name; }; const char* F::name = "[BUFS] INNER Loop end";

class G { public: static const char* name; }; const char* G::name = "[SERVE] BEGIN";
class H { public: static const char* name; }; const char* H::name = "[SERVE] BEFORE FETCH";
class I { public: static const char* name; }; const char* I::name = "[SERVE] AFTER  FETCH";
class J { public: static const char* name; }; const char* J::name = "[SERVE] BEFORE SEND";
class K { public: static const char* name; }; const char* K::name = "[SERVE] AFTER  SEND";
class L { public: static const char* name; }; const char* L::name = "[SERVE] END";

class Y { public: static const char* name; }; const char* Y::name = "[ONEBUF] BEGIN";
class Z { public: static const char* name; }; const char* Z::name = "[ONEBUF] ENDS ";
};

#include <TimeAPI.h>
#pragma comment(lib, "Winmm.lib")

void serveClient(shared_ptr<ClientInfo> param)
{
	using std::cout;
	using std::cerr;
	using std::endl;
	ClientInfo& info = *param;
	SOCKET listenSock = info.listenSock;
	NetBufferApi* api = info.api;
	sockaddr_in& clientAddr = info.clientAddr;

	srand(time(NULL));

	cout << "Streaming started." << endl;

	int bytes = 0;

	std::atomic<bool> connOK = true;

	const bool enableLatency = false;
	const uint64_t minLatency   = 25;
	const uint64_t maxLatency   = 25;

	// # of failed ASK_ACK trials before we discard this client
	
	// unit: ms
	const uint64_t TIME_BETWEEN_ASK_ACKS = 2000;

	// ALL UDP PACKETS MUST BE SENT BY THIS FUNCTION ONLY
	// It returns whatever is returned by system call 'sendto'
	auto& sendOneBuf = [&](const NetBufferApi::Buffer& buf) {
//		static TimerPrinter print(false);
//		print.print<serveClient_private::Y>();
		bytes = sendto(listenSock, (const char*)buf.getAllData(), (int)buf.size(), 0, (const sockaddr*)&clientAddr, sizeof(clientAddr));
//		print.print<serveClient_private::Z>();
		return bytes;
	};

	// return false if need to break execution (failed)
	// returns true if successful
	auto& sendBufs = [&](const char* TAG, const std::vector<NetBufferApi::Buffer>& bufs, bool frameBufBlockage) -> bool {
		using namespace serveClient_private;

		if (bufs.empty())
			return true;

		size_t repeat = 1;
		if (bufs.front().getType() == NetBuffer::Type::FRAME_IDR
			|| bufs.front().getType() == NetBuffer::Type::FRAME_NOT_IDR)
			repeat = 2;

		for (size_t r = 0; r < repeat; r++) {

			GoodSleep(7);

			size_t cnt = 1;
			const size_t sleepPeriod = frameBufBlockage ? 4 : 3;

//			static AveragePrinter<size_t> avgPrint(0, "serveClient sendBufs length");
//			avgPrint.print(bufs.size());

			for (auto i = bufs.begin(); i != bufs.end(); i++)
			{
				auto buf = *i;
				// Loop begins here.
				cnt++;

				bytes = sendOneBuf(buf);

				if (bytes < 0 || bytes != buf.size())
				{
					cerr << "error connecting to client in sendto() in " << TAG << ": " << bytes << endl;
					cerr << "                                    should have been " << buf.size() << endl;
					cerr << "                                    WSA Last Error = " << WSAGetLastError() << endl;
					connOK = false;
					return false;
				}
				// Loop ends here.
			}

		}

		return true;
	};

	// this one will start an synchronous, blocking process to ask for ACK.
	// If ACK is not received or network failed, this will set connOK to false.
	// Also it will return false upon such failures.
	auto& askACK = [&]() -> bool {
		const NetBufferApi::ACKDataType verify = rand();
		NetBufferApi::ACKDataType clientVerify = verify + 1;
		NetBuffer&& ackBuf = api->newACK(verify);
		for (uint64_t trials = 0; trials < ACK_TIMEOUT_TIMES; trials++)
		{
			clientVerify = verify + 1;

			if (sendOneBuf(ackBuf) < 0)
				continue;
			SOCKADDR_IN senderAddr;
			int senderAddrSize = sizeof(senderAddr);
			auto nret = recvfrom(listenSock, (char*)&clientVerify, sizeof(clientVerify), 0, (sockaddr *) &senderAddr, &senderAddrSize);

			if (nret != sizeof(clientVerify) || memcmp(&senderAddr, &info.clientAddr, senderAddrSize) != 0)
				continue;
			if (verify != clientVerify)
				continue;

			return true;
		}
		return connOK = false;
	};

	uint64_t lastAskAckTime = GetTickCount64();
//	TimerPrinter print(false);

	while(connOK)
	{
		using namespace serveClient_private;
//		print.print<G>();

		if (!api->sendAudioPacket([&sendOneBuf](NetBufferApi::Buffer& packet) -> bool {
			if (sendOneBuf(packet) < 0)
				return false;
			else return true;
		}))
			break;

		if (GetTickCount64() - lastAskAckTime >= TIME_BETWEEN_ASK_ACKS)
		{
			askACK();
			lastAskAckTime = GetTickCount64();
		}

		else
		{
			if (enableLatency)
			{
				shared_ptr<NetBufferApi::WholeFrame> frameToSend(nullptr);
				bool frameBufBlockage;

				assert(false);  // TODO: impl. latency simulation.

#if 0
				api->withFrames([&](NetBufferApi::Bufmap& map) {
					if (map.empty())
						return;

					auto low = getTime() - maxLatency;
					auto high = getTime() - minLatency;
					auto i = map.begin();
					while (i != map.end())
					{
						if (i->first < low)
						{
							frameToSend = i->second;
							frameBufBlockage = map.size() > 1;
							if (!sendBufs("W/LTNCY", i->second, map.size() > 1))
								break;
							i = map.erase(i);
						}
						else if (i->first < high)
						{
							if (!sendBufs("Latency sim.", i->second, map.size() > 1))
								break;
							map.erase(i);
							break;
						}
						else break;
					}
				});
				if (frameToSend)
				{
					if (!sendBufs("W/LTNCY", *frameToSend.get(), frameBufBlockage))
				}
#endif
			}
			else
			{
//				print.print<H>();
				shared_ptr<NetBufferApi::WholeFrame> frameToSend(nullptr);
				bool frameBufBlockage;

				api->withFrames([&](NetBufferApi::Bufmap& map) {
					if (map.empty())
						return;

					auto i = map.begin();
					if (i != map.end())
					{
						frameToSend = i->second;
						frameBufBlockage = map.size() > 1;
						map.erase(i);
					}
				});
//				print.print<I>();

//				print.print<J>();
				if (frameToSend)
				{
					if (!sendBufs("NORMAL  mode", *frameToSend, frameBufBlockage))
						break;
				}
//				print.print<K>();
			}
		}

//		print.print<L>();
		// GoodSleep(1);
		
	}

}
/*
void networkEntry(void* param)
{
	using std::cout;
	using std::cerr;
	using std::endl;
	NetBufferApi* api = (NetBufferApi*) param;
	int nret;

	WSADATA wsaData;
	WSAStartup(MAKEWORD(1,1), &wsaData);

	SOCKET listenSock = socket(AF_INET,
		SOCK_DGRAM,
		IPPROTO_UDP);

	if (listenSock == INVALID_SOCKET)
	{
		cerr << "error creating UDP server" << endl;
		WSACleanup();
		cerr << "error in socket()";
		cerr << endl;
		return;
	}

	DWORD timeo = 20;  // timeout for both Send and Receive (Ask for ACK needs a timeout)
	int send_buf = 65535; // 3 * 70000; // 3 frames
	setsockopt(listenSock, SOL_SOCKET, SO_RCVTIMEO, (char*) &timeo, sizeof(timeo));
	// setsockopt(listenSock, SOL_SOCKET, SO_SNDTIMEO, (char*) &timeo, sizeof(timeo));
	cout << "SET BUFFER " << setsockopt(listenSock, SOL_SOCKET, SO_SNDBUF, (char*)&send_buf, sizeof(send_buf)) << endl;

	SOCKADDR_IN addrListen;
	addrListen.sin_family = AF_INET;
	addrListen.sin_addr.s_addr = INADDR_ANY;  // Can be changed to _BROADCAST
	addrListen.sin_port = htons(api->getVideoPort());

	nret = bind(listenSock, (sockaddr *) &addrListen, sizeof(addrListen));
	if (nret == SOCKET_ERROR)
	{
		cerr << "error creating UDP server" << endl;
		WSACleanup();
		cerr << "error in bind()";
		cerr << endl;
		return;
	}

	cout << "UDP Listening on port: " << api->getVideoPort() << endl;

	while(true)
	{
		// Find a client.
		char clientGreet[1];
		shared_ptr<ClientInfo> info(new ClientInfo);

		memset(info.get(), 0, sizeof(info));
		info->listenSock = listenSock;
		info->api        = api;
		size_t clientAddrSize = sizeof(info->clientAddr);
		nret = recvfrom(listenSock, clientGreet, 1, 0, (sockaddr *) &info->clientAddr, (int*)&clientAddrSize);
		if (nret != 1)
		{
			continue;
		}

		cout<<"Got one new client."<<endl;

		// try sending video frame 0
		// WARNING: TODO: FIXME: race condition: if client connects before device opens, then there is no frame 0 to send.
		bool first_frame_ok = true;
		{
			int failure_count = 0;
			while (true)
			{
				// WARNING: first frame is sent without any header or "tailer"
				if (!api->packetizeAndSendNoIDTrack(NetBuffer::Type::VIDEO_FRAME_ZERO, api->getVideoFirstFrame(), api->getVideoFirstFrameSize(), [&](NetBuffer& buf) -> bool {
					auto bytes = sendto(listenSock, (const char*)buf.getAllData(), (int)buf.size(), 0, (const sockaddr*)&info->clientAddr, sizeof(info->clientAddr));
					if (bytes < 0 || bytes != buf.size())
						return false;
				}))
				{
					cerr << "error connecting to client in sendto() for FIRST FRAME" << endl;
					continue;
				}

				uint32_t verifySize = api->getVideoFirstFrameSize() - 1;

				SOCKADDR_IN senderAddr;
				int senderAddrSize = sizeof(senderAddr);

				auto nret = recvfrom(listenSock, (char*)&verifySize, sizeof(verifySize), 0, (sockaddr *) &senderAddr, &senderAddrSize);
				verifySize = ntohl(verifySize);

				if (nret != sizeof(verifySize)
					|| memcmp(&senderAddr, &info->clientAddr, senderAddrSize) != 0
					|| verifySize != api->getVideoFirstFrameSize())
				{
					nret = -1;
					failure_count++;
					if (failure_count > ACK_TIMEOUT_TIMES)
					{
						first_frame_ok = false;
						break;
					}
				}
				else break;
			}
		}

		if (first_frame_ok)
		{
			api->setBufferEnabled(true);
			serveClient(info);
			api->setBufferEnabled(false);
		}

		NetBuffer&& discard = api->newBufferWithoutData(NetBuffer::Type::SERVER_DISCARD_CLIENT);
		sendto(listenSock, (const char*)discard.getAllData(), (int)discard.size(), 0, (const sockaddr*)&info->clientAddr, sizeof(info->clientAddr));

		cout<<"Disconnected from client."<<endl;
	}
}
*/
// TODO: maybe later we could support other OS as host
