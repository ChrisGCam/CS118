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


// For UDP socket programming, the following tutorial was used: https://www.cs.rutgers.edu/~pxk/417/notes/sockets/udp.html

int const MAX_PACKET_SIZE = 1024;
int HEADER_SIZE;
int MAX_PAYLOAD_SIZE;

int main(int argc, char *argv[])
{
	// Check for correct argument length
	if (argc != 2) {
		fprintf(stderr, "ERROR: incorrect arguments.\n");
		exit(1);
	}
	
	HEADER_SIZE = sizeof(int) * 6;
	MAX_PAYLOAD_SIZE = MAX_PACKET_SIZE - HEADER_SIZE;
	
	struct sockaddr_in myAddr, clientAddr;
	socklen_t myAddrLen = sizeof(myAddr);
	socklen_t clientAddrLen = sizeof(clientAddr);
	int fd, portno, recvLen;
	char buf[MAX_PACKET_SIZE];
	
	int len, seqNum, wnd, ret, syn, fin;
	
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
	
	bzero((char *)&myAddr, sizeof(myAddr));
	myAddr.sin_family = AF_INET;
	myAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	myAddr.sin_port = htons(portno);

	if (bind(fd, (struct sockaddr *)&myAddr, sizeof(myAddr)) < 0) {
		perror("bind failed");
		return 0;
	}

	if (getsockname(fd, (struct sockaddr *)&myAddr, &myAddrLen) < 0) {
		perror("getsockname failed");
		return 0;
	}
	
	// Get a packet from the client
	getPacket(fd, buf, &len, (struct sockaddr *)&clientAddr, &clientAddrLen, &seqNum, &wnd, &ret, &syn, &fin);
	printf("Received packet from %s:%d\nData: %s\n\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), buf);
	
	close(fd);
	return 0;
}

// -1 = invalid size
// Wrapper function for sendto to also construct send a header in the packet
int sendPacket(int sockfd, char* message, size_t len, const struct sockaddr *dest_addr, socklen_t dest_len, int seqNum, int wnd, int ret, int syn, int fin)
{
	int result = -1;
	
	int packetLen = HEADER_SIZE + len;
	
	// Payload too large
	if(packetLen > MAX_PACKET_SIZE)
		return -1;
	
	char* toSend = malloc(packetLen);
	bzero(toSend, packetLen);
	
	int intSize = sizeof(int);
	
	// Copy in the header information
	memcpy(toSend, &len, intSize);
	memcpy(toSend + intSize, &seqNum, intSize);
	memcpy(toSend + intSize*2, &wnd, intSize);
	memcpy(toSend + intSize*3, &ret, intSize);
	memcpy(toSend + intSize*4, &syn, intSize);
	memcpy(toSend + intSize*5, &fin, intSize);
	
	result = sendto(sockfd, toSend, packetLen, 0, dest_addr, dest_len);
	if(result < 0)
	{
		perror("sendto failed.");
		return -1;
	}
	
	free(toSend);
	
	return result;
}

// Wrapper function for recvfrom that also gets the header contents from the packet
// Also gets the sock address of the sender
int getPacket(int sockfd, void* message, size_t* len, struct sockaddr *src_addr, socklen_t * src_len, int* seqNum, int* wnd, int* ret, int* syn, int* fin)
{
	int result = -1;
	int packetLen = MAX_PACKET_SIZE;
	
	char* received = malloc(packetLen);
	bzero(received, packetLen);
	
	result = recvfrom(sockfd, received, packetLen, 0, src_addr, src_len);
	
	memcpy(message, received + HEADER_SIZE, MAX_PAYLOAD_SIZE);
	
	int intSize = sizeof(int);
	
	memcpy(len, received, intSize);
	memcpy(seqNum, received + intSize, intSize);
	memcpy(wnd, received + intSize*2, intSize);
	memcpy(ret, received + intSize*3, intSize);
	memcpy(syn, received + intSize*4, intSize);
	memcpy(fin, received + intSize*5, intSize);
	
	free(received);
	
	return result;
}