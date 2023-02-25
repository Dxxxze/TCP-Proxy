/*
 * FILE:       server-proxy.c
 * AUTHOR:     SIWEN WANG, VICTOR MOLINA
 * COURSE:     CSC 4XX SPRING 2022
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

int ns;
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
	send(ns, buf, 5, 0);
}

void enqueue(int len, char *str) {
	dataQueue *newData = malloc(sizeof(dataQueue));
	newData -> len = len;

	char data[len];
	memcpy(data, str, len);
	newData -> data = data;

	//newData -> data = strndup(str, len);

	newData -> next = NULL;

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

void sendToCproxy() {
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

	send(ns, data, (head -> len)+13, 0);
	printf("+++sent to cproxy: +++%s\n", data);
	//printf(">>>data len: %ld<<<\n", strlen(data));
	//printf(">>>len: %d seq: %d ack: %d data: %s<<<\n", head->len, head->seq, ack, head -> data);
	bzero(data, MAX_LEN);
}

void informCproxy(int len) {
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

	send(ns, data, 13, 0);
	printf("+++sent to cproxy: +++%s\n", data);
	bzero(data, MAX_LEN);
}

int main(int argc, char *argv[]){
	if (argc != 2) {
		fprintf(stderr, "Invalid input argument. \n");
		exit(1);
	}

	// create the server socket that connect to cproxy
	// int cproxySocket_server, ns;
	int cproxySocket_server;
	if ((cproxySocket_server = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Failed to create server socket on sproxy. \n");
		exit(1);
	}
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(atoi(argv[1])); // 6200
	if(bind(cproxySocket_server, (struct sockaddr *)&address, sizeof(address)) != 0){
			fprintf(stderr, "Server socket failed to bind to cproxy.\n");
			exit(1);
		}
	if(listen(cproxySocket_server, 5) != 0){
		fprintf(stderr, "Server socket failed to listen to cproxy.\n");
		exit(1);
	}

	int newSession = 0;
	while (1) {
		if (!newSession) {
			struct sockaddr_in newAddress;
			int addrlen = sizeof(newAddress);
			ns = accept(cproxySocket_server, (struct sockaddr *)&newAddress, (socklen_t *)&addrlen);
			if(ns < 0 ){
				fprintf(stderr, "Server socket failed to accept cproxy connection. \n");
				exit(1);	
			}
			printf("Sproxy is connected to cproxy. \n");
		}

		// create the socket that connect to server/telnet daemon
		int serverSocket_client, re;
		if ((serverSocket_client = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
			perror("Failed to create client socket on sproxy. \n");
			exit(1);
		}
		struct sockaddr_in daemonServerAddress;
		daemonServerAddress.sin_family = AF_INET;
		daemonServerAddress.sin_addr.s_addr = inet_addr("127.0.0.1"); //localhost
		daemonServerAddress.sin_port = htons(23); // port 23
		re = connect(serverSocket_client, (struct sockaddr *)&daemonServerAddress, sizeof(daemonServerAddress));
		if (re < 0){
			perror("Failed to connect to server/telnet daemon.\n");
			exit(1);
		}
		printf("Connected Server/Telnet Daemon.\n");

		int n, MAX_LEN = 1037;
		char serverBuffer[MAX_LEN], clientBuffer[MAX_LEN];
		// send the first heartbeat
		sendHeartbeat();
		struct timeval tv;
		gettimeofday(&tv, NULL);
		int lastSent = tv.tv_sec;
		// get the first heartbeat
		size_t len1 = recv(ns, serverBuffer, MAX_LEN, 0);
		if (len1 <= 0) exit(1);
		gettimeofday(&tv, NULL);
		int lastReceived = tv.tv_sec;
		if (serverBuffer[0] != '1') {
			fprintf(stderr, "first message is not a hearbeat[o]\n");
			exit(1);
		}
		newSession = 0;
		memcpy(sessionID, serverBuffer+1, 4);
		bzero(serverBuffer, MAX_LEN);

		head = NULL;
		tail = NULL;
		seq = 1;
		ack = 1;
		// configuring select()
		while (1) {
			fd_set readfds;
			FD_ZERO(&readfds);
			FD_SET(ns, &readfds);
			FD_SET(serverSocket_client, &readfds);
			if (ns > serverSocket_client)
				n = ns + 1;
			else n = serverSocket_client + 1;

			// set a timeout
			struct timeval timeout;
			timeout.tv_sec = 1;
			re = select(n, &readfds, NULL, NULL, &timeout);
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
				// close connection to cproxy
				close(ns);
				// and wait for reconnect
				struct sockaddr_in newAddress;
				int addrlen = sizeof(newAddress);
				ns = accept(cproxySocket_server, (struct sockaddr *)&newAddress, (socklen_t *)&addrlen);
				if(ns < 0 ){
					fprintf(stderr, "Server socket failed to accept cproxy connection. \n");
					exit(1);	
				}
				printf("Sproxy is connected back to cproxy. \n");

				len1 = recv(ns, serverBuffer, MAX_LEN, 0);
				gettimeofday(&tv, NULL);
				lastReceived = tv.tv_sec;
				if (serverBuffer[0] != '1') {
					fprintf(stderr, "first message is not a hearbeat\n");
					exit(1);
				}
				if (strncmp(sessionID, serverBuffer+1, 4) != 0) {

					printf("> ? old sessionID: %s\n", sessionID);
					printf("> first heartbeat message: %s\n", serverBuffer);

					newSession = 1;
					memcpy(sessionID, serverBuffer+1, 4);
					break;
				}
				// send the first heartbeat
				sendHeartbeat();
				gettimeofday(&tv, NULL);
				lastSent = tv.tv_sec;
				bzero(serverBuffer, MAX_LEN);
				continue;
			}

			if (re == 0) continue;

			// got message from cproxy
			if (FD_ISSET(ns, &readfds)) {
				size_t len1 = recv(ns, serverBuffer, MAX_LEN, 0);
				if (len1 <= 0) break;

				// heartbeat message
				if (serverBuffer[0] == '1') {
					gettimeofday(&tv, NULL);
					lastReceived = tv.tv_sec;
				// data message
				} else {
					printf("+++get from cproxy of length %ld: +++%s\n", len1, serverBuffer);
					
					// int i;
					// printf(">>>\n");
					// for (i = 0; i<len1;i++) {
					// 	printf("%d ", serverBuffer[i]);
					// }
					// printf("<<<\n");

					int newSeq, newAck, length;
					char t[5];
					memcpy(t, serverBuffer+1, 4);
					t[4] = '\0';
					length = atoi(t);
					memcpy(t, serverBuffer+5, 4);
					t[4] = '\0';
					newSeq = atoi(t);
					memcpy(t, serverBuffer+9, 4);
					t[4] = '\0';
					newAck = atoi(t);

					//printf(">>>datalen: %d newSeq: %d newAck: %d<<<\n", length, newSeq, newAck);
					//printf(">>>seq: %d ack: %d stored seq: %d<<<\n", head -> seq, ack, seq);

					if (newSeq == ack) {
						send(serverSocket_client, serverBuffer+13, length, 0);

						printf("+++sent to daemon: +++%s\n", serverBuffer+13);

						ack++;
						if (ack > 9999) ack = 1;


						informCproxy(0);

					}
					if (head != NULL) {
						if (newAck > head -> seq) {
							dequeue();
							seq += 1;

							//if (head == NULL) printf("new seq: %d\n", seq);

						} else if (newAck == 1 && head->seq == 9999) {
							dequeue();
							seq = 1;
						}
					}				
				}
				bzero(serverBuffer, MAX_LEN);
			}
			// got message from telnet daemon
			if(FD_ISSET(serverSocket_client, &readfds)) {

				//printf("anything from daemon? \n");

				size_t len2 = recv(serverSocket_client, clientBuffer, MAX_LEN, 0);
				if (len2 <= 0) break;

				printf("+++get from daemon of length %ld: +++%s\n", len2, clientBuffer);

				enqueue(len2, clientBuffer);

				//sendToCproxy();

				//send(ns, data, len2+1, 0);
				//printf("+++sent to cproxy: +++%s\n", data);

				bzero(clientBuffer, MAX_LEN);
			}

			if (head != NULL) sendToCproxy();

		}
		if (!newSession) close(ns);
		freeQueue();
		close(serverSocket_client);
	}

	close(cproxySocket_server);
	return 0;
}
