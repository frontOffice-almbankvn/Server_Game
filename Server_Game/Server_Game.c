#include "winsock2.h"
#include "stdio.h"
#include "WS2tcpip.h"
#include "string.h"

#define SERVER_ADDR "127.0.0.1"
#define PORT 5500
#define DATA_BUFSIZE 8192
#define RECEIVE 0
#define SEND 1

#pragma comment(lib, "Ws2_32.lib")

/*Struct contains information of the socket communicating with client*/
typedef struct SocketInfo {
	SOCKET socket;
	WSAOVERLAPPED overlapped;
	WSABUF dataBuf;
	char buffer[DATA_BUFSIZE];
	int operation;
	int sentBytes;
	int recvBytes;
} SocketInfo;

void freeSockInfo(SocketInfo* siArray[], int n);
void closeEventInArray(WSAEVENT eventArr[], int n);
void checkNumberClient(SocketInfo* siArray[]);
void sentToClient(SocketInfo* siArray[], WSAEVENT eventArr[]);
int Send(SOCKET s, char* buff, int size, int flags) {
	int n;

	n = send(s, buff, size, flags);
	if (n == SOCKET_ERROR)
		printf("Error %d: Cannot send data.\n", WSAGetLastError());

	return n;
}
int main() {
	SocketInfo* socks[WSA_MAXIMUM_WAIT_EVENTS];
	WSAEVENT events[WSA_MAXIMUM_WAIT_EVENTS];
	int nEvents = 0; //Bien dem su kien

	WSADATA wsaData;
	if (WSAStartup((2, 2), &wsaData) != 0) {
		printf("WSAStartup() failed with error %d\n", WSAGetLastError());
		WSACleanup();
		return 1;
	}

	//Khoi tao gia tri mac dinh cho mang sock va event
	for (int i = 0; i < WSA_MAXIMUM_WAIT_EVENTS; i++) {
		socks[i] = 0;
		events[i] = 0;
	}

	//Bat dau mot socket de lang nghe
	SOCKET listenSocket;
	if ((listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET) {
		printf("Error %d: Cannot create server socket.", WSAGetLastError());
		return 1;
	}

	//Gan doi tuong SOCKET lang nghe voi 1 doi tuong event
	events[0] = WSACreateEvent();
	nEvents++;
	WSAEventSelect(listenSocket, events[0], FD_ACCEPT | FD_CLOSE);

	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PORT);
	inet_pton(AF_INET, SERVER_ADDR, &serverAddr.sin_addr);

	if (bind(listenSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr))) {
		printf("Error %d: Cannot associate a local address with server socket.", WSAGetLastError());
		return 0;
	}

	if (listen(listenSocket, 10)) {
		printf("Error %d: Cannot place server socket in state LISTEN.", WSAGetLastError());
		return 0;
	}

	printf("Server started!\n");

	int index, ret;
	SOCKET connSock;
	SOCKADDR_IN clientAddr;
	int clientAddrLen = sizeof(clientAddr);

	while (1) {
		//Cho su kien network tren cac socket
		index = WSAWaitForMultipleEvents(nEvents, events, FALSE, WSA_INFINITE, FALSE);

		if (index == WSA_WAIT_FAILED) {
			printf("Error %d: WSAWaitForMultipleEvents() failed \n", WSAGetLastError());
			return 0;
		}

		index = index - WSA_WAIT_EVENT_0;
		DWORD flags, transferredBytes;

		//If the event trigger =0 => mot no luc ket noi da duoc ghi nhan
		//
		if (index == 0) {
			WSAResetEvent(events[0]);
			//Khi mot socket moi duoc ket noi den, no se tra ve connection socket
			//Van de la socket listen van chi co 1 => vao ra bat dong bo theo su kien thuc chat la gi?
			if ((connSock = accept(listenSocket, (SOCKADDR*)&clientAddr, &clientAddrLen)) == INVALID_SOCKET) {
				printf("Error %d: Cannot permit incoming connection.\n", WSAGetLastError());
				return 0;
			}

			int i;
			//Bat dau kiem tra va tao socket client
			if (nEvents == WSA_MAXIMUM_WAIT_EVENTS) {
				printf("\nToo many clients");
				closesocket;
			}
			else {
				//Ngat socket nay voi tat cac cac event object
				WSAEventSelect(connSock, NULL, 0);

				//them socket nay vao mang SocketInfo
				i = nEvents;
				events[i] = WSACreateEvent();
				socks[i] = (SocketInfo*)malloc(sizeof(SocketInfo));
				socks[i]->socket = connSock;
				memset(&socks[i]->overlapped, 0, sizeof(WSAOVERLAPPED));
				socks[i]->overlapped.hEvent = events[i];
				socks[i]->dataBuf.buf = socks[i]->buffer;
				socks[i]->dataBuf.len = DATA_BUFSIZE;
				socks[i]->operation = RECEIVE;
				socks[i]->recvBytes = 0;
				socks[i]->sentBytes = 0;
				nEvents++;

				//dua yeu cau overlap I/O request de bat dau nhan du lieu tu socket
				flags = 0;
				if (WSARecv(socks[i]->socket, &(socks[i]->dataBuf), 1, &transferredBytes, &flags, &(socks[i]->overlapped), NULL) == SOCKET_ERROR) {
					if (WSAGetLastError() != WSA_IO_PENDING) {
						printf("WSARecv() failed with error %d\n", WSAGetLastError());
						closeEventInArray(events, i);
						freeSockInfo(socks, i);
						nEvents--;
					}
				}
			}
		}
		else { //Neu su kien duoc kich hoat khong phai la 0 thi yeu cau I/O hoan thanh
			SocketInfo* client;
			client = socks[index];
			WSAResetEvent(events[index]);
			BOOL result;
			result = WSAGetOverlappedResult(client->socket, &(client->overlapped), &transferredBytes, FALSE, &flags);
			if (result == FALSE || transferredBytes == 0) {
				closesocket(client->socket);
				closeEventInArray(events, index);
				freeSockInfo(socks, index);
				client = 0;
				nEvents--;
				continue;
			}

			// Check to see if the operation field equals RECEIVE. If this is so, then
			// this means a WSARecv call just completed => Tuc la phai hieu doan nay the nao>?
			if (client->operation == RECEIVE) {
				client->recvBytes = transferredBytes;
				client->sentBytes = 0;
				client->operation = SEND;
			}
			else
				client->sentBytes += transferredBytes;

			// Post another I/O operation
			// Since WSASend() is not guaranteed to send all of the bytes requested,
			// continue posting WSASend() calls until all received bytes are sent. => Dam bao gui du du lieu
			if (client->recvBytes > client->sentBytes) {
				client->dataBuf.buf = client->buffer + client->sentBytes;
				client->dataBuf.len = client->recvBytes - client->sentBytes;
				client->operation = SEND;
				if (WSASend(client->socket, &(client->dataBuf), 1, &transferredBytes, flags, &(client->overlapped), NULL) == SOCKET_ERROR) {
					if (WSAGetLastError() != WSA_IO_PENDING) {
						printf("WSASend() failed with error %d\n", WSAGetLastError());
						closesocket(client->socket);
						closeEventInArray(events, index);
						freeSockInfo(socks, index);
						client = 0;
						nEvents--;
						continue;
					}
				}
			}
			else {
				// No more bytes to send post another WSARecv() request
				memset(&(client->overlapped), 0, sizeof(WSAOVERLAPPED));
				client->overlapped.hEvent = events[index];
				client->recvBytes = 0;
				client->operation = RECEIVE;
				client->dataBuf.buf = client->buffer;
				client->dataBuf.len = DATA_BUFSIZE;
				flags = 0;
				if (WSARecv(client->socket, &(client->dataBuf), 1, &transferredBytes, &flags, &(client->overlapped), NULL) == SOCKET_ERROR) {
					if (WSAGetLastError() != WSA_IO_PENDING) {
						printf("WSARecv() failed with error %d\n", WSAGetLastError());
						closesocket(client->socket);
						closeEventInArray(events, index);
						freeSockInfo(socks, index);
						client = 0;
						nEvents--;
					}
				}
			}
		}
		checkNumberClient(socks);
		if (index != 0) {
			printf("\n");
			for (int i = 1; i < WSA_MAXIMUM_WAIT_EVENTS; i++) {
				if ((socks[i] != 0)) {
					//&& (socks[i]->operation == RECEIVE)
					char m[10];
					strcpy(m, "New client");
					SocketInfo* client;
					client = socks[i];
					WSABUF buff;
					buff.buf = m;
					buff.len = sizeof(m);
					/*WSASend(client->socket, &(buff), 1, &buff.len, &flags, &(client->overlapped), NULL);*/
					/*WSASend(client->socket, &(buff), 1, &buff.len, &flags, &(client->overlapped), NULL);*/
					Send(client->socket, m, 10, 0);
				}
			}
		}
		/*	sentToClient(socks, events, index);*/
	}


	return 0;
}

