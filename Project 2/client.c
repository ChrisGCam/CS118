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

// For UDP socket programming, the following tutorial was used: https://www.cs.rutgers.edu/~pxk/417/notes/sockets/udp.html

int const MAX_PACKET_SIZE = 1024;
int HEADER_SIZE = 24;
int MAX_PAYLOAD_SIZE = 1000;

int main(int argc, char *argv[])
{
	// Check for correct argument length
	if (argc != 4) {
		fprintf(stderr, "ERROR: incorrect arguments.\nUSAGE: ./client <port> <ip> <filename>\nSet <ip> to 1 for same machine\n");
		exit(1);
	}
	
	HEADER_SIZE = sizeof(int) * 6;
	MAX_PAYLOAD_SIZE = MAX_PACKET_SIZE - HEADER_SIZE;
	
	struct sockaddr_in servAddr;
	socklen_t servAddrLen = sizeof(servAddr);
	int fd, portno, recvLen, i;
	char buf[MAX_PACKET_SIZE];
	
	// Set the IP address to local machine
	char* IPAddress = "127.0.0.1";
	
	int len, seqNum, wnd = 5120, ret, syn, fin;
	
	// Get the portnumber
	portno = atoi(argv[1]);
	if(portno < 0)
	{
		fprintf(stderr, "ERROR: invalid port number.\n");
		exit(1);
	}
	
	// If IP address is "1", then leave it as local machine IP address
	// Otherwise record the IP address
	if(strcmp(argv[2],"1") != 0)
		IPAddress = argv[2];
	
	
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		fprintf(stderr, "ERROR: socket creation failed.\n");
		exit(1);
	}
	
	bzero((char *)&servAddr, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(portno);

	if(inet_aton(IPAddress, &servAddr.sin_addr) == 0)
	{
		fprintf(stderr, "ERROR: inet_aton() failed.\n");
		exit(1);
	}
	
	

	// Send the wanted file name
	sprintf(buf, "%s", argv[3]);
	
	sendPacket(fd, buf, strlen(buf), (struct sockaddr *)&servAddr, servAddrLen, 0, wnd, 0, 1, 0);
	getPacket(fd, buf, &len, (struct sockaddr *)&servAddr, &servAddrLen, &seqNum, &wnd, &ret, &syn, &fin);
	fprintf(stdout, "Received Contents:\n%s\n\n", buf);

	sendPacket(fd, buf, 0, (struct sockaddr *)&servAddr, servAddrLen, seqNum+1, wnd, 0, 1, 0);
	
	
	close(fd);
	return 0;
}


int sendPacket(int sockfd, char* message, size_t len, const struct sockaddr *dest_addr, socklen_t dest_len, int seqNum, int wnd, int ret, int syn, int fin)
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
	memcpy(toSend + intSize * 3, &ret, intSize);
	memcpy(toSend + intSize * 4, &syn, intSize);
	memcpy(toSend + intSize * 5, &fin, intSize);
	memcpy(toSend + intSize * 6, message, len);
	result = sendto(sockfd, toSend, packetLen, 0, dest_addr, dest_len);
	if (result < 0)
	{
		perror("sendto failed.");
		return -1;
	}

	free(toSend);

	return result;
}

// Wrapper function for recvfrom that also gets the header contents from the packet
// Also gets the sock address of the sender
int getPacket(int sockfd, char* message, size_t* len, struct sockaddr *src_addr, socklen_t * src_len, int* seqNum, int* wnd, int* ret, int* syn, int* fin)
{

	int result = -1;
	int packetLen = MAX_PACKET_SIZE;

	char* received = malloc(packetLen);
	bzero(received, packetLen);
	bzero(message, packetLen);


	result = recvfrom(sockfd, received, packetLen, 0, src_addr, src_len);

	memcpy(message, received + 24, MAX_PAYLOAD_SIZE);

	int intSize = sizeof(int);

	memcpy(len, received, intSize);
	memcpy(seqNum, received + intSize, intSize);
	memcpy(wnd, received + intSize * 2, intSize);
	memcpy(ret, received + intSize * 3, intSize);
	memcpy(syn, received + intSize * 4, intSize);
	memcpy(fin, received + intSize * 5, intSize);

	free(received);

	return result;
}