#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>
#include <netdb.h>

// For UDP socket programming, the following tutorial was used: https://www.cs.rutgers.edu/~pxk/417/notes/sockets/udp.html
// For select(), the following tutorial was used: http://beej.us/guide/bgnet/output/html/multipage/selectman.html


int const MAX_PACKET_SIZE = 1024;
int HEADER_SIZE = 24;
int MAX_PAYLOAD_SIZE = 1000;
const int RTOTime = 500;
const int MAX_SEQ_NUM = 30720;

int main(int argc, char *argv[])
{
	// Check for correct argument length
	if (argc != 4) {
		fprintf(stderr, "ERROR: incorrect arguments.\nUSAGE: ./client <port> <hostname> <filename>\nSet <ip> to 1 for same machine\n");
		exit(1);
	}
	
	HEADER_SIZE = sizeof(int) * 6;
	MAX_PAYLOAD_SIZE = MAX_PACKET_SIZE - HEADER_SIZE;
	
	struct sockaddr_in servAddr;
	socklen_t servAddrLen = sizeof(servAddr);
	int fd, portno, recvLen, i;
	char buf[MAX_PACKET_SIZE];
	
	
	int len = 0, seqNum = 0, wnd = 5120, syn = 0, fin = 0, ret = 0;
	unsigned int fileStart = 0;

	// Store the last 10 ACK numbers we sent
	int * transmitted = (int*)malloc(sizeof(int) * 10);

	for (i = 0; i < 10; i++)
		transmitted[i] = -1;

	struct timespec start, end, beginTransfer, endTransfer;
	int totalFileLength = 0;

	// Get the portnumber
	portno = atoi(argv[1]);
	if(portno < 0)
	{
		fprintf(stderr, "ERROR: invalid port number.\n");
		exit(1);
	}
		
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		fprintf(stderr, "ERROR: socket creation failed.\n");
		exit(1);
	}
	
	struct hostent * server;
	server = gethostbyname(argv[2]);
	if(server == NULL)
	{
		fprintf(stderr, "ERROR: invalid host name.\n");
		exit(1);
	}
	
	bzero((char *)&servAddr, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(portno);
	memcpy(&servAddr.sin_addr.s_addr, server->h_addr, server->h_length);

	// Get the time when we start attempting a connection
	clock_gettime(CLOCK_MONOTONIC_RAW, &beginTransfer);

	// SYN/SYN-ACK Handshake
	ret = 0;
sendSYN:
	// Send the SYN with seqNum of 0
	sendPacket(fd, buf, 0, (struct sockaddr *)&servAddr, servAddrLen, 0, wnd, 1, 0, 0);
	if(ret == 0)
		fprintf(stdout, "Sending packet SYN\n");
	else
		fprintf(stdout, "Sending packet Retransmission SYN\n");

	// Set up a timeout timer to resend SYN
	fd_set readfds;
	struct timeval tv;
	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);
	tv.tv_sec = 0;
	tv.tv_usec = RTOTime * 1000;
	int rv = select(fd + 1, &readfds, NULL, NULL, &tv);
	if (rv == 0)
	{
		ret = 1;
		goto sendSYN;
	}

	// Get the SYNACK with seqNum 0
	recvLen = getPacket(fd, buf, &len, (struct sockaddr *)&servAddr, &servAddrLen, &seqNum, &wnd, &syn, &fin, &fileStart);
	if(syn == 1 && seqNum == 0 && len == 0)
		fprintf(stdout, "Receiving packet SYN-ACK\n");
	else
	{
		ret = 1;
		goto sendSYN;
	}
	ret = 0;


	
	// Send the wanted file name with seqNum HEADER_SIZE + file name length
