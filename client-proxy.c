/*
 * FILE:       client-proxy.c
 * AUTHOR:     SIWEN WANG, VICTOR MOLINA
 * COURSE:     CSC4XX SPRING 2022
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>

int clientSocket_server, ns;
int sproxySocket_client;
char sessionID[4];
int seq, ack;
int MAX_LEN = 1037;

typedef struct dataQueue {
	int len;
	int seq;
	char *data;
	struct dataQueue *next;
} dataQueue;

dataQueue *head = NULL;
dataQueue *tail = NULL;

void freeQueue() {
	dataQueue *curr;
	for (curr = head; curr != NULL; ) {
		//free(curr -> data);
		dataQueue *temp = curr -> next;
		free(curr);
		curr = temp;
	}
}

void sendHeartbeat() {
	char buf[6];
	buf[0] = '1';
	int i = 0;
    while (i + strlen(sessionID) < 4) {
        buf[i+1] = '0';
        i++;
    }
	strncat(buf, sessionID, 4);
	send(sproxySocket_client, buf, 5, 0);
}

void enqueue(int len, char *str) {
	dataQueue *newData = malloc(sizeof(dataQueue));
	newData -> len = len;

	char data[len];
	memcpy(data, str, len);
	newData -> data = data;

	//newData -> data = strndup(str, len);

	newData -> next = NULL;

	// int i;
	// printf(">>> in the queue\n");
	// for (i = 0; i<len;i++) {
	// 	printf("%d ", newData->data[i]);
	// }
	// printf("<<<\n");

	if (head == NULL) {
		head = newData;
		tail = newData;
		if (seq > 9999) seq = 1;
		newData -> seq = seq;
	} else {
		tail -> next = newData;
		int ts = tail -> seq;
		if (ts >= 9999) newData -> seq = 1;
		else newData -> seq = ts + 1;
		tail = newData;
	}
}

void dequeue() {
	if (head == NULL) return;
	dataQueue *temp = head;
	head = head -> next;
	//free(temp -> data);
	free(temp);
}

void sendToSproxy() {
	char data[MAX_LEN];
	data[0] = '0';

	char length[5] = "";
	char Seq[5] = "";
	char Ack[5] = "";
	sprintf(length, "%d", head -> len);
    sprintf(Seq, "%d", head -> seq);
    sprintf(Ack, "%d", ack);
	int i = 0; 
    for (; i < 4; i++) {
        if (length[i] == '\0') length[i] = ' ';
        if (Seq[i] == '\0') Seq[i] = ' ';
        if (Ack[i] == '\0') Ack[i] = ' ';
    }
	strncat(data, length, 4);
    strncat(data, Seq, 4);
    strncat(data, Ack, 4);
	//strncat(data, head -> data, head -> len);
	memcpy(data+13, head -> data, head -> len);

	send(sproxySocket_client, data, (head -> len)+13, 0);

	printf("+++sent to sproxy: +++%s\n", data);
	//printf(">>>data len: %ld<<<\n", strlen(data));
	//printf(">>>len: %d seq: %d ack: %d data: %s<<<\n", head->len, head->seq, ack, head -> data);
	bzero(data, MAX_LEN);
}

void informSproxy(int len) {
	char data[MAX_LEN];
	data[0] = '0';

	char length[5] = "";
	char Seq[5] = "";
	char Ack[5] = "";
	sprintf(length, "%d", len);
    sprintf(Seq, "%d", 0);
    sprintf(Ack, "%d", ack);
	int i = 0; 
    for (; i < 4; i++) {
        if (length[i] == '\0') length[i] = ' ';
        if (Seq[i] == '\0') Seq[i] = ' ';
        if (Ack[i] == '\0') Ack[i] = ' ';
    }
	strncat(data, length, 4);
    strncat(data, Seq, 4);
    strncat(data, Ack, 4);

	send(sproxySocket_client, data, 13, 0);

	printf("+++sent to sproxy: +++%s\n", data);
	bzero(data, MAX_LEN);
}

// create the socket that connect to sproxy
int connectToSproxy(char *port, char *ip) {
	if ((sproxySocket_client = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Failed to create client socket on cproxy. \n");
		exit(1);
	}
	struct sockaddr_in serverAddress;
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = inet_addr(ip); // 192.168.6.4
	serverAddress.sin_port = htons(atoi(port)); // 6200
	int re = connect(sproxySocket_client, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
	return re;
}

void setUpSocketToTelnet(char *port) {
	if ((clientSocket_server = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Failed to create server socket on cproxy. \n");
		exit(1);
	}
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(atoi(port)); // 5200
	if (bind(clientSocket_server, (struct sockaddr *)&address, sizeof(address)) != 0){
			fprintf(stderr, "Server socket failed to bind to client vm.\n");
			exit(1);
		}
	if (listen(clientSocket_server, 5) != 0){
		fprintf(stderr, "Server socket failed to listen to client vm.\n");
		exit(1);
	}
}

int main(int argc, char *argv[]) {
	if (argc != 4) {
		fprintf(stderr, "Invalid input argument. \n");
		exit(1);
	}

	// create the socket that connect to client vm/telnet
	// port is the first input argument, telnet should use the same port
	setUpSocketToTelnet(argv[1]);

	int session = 0;
	int newSession = 0;
	while (1) {
		struct sockaddr_in newAddress;
		int addrlen = sizeof(newAddress);
		ns = accept(clientSocket_server, (struct sockaddr *)&newAddress, (socklen_t *)&addrlen);
		if (ns < 0){
			fprintf(stderr, "Server socket failed to accept telnet connection.\n");
			exit(1);
		}
		printf("Cproxy is connected to client vm. \n");

		if (!newSession) {
			// create the socket that connect to sproxy
			int re = connectToSproxy(argv[3], argv[2]);
			if (re < 0){
				perror("Failed to connect to sproxy.\n");
				exit(1);
			}
			printf("Cproxy is connected to sproxy. \n");
		}

		int n;
		char serverBuffer[MAX_LEN], clientBuffer[MAX_LEN];
		session += 1;
		// send the first heartbeat
		sprintf(sessionID, "%d", session);
		sendHeartbeat();
		struct timeval tv;
		gettimeofday(&tv, NULL);
		int lastSent = tv.tv_sec;
		size_t len2 = recv(sproxySocket_client, clientBuffer, MAX_LEN, 0);
		if (len2 <= 0) exit(1);
		gettimeofday(&tv, NULL);
		int lastReceived = tv.tv_sec;
		if (clientBuffer[0] != '1') {
			fprintf(stderr, "first message is not a hearbeat[o]\n");
			exit(1);
		}
		bzero(clientBuffer, MAX_LEN);

		printf("> this session ID: %s\n", sessionID);

		head = NULL;
		tail = NULL;
		seq = 1;
		ack = 1;
		// configuring select()
		while (1) {
			fd_set readfds;
			FD_ZERO(&readfds);
			FD_SET(ns, &readfds);
			FD_SET(sproxySocket_client, &readfds);
			if (ns > sproxySocket_client)
				n = ns + 1;
			else n = sproxySocket_client + 1;

			// set a timeout
			struct timeval timeout;
			timeout.tv_sec = 1;
			int re = select(n, &readfds, NULL, NULL, &timeout);
			if (re < 0) {
				perror("Failed select()");
				break;
			}

			gettimeofday(&tv, NULL);
			int currTime = tv.tv_sec;
			if (currTime - lastSent >= 1) {
				sendHeartbeat();
				gettimeofday(&tv, NULL);
				lastSent = tv.tv_sec;
			}
			if (currTime - lastReceived >= 3) {
				// close the socket that connect to sproxy
				close(sproxySocket_client);
				// try to reconnect to sproxy
				while (1) {
					if (!newSession) {
						// still reading from telnet
						size_t len1 = recv(ns, serverBuffer, MAX_LEN, MSG_DONTWAIT);
						if (len1 == 0) newSession = 1;
						else if (len1 < 0) {
							perror("fail recv from telnet without blocking\n");
							exit(1);
						} else enqueue(len1, serverBuffer);
						bzero(serverBuffer, MAX_LEN);
					}
					int re1 = connectToSproxy(argv[3], argv[2]);
					if (re1 >= 0) break;
				}
				printf("Cproxy is reconnected to sproxy. \n");

				printf("> current session ID: %s\n", sessionID);
				printf("is new session? %d\n", newSession);

				if (newSession) break;

				// send the first heartbeat
				sprintf(sessionID, "%d", session);
				sendHeartbeat();
				struct timeval tv;
				gettimeofday(&tv, NULL);
				lastSent = tv.tv_sec;
				lastReceived = 0;
				size_t len2 = recv(sproxySocket_client, clientBuffer, MAX_LEN, 0);
				if (len2 <= 0) exit(1);
				gettimeofday(&tv, NULL);
				lastReceived = tv.tv_sec;
				if (clientBuffer[0] != '1') {
					fprintf(stderr, "first message is not a hearbeat[o]\n");
					exit(1);
				}
				bzero(clientBuffer, MAX_LEN);
				continue;
			}

			if (re == 0) continue;

			// got message from telnet
			if (FD_ISSET(ns, &readfds)) {
				size_t len1 = recv(ns, serverBuffer, MAX_LEN, 0);
				if (len1 <= 0) break;

				printf("+++get from telnet of len %ld: +++%s\n", len1, serverBuffer);

				// int i;
				// printf(">>>\n");
				// for (i = 0; i<len1;i++) {
				// 	printf("%d ", serverBuffer[i]);
				// }
				// printf("<<<\n");

				enqueue(len1, serverBuffer);

				//sendToSproxy();

				bzero(serverBuffer, MAX_LEN);
			}
			// got message from sproxy
			if(FD_ISSET(sproxySocket_client, &readfds)) {
				len2 = recv(sproxySocket_client, clientBuffer, MAX_LEN, 0);
				if (len2 <= 0) break;

				// heartbeat message
				if (clientBuffer[0] == '1') {
					gettimeofday(&tv, NULL);
					lastReceived = tv.tv_sec;
				// data message
				} else {
					printf("+++get from sproxy of len %ld: +++%s\n", len2, clientBuffer);

					int newSeq, newAck, length;
					char t[5];
					memcpy(t, clientBuffer+1, 4);
					t[4] = '\0';
					length = atoi(t);
					memcpy(t, clientBuffer+5, 4);
					t[4] = '\0';
					newSeq = atoi(t);
					memcpy(t, clientBuffer+9, 4);
					t[4] = '\0';
					newAck = atoi(t);

					//printf(">>>datalen: %d newSeq: %d newAck: %d<<<\n", length, newSeq, newAck);

					if (newSeq == ack) {
						
						send(ns, clientBuffer+13, length, 0);
						printf("+++sent to telnet: +++%s\n", clientBuffer+13);
						
						ack += 1;
						if (ack > 9999) ack = 1;

						informSproxy(0);

					}
					if (head != NULL) {
						if (newAck > head -> seq) {
							dequeue();
							seq += 1;
						} else if (newAck == 1 && head->seq == 9999) {
							dequeue();
							seq = 1;
						}
					}

					//printf(">>>current ack: %d<<<\n", ack);

				}

				bzero(clientBuffer, MAX_LEN);
			}

			if (head != NULL) sendToSproxy();

		}
		close(ns);
		freeQueue();
		if (!newSession) close(sproxySocket_client);
	}

	close(clientSocket_server);
	return 0;
}
