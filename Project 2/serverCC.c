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
#include <sys/select.h>

const int MAX_PACKET_SIZE = 1024;
int HEADER_SIZE = 24;
int MAX_PAYLOAD_SIZE = 1000;
const int RTOTime = 500;
const int MAX_SEQ_NUM = 30720;

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

	int ssthresh = 15360, cwnd = 1024;
	int FRFR = 0;
	int numDupes = -1;

	int len = 0, seqNum = 0, wnd = 5120, ret = 0, syn = 0, fin = 0;
	unsigned int fileStart = 0;
	int fd;
	int newSeq;

	// How many packets can fit in wnd
	int wndSize;

	// The byte location of the file for the sent packets
	unsigned int * fileLocs;
	// The byte location of the next payload
	unsigned int * fileLocsEnd;
	// The seq numbers of the sent packets
	int * wndSeqs;
	// The expected ACK numbers for the corresponding packet
	int * ACKNums;
	// Whether or not the packet was acked; If ACKed, set to the ACK number
	int * ACKed;
	// The length of the payloads
	int * PLLengths;
	// The RTO  timers for individual packets
	struct timespec * timers;


	// Get the portnumber
	portno = atoi(argv[1]);
	if (portno < 0)
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

	fprintf(stdout, "Awaiting client connection request...\n");


getSYN:
	/*******************************************************************************************************************************/
	// SYN/SYN-ACK handshake
	// Get the SYN and initial sequence number
	recvLen = getPacket(fd, buf, &len, (struct sockaddr *)&clientAddr, &clientAddrLen, &seqNum, &wnd, &syn, &fin, &fileStart);

	// Check if the first message was a SYN message
	if (syn != 1)
	{
		goto getSYN;
	}
	else
		fprintf(stdout, "Client Information: %s:%d\n\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));

	fprintf(stdout, "Receiving packet %i SYN\n", seqNum);

	ret = 0;
sendSYNACK:
	// Send SYN ACK
	sendLen = sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, seqNum, cwnd, 1, 0, 0);

	// If this is not a retransmission
	if (ret == 0)
		fprintf(stdout, "Sending packet %i %i SYN-ACK\n", seqNum, cwnd);
	// If this is a retransmission
	else
		fprintf(stdout, "Sending packet %i %i Retransmission SYN-ACK\n", seqNum, cwnd);


	// Set up a timeout timer to resend FIN ACK if we don't get the file request in time
	fd_set readfds;
	struct timeval tv;
	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);
	tv.tv_sec = 0;
	tv.tv_usec = RTOTime * 1000;
	int rv = select(fd + 1, &readfds, NULL, NULL, &tv);
	// If we time out on getting the file name
	if (rv == 0)
	{
		ret = 1;
		goto sendSYNACK;
	}
	/*******************************************************************************************************************************/