//giai phong 1 socket khoi mang
void freeSockInfo(SocketInfo* siArray[], int n) {
	closesocket(siArray[n]->socket);
	free(siArray[n]);
	siArray[n] = 0;
	printf("Socket %d closed\n", n);
	for (int i = n; i < WSA_MAXIMUM_WAIT_EVENTS - 1; i++) {
		//Doan nay don nhung socket con o vi tri phia sau len tren
		siArray[i] = siArray[i + 1];
	}
}

//giai phong event khoi array
void closeEventInArray(WSAEVENT eventArr[], int n) {
	WSACloseEvent(eventArr[n]);
	printf("Event %d closed\n", n);
	for (int i = n; i < WSA_MAXIMUM_WAIT_EVENTS - 1; i++) {
		//Doan nay don nhung event o phai sau len tren 1 don vi 

		eventArr[i] = eventArr[i + 1];
	}
}
void checkNumberClient(SocketInfo* siArray[]) {
	for (int i = 1; i < WSA_MAXIMUM_WAIT_EVENTS; i++) {
		if (siArray[i] != 0) {
			printf("Sock %d is active\n", i);
		}
	}
}

void sentToClient(SocketInfo* siArray[], WSAEVENT eventArr[]) {
	char m[10];
	strcpy(m, "New client");
	/*printf("Kich co: %d\n", sizeof(m));*/
	int needToTransferBytes = 10;

	for (int i = 1; i < WSA_MAXIMUM_WAIT_EVENTS; i++) {
		if (siArray[i] != 0) {
			SocketInfo* client;
			client = siArray[i];
			//client->sentBytes = 0;
			//Dam bao rang client trong trang thai receive
			while (client->operation != RECEIVE) {
				//Cho doi cho operation ve Receive
			}
			//Chuyen thanh trang thai send
			client->operation = SEND;
			client->sentBytes = 0;
			client->recvBytes = needToTransferBytes;
			int fl = 0;
			WSABUF buff;
			//buff.buf = m;
			//buff.len = needToTransferBytes;

			while (client->recvBytes > client->sentBytes) {
				/*client->dataBuf.buf = client->buffer + client->sentBytes;*/
				buff.buf = m + client->sentBytes;
				buff.len = client->recvBytes - client->sentBytes;

				client->operation = SEND;
				if (WSASend(client->socket, &(buff), 1, &needToTransferBytes, &fl, &(client->overlapped), NULL) == SOCKET_ERROR) {

				}
				client->sentBytes += needToTransferBytes;
			}
			client->operation = RECEIVE;
		}
	}


}