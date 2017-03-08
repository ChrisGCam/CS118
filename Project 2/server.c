#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/wait.h>
#include <signal.h>

int const MAX_PACKET_SIZE = 1024;
int const HEADER_SIZE = 24;

int main(int argc, char *argv[])
{
	fprintf(stdout, "%i \n", sizeof(socklen_t));
	
	
	return 0;
}

// -1 = invalid size
// Wrapper function for sendto to also construct send a header in the packet
int sendPacket(int sockfd, char* message, size_t len, const struct sockaddr *dest_addr, socklen_t dest_len, int seqNum, int wnd, int ret, int syn, int fin)
{
	int result = -1;
	
	int packetLen = HEADER_SIZE + len;
	
	if(packetLen > MAX_PACKET_SIZE)
		return -1;
	
	char* toSend = malloc(packetLen);
	bzero(toSend, packetLen);
	
	int intSize = sizeof(int);
	
	memcpy(toSend, &len, intSize);
	memcpy(toSend + intSize, &seqNum, intSize);
	memcpy(toSend + intSize*2, &wnd, intSize);
	memcpy(toSend + intSize*3, &ret, intSize);
	memcpy(toSend + intSize*4, &syn, intSize);
	memcpy(toSend + intSize*5, &fin, intSize);
	
	result = sendto(sockfd, toSend, packetLen, 0, dest_addr, dest_len);
	
	free(toSend);
	
	return result;
}

int getPacket(int sockfd, void* message, size_t* len, struct sockaddr *src_addr, socklen_t * src_len, int* seqNum, int* wnd, int* ret, int* syn, int* fin)
{
	int result = -1;
	int packetLen = HEADER_SIZE + MAX_PACKET_SIZE;
	
	char* received = malloc(packetLen);
	bzero(received, packetLen);
	
	result = recvfrom(sockfd, received, packetLen, 0, src_addr, src_len);
	
	memcpy(message, received + HEADER_SIZE, MAX_PACKET_SIZE - HEADER_SIZE);
	
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