sendFileName:
	sprintf(buf, "%s", argv[3]);
	//printf("DEBUG: %i\n", strlen(buf));
	sendPacket(fd, buf, strlen(buf), (struct sockaddr *)&servAddr, servAddrLen, 0, wnd, 0, 0, 0);
	if(ret == 0)
		fprintf(stdout, "Sending packet 0 FileName\n\n");
	else
		fprintf(stdout, "Sending packet 0 Retransmission FileName\n\n");

	ret = 1;
	bzero(buf, MAX_PACKET_SIZE);

	// Set up a timeout timer to resend filename if needed
	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);
	tv.tv_sec = 0;
	tv.tv_usec = RTOTime * 1000;
	rv = select(fd + 1, &readfds, NULL, NULL, &tv);
	if (rv == 0)
	{
		ret = 1;
		goto sendFileName;
	}


	FILE *fp = fopen("received.data", "w");

	if (fp == NULL)
	{
		printf("ERROR: received.data file creation\n");
		exit(1);
	}
	
	while (fin == 0)
	{
		bzero(buf, MAX_PACKET_SIZE);
		getPacket(fd, buf, &len, (struct sockaddr *)&servAddr, &servAddrLen, &seqNum, &wnd, &syn, &fin, &fileStart);

		if (syn == 1 && fin == 0)
			goto sendFileName;
		else if(fin == 0)
			fprintf(stdout, "Receiving packet %i\n", seqNum);
		else
		{
			fprintf(stdout, "Receiving packet %i FIN\n", seqNum);
			seqNum += HEADER_SIZE;
			seqNum %= MAX_SEQ_NUM;
			break;
		}
		
		int ACKNum = seqNum + len + HEADER_SIZE;
		ACKNum %= MAX_SEQ_NUM;
		
		fseek(fp, fileStart, SEEK_SET);
		fwrite(buf, sizeof(char), len, fp);

		if (totalFileLength < fileStart)
			totalFileLength = fileStart + MAX_PAYLOAD_SIZE;

		ret = addToTransmitted(transmitted, ACKNum);

		if (ret == 0)
		{
			sendPacket(fd, buf, 0, (struct sockaddr *)&servAddr, servAddrLen, ACKNum, wnd, 0, 0, 0);
			printf("Sending packet %i\n", ACKNum);
		}
		else
		{
			sendPacket(fd, buf, 0, (struct sockaddr *)&servAddr, servAddrLen, ACKNum, wnd, 0, 0, 0);
			printf("Sending packet %i Retransmission\n", ACKNum);
		}
	}

	ret = 0;
	// FIN/FIN-ACK Handshake
sendFIN:
	sendPacket(fd, buf, 0, (struct sockaddr *)&servAddr, servAddrLen, seqNum, wnd, 0, 1, 0);
	if(ret == 0)
		printf("Sending packet %i FIN\n", seqNum);
	else
		printf("Sending packet %i Retransmission FIN\n", seqNum);

	// Set up a timeout timer to resend FIN if needed
	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);
	tv.tv_sec = 0;
	tv.tv_usec = RTOTime * 1000;
	rv = select(fd + 1, &readfds, NULL, NULL, &tv);
	if (rv == 0)
	{
		ret = 1;
		goto sendFIN;
	}

	fin = 0;
	syn = 1;
	int retSeqNum = -1;
	// Get FIN-ACK and then enter TIME-WAIT
	while (1 == 1)
	{
		getPacket(fd, buf, &len, (struct sockaddr *)&servAddr, &servAddrLen, &retSeqNum, &wnd, &syn, &fin, &fileStart);

		if (fin == 1 && syn == 0 && retSeqNum == seqNum)
		{
			break;
		}

		else
		{
			printf("Receiving packet %i\n", retSeqNum);
			ret = 1;
			goto sendFIN;
		}
	}

	printf("Receiving packet %i FIN-ACK\n", retSeqNum);

	printf("Entering TIME-WAIT state\n");

	// Get the time when we finish the connection
	clock_gettime(CLOCK_MONOTONIC_RAW, &endTransfer);
	int totalTime = (endTransfer.tv_sec - beginTransfer.tv_sec) * 1000 + (endTransfer.tv_nsec - beginTransfer.tv_nsec) / 1000000;

	// Figure out the average time of each packet by calculating how many data packets and how many handshake packets we had to send
	int avgRTO = totalTime / (totalFileLength / MAX_PAYLOAD_SIZE + 4);
	int waitTime;
	if (RTOTime > avgRTO)
		waitTime = RTOTime * 4;
	else
		waitTime = avgRTO * 4;

	int msecElapsed = 0;
	clock_gettime(CLOCK_MONOTONIC_RAW, &start);
	
	// During this time period, send ACKs to any last messages
	while (msecElapsed < waitTime)
	{
		// ACK the FIN
		sendPacket(fd, buf, 0, (struct sockaddr *)&servAddr, servAddrLen, (seqNum + HEADER_SIZE) % MAX_SEQ_NUM, wnd, 0, 0, 0);
		printf("Sending packet %i\n", seqNum);

		clock_gettime(CLOCK_MONOTONIC_RAW, &end);
		msecElapsed = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
		int timeoutTime = waitTime - msecElapsed;
		if (timeoutTime < 0)
			break;

		// Set up a timeout timer to exit the program once TIME-WAIT ends
		fd_set readfds;
		struct timeval tv;
		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);
		tv.tv_sec = 0;
		tv.tv_usec = timeoutTime * 1000;
		int rv = select(fd + 1, &readfds, NULL, NULL, &tv);
		if (rv == 0)
		{
			goto closeConn;
		}

		getPacket(fd, buf, &len, (struct sockaddr *)&servAddr, &servAddrLen, &seqNum, &wnd, &syn, &fin, &fileStart);
		if(fin == 1 && syn == 1 && seqNum == retSeqNum)
			printf("Receiving packet %i FIN-ACK\n", retSeqNum);
		else if(fin == 1)
			printf("Receiving packet %i FIN\n", seqNum);
		else
			printf("Receiving packet %i\n", seqNum);
	}
	
	closeConn:
	
	printf("Closing the connection...\n");

	fclose(fp);
	close(fd);
	return 0;
}