getFileName:
	// Get the name of the requested file
	recvLen = getPacket(fd, buf, &len, (struct sockaddr *)&clientAddr, &clientAddrLen, &seqNum, &wnd, &syn, &fin, &fileStart);
	if (syn == 1 || len == 0)
		goto sendSYNACK;

	fprintf(stdout, "Requested File Name: %s\n\n", buf);

	// Get a file pointer
	char* fileContents;
	FILE *fp = fopen(buf, "r");

	if (fp == NULL)
	{
		sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, 0, cwnd, 0, 1, 0);
		perror("ERROR: File not found\n");
		goto getFIN;
	}

	// Get the file size
	size_t fileSize = 0;
	fseek(fp, 0, SEEK_END);
	fileSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (fileSize == 0)
	{
		sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, 0, cwnd, 0, 1, 0);
		perror("ERROR: File is empty\n");
		goto getFIN;
	}

	// Allocate space for the file + 1 for a null byte terminator
	fileContents = malloc(fileSize + 1);
	if (fileContents == NULL)
	{
		sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, 0, cwnd, 0, 1, 0);
		perror("ERROR: Allocating buffer for file contents\n");
		goto getFIN;
	}

	// Copy the contents of the file into fileContents and null byte terminate it
	size_t fileLen = fread(fileContents, 1, fileSize, fp);
	fileContents[fileLen] = '\0';


	int maxArrLen = 150;

	// Allocate space in the arrays
	wndSeqs = (int*)malloc(sizeof(int) * maxArrLen);
	ACKNums = (int*)malloc(sizeof(int) * maxArrLen);
	ACKed = (int*)malloc(sizeof(int) * maxArrLen);
	fileLocs = (unsigned int*)malloc(sizeof(unsigned int) * maxArrLen);
	PLLengths = (int*)malloc(sizeof(int) * maxArrLen);
	timers = (struct timespec*)malloc(sizeof(struct timespec) * maxArrLen);
	fileLocsEnd = (unsigned int*)malloc(sizeof(unsigned int) * maxArrLen);

	// Set all the ACKs, file locations, and seq numbers to -1
	for (i = 0; i < maxArrLen; i++)
	{
		ACKed[i] = -1;
		wndSeqs[i] = -1;
		fileLocs[i] = 0;
		ACKNums[i] = -1;
		PLLengths[i] = -1;
		fileLocsEnd[i] = 0;
	}

	ret = 0;

	// Number of packets sent in the current window
	int packetsSent = 0;

	// Total number of ACKs received
	int numACKsRcvd = 0;

	// The current window will be the range (sendBase, sendBase + wnd)
	int sendBase = 0;

	// The byte of the file that corresponds to to current sendBase
	// This is different from sendBase because seq includes header bytes (i.e. is incremented by 1024 on each packet), 
	// but fileBase just keeps track of payload bytes (only 1000 bytes of the file are sent at a time)
	int fileBase = 0;

	/*
	FILE* fp2 = fopen("sender.data", "w");

	if (fp2 == NULL)
	{
	printf("ERROR: received.data file creation\n");
	exit(1);
	}
	*/
	// While a fin message isn't sent/received yet...
	while (fin == 0)
	{
		unsigned int fileOffset = fileBase + packetsSent * MAX_PAYLOAD_SIZE;
		

		wndSize = cwnd / MAX_PACKET_SIZE;
		if (wndSize == 0)
			wndSize = 1;

		//printf("DEBUG 1\n");

		// Send the packets in the current window
		while (fileOffset < fileLen && packetsSent < wndSize)
		{
			// Calculate the sequence number of this packet % 30720 (since that is the max sequence number)
			seqNum = (sendBase + packetsSent * MAX_PACKET_SIZE) % MAX_SEQ_NUM;

			// Initialize payloadSize as the largest possible payload size
			int payloadSize = MAX_PAYLOAD_SIZE;

			// On the last packet, send fewer bytes if needed
			if (fileLen - fileOffset < MAX_PAYLOAD_SIZE)
				payloadSize = fileLen - fileOffset;

			// Make an buffer for the packet we're sending and send it
			char toSend[payloadSize];
			bzero(toSend, payloadSize);
			memcpy(toSend, fileContents + fileOffset, payloadSize);
			sendPacket(fd, toSend, payloadSize, (struct sockaddr *)&clientAddr, clientAddrLen, seqNum, cwnd, 0, 0, fileOffset);
			fprintf(stdout, "Sending packet %i %i %i\n", seqNum, cwnd, ssthresh);

			//fseek(fp2, fileOffset, SEEK_SET);
			//fwrite(toSend, sizeof(char), payloadSize, fp2);

			// Start the timer
			clock_gettime(CLOCK_MONOTONIC_RAW, &timers[packetsSent]);
			// Set the packet as not ACKed
			ACKed[packetsSent] = -1;
			// Set the sequence number of this packet
			wndSeqs[packetsSent] = seqNum;
			// Set the expected ACK value for this packet
			ACKNums[packetsSent] = (seqNum + payloadSize + HEADER_SIZE) % MAX_SEQ_NUM;
			// Set the starting byte of the sent file contents
			fileLocs[packetsSent] = fileOffset;
			// Set the length of the payload in the array
			PLLengths[packetsSent] = payloadSize;
			fileLocsEnd[packetsSent] = fileOffset + payloadSize;

			// Increment the number of packets we sent in this current window
			packetsSent++;
			fileOffset = fileBase + packetsSent * MAX_PAYLOAD_SIZE;
		}

		//printf("DEBUG 2\n");

		// Get the time of the oldest packet
		int msec = 0;
		struct timespec before, now;
		clock_gettime(CLOCK_MONOTONIC_RAW, &now);

		int oldestIndex = -1;
		int oldestTime = -1;

		for (i = 0; i < wndSize; i++)
		{
			// Check if the entry is valid and that it hasn't been acked
			if (wndSeqs[i] != -1 && ACKed[i] == -1)
			{
				before = timers[i];
				msec = (now.tv_sec - before.tv_sec) * 1000 + (now.tv_nsec - before.tv_nsec) / 1000000;

				if (msec > oldestTime)
				{
					oldestTime = msec;
					oldestIndex = i;
				}

			}
		}

		//printf("DEBUG 3\n");

		// If any packet is already timed out
		if (oldestTime > RTOTime)
			goto timeout;

		//printf("DEBUG 4\n");

		// Set up a timer based on the oldest packet to break out of getPacket on the timeout
		fd_set readfds;
		struct timeval tv;
		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);
		tv.tv_sec = 0;
		tv.tv_usec = (RTOTime - oldestTime) * 1000;
		int rv = select(fd + 1, &readfds, NULL, NULL, &tv);
		if (rv == 0)
		{
			goto timeout;
		}

		//printf("DEBUG 5\n");

		int retLen, retSeqNum, retWnd, retSyn, retFin;
		// Try to get ACKs from client and use select() to timeout on the oldest package
		getPacket(fd, buf, &retLen, (struct sockaddr *)&clientAddr, &clientAddrLen, &retSeqNum, &retWnd, &retSyn, &retFin, &fileStart);
		fin = retFin;
		seqNum = retSeqNum;
		if (fin == 1)
		{
			ret = 0;
			fprintf(stdout, "Receiving packet %i FIN\n", seqNum);
			goto sendFINACK;
		}
		else
		{
			fprintf(stdout, "Receiving packet %i\n", retSeqNum);
		}
		for (i = 0; i < wndSize; i++)
		{
			if (ACKNums[i] == retSeqNum)
			{
				if (i != 0)
				{
					numDupes++;
					if (numDupes == 3)
					{
						FRFR = 1;
						ssthresh = cwnd / 2;
						cwnd = ssthresh + 3 * MAX_PACKET_SIZE;

						// Retransmit the old packet
						seqNum = wndSeqs[0];
						unsigned int tempLoc = fileLocs[0];

						int payloadSize = PLLengths[0];

						// Make an buffer for the packet we're sending and send it
						char toSend[payloadSize];
						bzero(toSend, payloadSize);
						memcpy(toSend, fileContents + tempLoc, payloadSize);

						// Send the retransmission
						sendPacket(fd, toSend, payloadSize, (struct sockaddr *)&clientAddr, clientAddrLen, seqNum, cwnd, 0, 0, tempLoc);
						fprintf(stdout, "Sending packet %i %i %i Retransmission\n", seqNum, cwnd, ssthresh);

						//fseek(fp2, tempLoc, SEEK_SET);
						//fwrite(toSend, sizeof(char), payloadSize, fp2);
						//printf("DEBUG: %i %i\n", fileOffset, payloadSize);

						// Start the timer again
						clock_gettime(CLOCK_MONOTONIC_RAW, &timers[0]);

					}
					else if (numDupes > 3)
					{
						FRFR = 1;
						cwnd += MAX_PACKET_SIZE;
					}
				}

				// We are in slow start
				if (cwnd < ssthresh)
				{
					cwnd += MAX_PACKET_SIZE;
				}
				else
				{
					cwnd += MAX_PACKET_SIZE * MAX_PACKET_SIZE / cwnd;
				}
				
				ACKed[i] = retSeqNum;
				break;
			}
		}

		//printf("DEBUG 6\n");

		// While the first packet in the array has been ACKed
		while (ACKed[0] != -1)
		{
			numDupes = -1;
			if (FRFR == 1)
			{
				FRFR = 0;
				cwnd = ssthresh;
			}

			// Slides the location of the file we send next
			fileBase = fileLocsEnd[0];

			numACKsRcvd++;
			// Calculate the new base for file contents
			int payloadLen = PLLengths[0];

			// Set the new send base
			sendBase = ACKNums[0];
			// We must now send one more new packet
			packetsSent--;

			// Shift all the arrays to the left by 1
			for (i = 0; i < maxArrLen - 1; i++)
			{
				timers[i] = timers[i + 1];
				ACKed[i] = ACKed[i + 1];
				wndSeqs[i] = wndSeqs[i + 1];
				fileLocs[i] = fileLocs[i + 1];
				ACKNums[i] = ACKNums[i + 1];
				PLLengths[i] = PLLengths[i + 1];
				fileLocsEnd[i] = fileLocsEnd[i + 1];
			}

			// And empty out the last entry
			ACKed[maxArrLen - 1] = -1;
			wndSeqs[maxArrLen - 1] = -1;
			fileLocs[maxArrLen - 1] = 0;
			ACKNums[maxArrLen - 1] = -1;
			PLLengths[maxArrLen - 1] = -1;
			fileLocsEnd[maxArrLen - 1] = 0;
		}

		//printf("DEBUG 7\n");

		// If we're done transferring the file, send a FIN and commence FIN/ACK
		if (fileBase >= fileLen)
		{
			fin = 1;
			sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, sendBase, cwnd, 1, fin, 0);
			fprintf(stdout, "Sending packet %i %i FIN\n", sendBase, cwnd);
			ret = 0;
			goto getFIN;
		}

		//printf("DEBUG 8\n");

	timeout:
		// Retransmit timed out packages
		msec = 0;
		clock_gettime(CLOCK_MONOTONIC_RAW, &now);

		
		//printf("DEBUG 9\n");
		
		// For each packet in the window
		for (i = 0; i < wndSize; i++)
		{
			//printf("DEBUG 9.0: %i %i\n", wndSeqs[i],  ACKed[i]);
			// Check that it's a valid entry and not yet ACKed
			if (wndSeqs[i] != -1 && ACKed[i] == -1)
			{
				//printf("DEBUG 9.1\n");
				// Get the time when the package was sent
				before = timers[i];
				// Find the elapsed time since it was sent
				msec = (now.tv_sec - before.tv_sec) * 1000 + (now.tv_nsec - before.tv_nsec) / 1000000;

				// If the elapsed time is longer than the RTO time
				if (msec >= RTOTime)
				{
					//printf("DEBUG 9.2\n");
					if (cwnd / 2 > 2 * MAX_PACKET_SIZE)
						ssthresh = cwnd / 2;
					else
						ssthresh = 2 * MAX_PACKET_SIZE;

					// Reset cwnd to 1 MSS
					cwnd = MAX_PACKET_SIZE;

					// Retransmit this package
					seqNum = wndSeqs[i];
					unsigned int tempLoc = fileLocs[i];

					int payloadSize = PLLengths[i];

					// Make an buffer for the packet we're sending and send it
					char toSend[payloadSize];
					bzero(toSend, payloadSize);
					memcpy(toSend, fileContents + tempLoc, payloadSize);

					// Send the retransmission
					sendPacket(fd, toSend, payloadSize, (struct sockaddr *)&clientAddr, clientAddrLen, seqNum, cwnd, 0, 0, tempLoc);
					fprintf(stdout, "Sending packet %i %i %i Retransmission\n", seqNum, cwnd, ssthresh);

					//fseek(fp2, tempLoc, SEEK_SET);
					//fwrite(toSend, sizeof(char), payloadSize, fp2);
					////printf("DEBUG: %i %i\n", fileOffset, payloadSize);

					// Start the timer again
					clock_gettime(CLOCK_MONOTONIC_RAW, &timers[i]);

				}
			}
		}

		//printf("DEBUG 10\n");
	}


	/******************************************************************************************************************************************************************************************/
	// FIN/FINACK Handshake
	//Closing TCP Connection: Client sends Server a FIN; Server receives FIN, replies with ACK and then replies with FIN; Client receives ACK+FIN, replies with ACK; Server receives ACK, closes

