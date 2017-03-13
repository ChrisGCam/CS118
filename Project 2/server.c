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
#include <time.h>
#include <signal.h>
#include <sys/select.h>

const int MAX_PACKET_SIZE = 1024;
int HEADER_SIZE = 24;
int MAX_PAYLOAD_SIZE = 1000;
const int RTOTime = 500;

// For UDP socket programming, the following tutorial was used: https://www.cs.rutgers.edu/~pxk/417/notes/sockets/udp.html
// For select(), the following tutorial was used: http://beej.us/guide/bgnet/output/html/multipage/selectman.html


int main(int argc, char *argv[])
{

	// Check for correct argument length
	if (argc != 2) {
		fprintf(stderr, "ERROR: incorrect arguments.\n");
		exit(1);
	}
	
	// There's 6 integers in teh header
	HEADER_SIZE = sizeof(int) * 6;
	MAX_PAYLOAD_SIZE = MAX_PACKET_SIZE - HEADER_SIZE;

	struct sockaddr_in myAddr, clientAddr;
	socklen_t myAddrLen = sizeof(myAddr);
	socklen_t clientAddrLen = sizeof(clientAddr);
	int portno, recvLen, sendLen, i;
	char buf[MAX_PACKET_SIZE];

	int len = 0, seqNum = 0, wnd = 5120, ret = 0, syn = 0, fin = 0;
	int fd;
	int numACKsRcvd = 0;
	
	// The current window will be the range (sendBase, sendBase + wnd)
	int sendBase = 0;

	// How many packets can fit in wnd
	int wndSize;

	// The byte location of the file for the sent packets
	int * fileLocs;
	// The seq numbers of the sent packets (needed to match with incomingACKs)
	int * wndSeqs;
	// Whether or not the packet was acked; If ACKed, set to the ACK number
	int * ACKed;
	// The RTO  timers for individual packets
	time_t * timers;

	// Get the portnumber
	portno = atoi(argv[1]);
	if(portno < 0)
	{
		fprintf(stderr, "ERROR: Invalid port number\n");
		exit(-1);
	}
	

	/****************************************************************************************/
	// Establish UDP connection
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		fprintf(stderr, "ERROR: Socket creation failed\n");
		exit(-1);
	}
	
	bzero((char *)&myAddr, sizeof(myAddr));
	myAddr.sin_family = AF_INET;
	myAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	myAddr.sin_port = htons(portno);

	if (bind(fd, (struct sockaddr *)&myAddr, sizeof(myAddr)) < 0) {
		perror("ERROR: Bind failed");
		return -1;
	}

	if (getsockname(fd, (struct sockaddr *)&myAddr, &myAddrLen) < 0) {
		perror("ERROR: getsockname failed");
		return -1;
	}
	/***************************************************************************************
	/*
	// SYN/SYNACK handshake
	// Get the SYN and initial sequence number
	recvLen = getPacket(fd, buf, &len, (struct sockaddr *)&clientAddr, &clientAddrLen, &seqNum, &wnd, &ret, &syn, &fin);
	
	if (recvLen < 0)
	{
		sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, 0, 0, 0, 0, 1);
		perror("ERROR: Receiving SYN\n");
		return -1;
	}

	fprintf(stdout, "Received messages from %s:%d\n\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));

	// Check if the first message was a SYN message
	if (syn != 1)
	{
		sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, 0, 0, 0, 0, 1);
		perror("ERROR: First packet received was not a SYN\n");
		return -1;
	}

	fprintf(stdout, "Receiving SYN\n");
	
	// Send SYN ACK
	sendLen = sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, 0, 0, 0, 1, 0);
	if (sendLen < 0)
	{
		sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, 0, 0, 0, 0, 1);
		perror("ERROR: Sending SYN ACK\n");
		return -1;
	}
	fprintf(stdout, "Sending SYN ACK\n");
	*/


	// Get the name of the requested file
	recvLen = getPacket(fd, buf, &len, (struct sockaddr *)&clientAddr, &clientAddrLen, &seqNum, &wnd, &ret, &syn, &fin);
	if (recvLen < 0)
	{
		sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, 0, wnd, 0, 0, 1);
		perror("ERROR: Receiving file name\n");
		return -1;
	}
	
	fprintf(stdout, "Received request from %s:%d\nRequested File Name: %s\n\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), buf);

	// Get a file pointer
	char* fileContents;
	FILE *fp = fopen(buf, "r");

	if (fp == NULL)
	{
		sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, 0, wnd, 0, 0, 1);
		perror("ERROR: File not found\n");
		exit(1);
	}

	// Get the file size
	size_t fileSize = 0;
	fseek(fp, 0, SEEK_END);
	fileSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (fileSize == 0)
	{
		sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, 0, wnd, 0, 0, 1);
		perror("ERROR: File is empty\n");
		exit(1);
	}

	// Allocate space for the file + 1 for a null byte terminator
	fileContents = malloc(fileSize + 1);
	if (fileContents == NULL)
	{
		sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, 0, wnd, 0, 0, 1);
		perror("ERROR: Allocating buffer for file contents\n");
		exit(1);
	}

	// Copy the contents of the file into fileContents and null byte terminate it
	size_t fileLen = fread(fileContents, 1, fileSize, fp);
	fileContents[fileLen] = '\0';
	

	// Calculate how many packets fit in the window and allocate space to keep an array of what has been ACKed in the current window
	wndSize = wnd / MAX_PACKET_SIZE;
	if (wnd % MAX_PACKET_SIZE > 0)
		wndSize++;

	// Allocate space in the arrays
	wndSeqs = (int*)malloc(sizeof(int) * wndSize);
	ACKed = (int*)malloc(sizeof(int) * wndSize);
	fileLocs = (int*)malloc(sizeof(int) * wndSize);
	timers = (time_t*)malloc(sizeof(time_t) * wndSize);

	// Set all the timers, ACKs, and seq numbers to -1
	for (i = 0; i < wndSize; i++)
	{
		timers[i] = -1;
		ACKed[i] = -1;
		wndSeqs[i] = -1;
		fileLocs[i] = -1;
	}

	
	// Number of packets sent in the current window
	int packetsSent = 0;

	// The byte of the file that corresponds to to current sendBase
	// This is different from sendBase because seq includes header bytes (i.e. is incremented by 1024 on each packet), 
	// but currLoc just keeps track of payload bytes (only 1000 bytes of the file are sent at a time)
	int currLoc = 0;

	// While a fin message isn't sent/received yet...
	while (fin == 0)
	{
		// Send the packets in the current window
		while (currLoc < fileLen && packetsSent < wndSize)
		{
			// Calculate the sequence number of this packet % 30720 (since that is the max sequence number)
			seqNum = (sendBase + packetsSent * MAX_PACKET_SIZE) % 30720;

			// Initialize packetSize as the largest possible packet size
			int packetSize = MAX_PACKET_SIZE;

			// On the last packet, send fewer bytes if needed
			if (fileLen - currLoc < MAX_PAYLOAD_SIZE)
				packetSize = fileLen - currLoc + HEADER_SIZE;

			// Make an buffer for the packet we're sending and send it
			char toSend[packetSize];
			bzero(toSend, packetSize);
			memcpy(toSend, fileContents + currLoc, packetSize);
			sendPacket(fd, toSend, packetSize, (struct sockaddr *)&clientAddr, clientAddrLen, seqNum, wnd, 0, 0, 0);
			fprintf(stdout, "Sending packet %i %i\n", seqNum, wnd);

			// Start the timer
			time(&timers[packetsSent]);
			// Set the packet as not ACKed
			ACKed[packetsSent] = -1;
			// Set the sequence number of this packet
			wndSeqs[packetsSent] = seqNum;
			// Set the starting byte of the sent file contents
			fileLocs[packetsSent] = currLoc;

			// Advance the location of the file we need to send
			currLoc += packetSize - HEADER_SIZE;
			// Increment the number of packets we sent in this current window
			packetsSent++;
		}
		
		// Get the time of the oldest packet
		int msec = 0;
		time_t before, now;
		time(&now);
		double difference;

		int oldestIndex = -1;
		int oldestTime = -1;

		for (i = 0; i < wndSize; i++)
		{
			// Check if the entry is valid and that it hasn't been acked
			if (wndSeqs[i] != -1 && ACKed[i] == -1)
			{
				before = timers[i];
				difference = difftime(now, before);
				difference *= 1000;

				if (difference > oldestTime)
				{
					oldestTime = msec;
					oldestIndex = i;
				}

			}
		}

		// Set up a timer based on the oldest packet to break out of getPacket if needed
		fd_set readfds;
		struct timeval tv;
		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);
		tv.tv_sec = 0;
		tv.tv_usec = (RTOTime - (int)difference) * 1000;
		int rv = select(fd + 1, &readfds, NULL, NULL, &tv);
		if (rv == 0)
		{
			goto timeout;
		}

		// Try to get ACKs from client and use select() to timeout on the oldest package
		getPacket(fd, buf, &len, (struct sockaddr *)&clientAddr, &clientAddrLen, &seqNum, &wnd, &ret, &syn, &fin);
		if (fin == 1)
		{
			fprintf(stdout, "Receiving packet %i FIN\n", seqNum);
			break;
		}
		for (i = 0; i < wndSize; i++)
		{
			if (wndSeqs[i] + 1 == seqNum)
			{
				fprintf(stdout, "Receiving packet %i\n", seqNum);
				ACKed[i] = seqNum;
				numACKsRcvd++;
				break;
			}
		}

		// While the first packet in the array has been ACKed
		while (ACKed[0] != -1)
		{
			// Set the new send base
			sendBase = ACKed[0];
			// We must now send one more new packet
			packetsSent--;

			// Shift all the arrays to the left by 1
			for (i = 0; i < wndSize - 1; i++)
			{
				timers[i] = timers[i + 1];
				ACKed[i] = ACKed[i + 1];
				wndSeqs[i] = wndSeqs[i + 1];
				fileLocs[i] = fileLocs[i + 1];
			}

			// And empty out the last entry
			timers[wndSize - 1] = -1;
			ACKed[wndSize - 1] = -1;
			wndSeqs[wndSize - 1] = -1;
			fileLocs[wndSize - 1] = -1;
		}
		
		// Update the currLoc since we got an ack
		currLoc = numACKsRcvd * MAX_PACKET_SIZE;

		// If we're done transferring the file, send a FIN and commence FIN/ACK
		if (currLoc >= fileLen)
		{
			fin = 1;
			sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, sendBase, wnd, 0, 0, fin);
			fprintf(stdout, "Sending packet %i %i FIN\n", seqNum, wnd);
			break;
		}

		timeout:
							
		// Retransmit timed out packages
		msec = 0;
		time(&now);
		difference = 0;

		// For each packet in the window
		for (i = 0; i < wndSize; i++)
		{
			// Check that it's a valid entry and not yet ACKed
			if (wndSeqs[i] != -1 && ACKed[i] == -1)
			{
				// Get the time when the package was sent
				before = timers[i];
				// Find the elapsed time since it was sent
				difference = difftime(now, before) * 1000;		
				printf("Difference = %f\n", difference);

				// If the elapsed time is longer than the RTO time
				if (difference >= RTOTime)
				{
					// Retransmit this package
					seqNum = wndSeqs[i];
					int tempLoc = fileLocs[i];

					// Initialize packetSize as the largest possible packet size
					int packetSize = MAX_PACKET_SIZE;

					// On the last packet, send fewer bytes if needed
					if (fileLen - tempLoc < MAX_PAYLOAD_SIZE)
						packetSize = fileLen - tempLoc + HEADER_SIZE;

					// Make an buffer for the packet we're sending and send it
					char toSend[packetSize];
					bzero(toSend, packetSize);
					memcpy(toSend, fileContents + tempLoc, packetSize);

					// Send the retransmission
					sendPacket(fd, toSend, packetSize, (struct sockaddr *)&clientAddr, clientAddrLen, seqNum, wnd, 1, 0, 0);
					fprintf(stdout, "Sending packet %i %i Retransmission\n", seqNum, wnd);

					// Start the timer again
					time(&timers[i]);

					// Advance the location of the file we need to send
					tempLoc += packetSize - HEADER_SIZE;

				}
			}
		}
	}

	/* TODO:
		Closing TCP Connection: Client sends TCP FIN; Server receives FIN, replies with ACK, closes connection, sends FIN; Client receives ACK+FIN, replies with ACK; Server receives ACK, closes
	*/

	free(wndSeqs);
	free(timers);
	free(ACKed);
	close(fd);
	fclose(fp);
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
	memcpy(wnd, received + intSize*2, intSize);
	memcpy(ret, received + intSize*3, intSize);
	memcpy(syn, received + intSize*4, intSize);
	memcpy(fin, received + intSize*5, intSize);
	
	free(received);

	return result;
}