// Return 0 if new, 1 if it was inside already (this is a retransmission)
int addToTransmitted(int* transmitted, int ACKNum)
{
	int i;
	for (i = 0; i < 10; i++)
	{
		// If we find a new spot, put the number in there
		if (transmitted[i] == -1)
		{
			transmitted[i] = ACKNum;
			return 0;
		}
		// If we find it's in the array already, return 0
		else if (ACKNum == transmitted[i])
			return 1;
	}

	// Otherwise, it's not in the array and it's full
	for (i = 0; i < 9; i++)
	{
		transmitted[i] = transmitted[i + 1];
	}

	transmitted[9] = ACKNum;

	return 0;
}

// message is the payload bytes, and len is the length of the payload bytes
int sendPacket(int sockfd, char* message, size_t len, const struct sockaddr *dest_addr, socklen_t dest_len, int seqNum, int wnd, int syn, int fin, unsigned int fileStart)
{

	int result = -1;

	int packetLen = HEADER_SIZE + len;

	// Payload too large
	if (packetLen > MAX_PACKET_SIZE)
		return -1;

	char* toSend = malloc(packetLen);
	bzero(toSend, packetLen);

	int intSize = sizeof(int);

	// Copy in the header information
	memcpy(toSend, &len, intSize);
	memcpy(toSend + intSize, &seqNum, intSize);
	memcpy(toSend + intSize * 2, &wnd, intSize);
	memcpy(toSend + intSize * 3, &syn, intSize);
	memcpy(toSend + intSize * 4, &fin, intSize);
	memcpy(toSend + intSize * 5, &fileStart, intSize);
	memcpy(toSend + HEADER_SIZE, message, len);
	result = sendto(sockfd, toSend, packetLen, 0, dest_addr, dest_len);
	if (result < 0)
	{
		perror("sendto failed.");
		return -1;
	}

	free(toSend);

	return result;
}

// Wrapper function for recvfrom that also gets the header contents from the packet and copies them into the corresponding parameters
int getPacket(int sockfd, char* message, size_t* len, struct sockaddr *src_addr, socklen_t * src_len, int* seqNum, int* wnd, int* syn, int* fin, unsigned int* fileStart)
{

	int result = -1;
	int packetLen = MAX_PACKET_SIZE;

	char* received = malloc(packetLen);
	bzero(received, packetLen);
	bzero(message, packetLen);


	result = recvfrom(sockfd, received, packetLen, 0, src_addr, src_len);

	int intSize = sizeof(int);

	memcpy(len, received, intSize);
	memcpy(seqNum, received + intSize, intSize);
	memcpy(wnd, received + intSize * 2, intSize);
	memcpy(syn, received + intSize * 3, intSize);
	memcpy(fin, received + intSize * 4, intSize);
	memcpy(fileStart, received + intSize * 5, intSize);
	memcpy(message, received + HEADER_SIZE, MAX_PAYLOAD_SIZE);

	free(received);

	return result;
}