sendFIN:
	sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, sendBase, cwnd, 1, 1, 0);
	if (ret == 0)
		fprintf(stdout, "Sending packet %i %i FIN\n", sendBase, cwnd);
	else
		fprintf(stdout, "Sending packet %i %i Retransmission FIN\n", sendBase, cwnd);

getFIN:
	// Set up a timeout timer to resend FIN if needed (in case we timeout before we get the FIN)
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

	getPacket(fd, buf, &len, (struct sockaddr *)&clientAddr, &clientAddrLen, &seqNum, &wnd, &syn, &fin, &fileStart);
	if (fin == 1 && seqNum == (sendBase + HEADER_SIZE) % MAX_SEQ_NUM)
	{
		fprintf(stdout, "Receiving packet %i FIN\n", seqNum);
		ret = 0;
		goto sendFINACK;
	}
	else
	{
		ret = 1;
		goto sendFIN;
	}

	ret = 0;

sendFINACK:
	newSeq = (sendBase + HEADER_SIZE) % MAX_SEQ_NUM;
	// Now that the client sent the FIN, we send the FIN ACK to the client's FIN
	sendPacket(fd, buf, 0, (struct sockaddr *)&clientAddr, clientAddrLen, newSeq, cwnd, 0, 1, 0);
	// If it's not a retransmission
	if (ret == 0)
		fprintf(stdout, "Sending packet %i %i FIN-ACK\n", newSeq, cwnd);
	// If it's a retransmission
	else
		fprintf(stdout, "Sending packet %i %i Retransmission FIN-ACK\n", newSeq, cwnd);

	// Set up a timeout timer to resend FIN-ACK if needed (in case we timeout before we get the last ACK)
	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);
	tv.tv_sec = 0;
	tv.tv_usec = RTOTime * 1000;
	rv = select(fd + 1, &readfds, NULL, NULL, &tv);
	if (rv == 0)
	{
		ret = 1;
		goto sendFINACK;
	}

getLastACK:
	// Now we get the last ACK and we are done
	getPacket(fd, buf, &len, (struct sockaddr *)&clientAddr, &clientAddrLen, &seqNum, &wnd, &syn, &fin, &fileStart);
	if (seqNum != (newSeq + HEADER_SIZE) % MAX_SEQ_NUM)
		goto sendFINACK;

	fprintf(stdout, "Receiving packet %i\n", seqNum);
	fprintf(stdout, "Closing the connection with the client...\n");
	/******************************************************************************************************************************************************************************************/

	fprintf(stdout, "\n\nAwaiting new client connection request...\n");

	fclose(fp);
	//fclose(fp2);

	goto getSYN;

	free(fileContents);
	free(wndSeqs);
	free(timers);
	free(ACKed);

	free(ACKNums);
	free(PLLengths);
	free(fileLocsEnd);
	free(fileLocs);

	close(fd);
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
		free(toSend);
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