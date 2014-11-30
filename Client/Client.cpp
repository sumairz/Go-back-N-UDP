/*
Name- Sumair (7099347) & Mandeep Singh (7163738)

*/

#pragma comment(lib,"wsock32.lib")
#define _CRT_SECURE_NO_DEPRECATE
#define _CRT_SECURE_NO_WARNINGS
#include <winsock.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <windows.h>
#include <time.h>
#include "client.h"
#include "Timer.h"

using namespace std;
//int WINDOW_SIZE;

bool FileExists(char * filename)
{
	int result;
	struct _stat stat_buf;
	result = _stat(filename, &stat_buf);
	return (result == 0);
}

long GetFileSize(char * filename)
{
	int result;
	struct _stat stat_buf;
	result = _stat(filename, &stat_buf);
	if (result != 0) return 0; 
	return stat_buf.st_size;
}

bool UdpClient::SendFile(int sock, char * filename, char * sending_hostname, int server_number, int WINDOW_SIZE)
{
	if (TRACE) fout << "Sender started on host " << sending_hostname << std::endl;
	           std::cout << "Sender started on host " << sending_hostname << std::endl;
	
	Timer * timer = new Timer(); Frame frame; FrameResponse response;
	int sequence_number = server_number % SEQUENCE_WIDTH; // expected sequence start
	int packets_sent=0, packets_received=0, revised_sequence_number, i, j, last_ack, last_nak, counter_last_frame_sent = 0;
	long file_size, bytes_to_read=0, bytes_read=0, total_bytes_read=0, bytes_offset=0;
	bool bLastOutgoingFrame = false, bFirstPacket = true, bAdd = false, bFound;
	bool bAtLeastOneResponse, bLastFrameAcked, bGoBackInWindow = false, bTimedOut = false;
	int sequence_history[20]; fpos_t history_stream_pos[20];
	int sequence_ubound = 2*WINDOW_SIZE + 1;
	/* Open file stream in read-only binary mode */
	FILE * stream = fopen(filename, "r+b"); rewind(stream);
	if (stream != NULL)
	{
		file_size = GetFileSize(filename);
		/* Send packets */
		while (1)
		{ 
			/* Send each frame in the window */
		for (i = 0 ; i < WINDOW_SIZE ; i++)
			{
				/* Increase sequence number for next dispatch */
				if ( !bFirstPacket ) sequence_number = (sequence_number+1) % (sequence_ubound+1);

				if (bGoBackInWindow) /* We will need to re-read from file and re-send packets */
				{
					/* Decrease the sequence number according to go_back_n variable */
					sequence_number = revised_sequence_number;
					bGoBackInWindow = false;

					/* Go back in the history and set the correct file position */
					for ( j = 0 ; j < WINDOW_SIZE ; j++)
						if (sequence_history[j] == sequence_number)
						{
							fsetpos(stream, &history_stream_pos[j]);
							break;
						}
				}
				bLastOutgoingFrame = ((file_size - ftell(stream)) <= FRAME_BUFFER_SIZE);
				bytes_to_read = (bLastOutgoingFrame ? (file_size - ftell(stream)) : FRAME_BUFFER_SIZE);

				/* Keep a history of sequence numbers used in the window */
				sequence_history[i] = sequence_number;
				fgetpos(stream, &history_stream_pos[i]); // save indicator of file right before reading
				
				/* Read file into frame buffer */
				bytes_read = fread(frame.buffer, sizeof(char), bytes_to_read, stream);
				total_bytes_read += bytes_read;
				
				/* Set frame parameters */
				frame.buffer_length = bytes_read;
				frame.sequence = sequence_number;	// set the frame sequence bits
				frame.last = bLastOutgoingFrame;

				/* Place the frame in the send packet buffer and send it off */
				memcpy(send_packet.buffer, &frame, sizeof(frame));
				send_packet.type = FRAME;
				send_packet.buffer_length = sizeof(frame);
				if ( SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet) ) { return false; }
			
				/* Start timer after first frame in window is sent */
				if (i == 0)
					timer->SetInterval(OLDEST_FRAME_TIMER);
	
				/* Keep track of statistics and log */
				packets_sent++;
				if (TRACE) fout << "Sender: sent frame " << sequence_number << " (bytes offset " << ftell(stream) << ")" << std::endl;
				           std::cout << "Sender: sent frame " << sequence_number << " (bytes offset " << ftell(stream) << ")" << std::endl;
	
				bFirstPacket = false;
				if (bLastOutgoingFrame)
					break;
			}

			bAtLeastOneResponse = false; // keep track of whether we've received at least one response
			bLastFrameAcked = false; // keep track of whether the last frame has been acked
			bTimedOut = false;
			/* Receive responses */
			for (i = 0 ; i < WINDOW_SIZE ; i++)
			{
				while( ReceivePacket(sock, &recv_packet) != FRAME_RESPONSE )
				{
					if (timer->TimedOut())
					{
						if (TRACE) fout << "Sender: timed out!" << std::endl;
						           std::cout << "Sender: timed out!" << std::endl;
						bTimedOut = true; bGoBackInWindow = true;
						break;
					}
				}
				if (bTimedOut) break;
				packets_received++;
				bAtLeastOneResponse = true;
				
				/* Copy the recv_packet's buffer to the response */
				memcpy(&response, recv_packet.buffer, sizeof(response));
				if ( response.type == ACK )
				{
					if (TRACE) fout << "Sender: received ACK " << response.number << std::endl;
						       std::cout << "Sender: received ACK " << response.number << std::endl;
					if (response.number == sequence_number)
					{
						bLastFrameAcked = true; bGoBackInWindow = false;
						break;
					}
					else
						last_ack = response.number;
				}
				else if ( response.type == NAK )
				{
					if (TRACE) fout << "Sender: received NAK " << response.number << std::endl;
						       std::cout << "Sender: received NAK " << response.number << std::endl;
					bGoBackInWindow = true;
					last_nak = response.number;
					if (response.number == sequence_number)
						break;
				}
			}
			
			if ( bGoBackInWindow ) 
			{
				if (bAtLeastOneResponse) // decide from where to restart
				{
					bFound = false;
					for (j = WINDOW_SIZE-1 ; j >= 0 ; j--)
					{
						if (sequence_history[j] == last_ack) // Has it been ACK'd ?
						{ 
							revised_sequence_number = (last_ack + 1) % (sequence_ubound + 1);
							last_ack = -1; bFound = true; break;
						}
						else if (sequence_history[j] == last_nak) // Has it been NAK'd?
						{
							revised_sequence_number = last_nak;
							last_nak = -1; bFound = true; break;
						}
					}
					if (!bFound)
					{
						revised_sequence_number = last_nak;
					}
				}
				else // go back to the beginning of the window
				{
					revised_sequence_number = sequence_history[0];
				}
			}

			/* Check for exit */
			if (bLastOutgoingFrame && ( bLastFrameAcked || counter_last_frame_sent > MAX_RETRIES ))
				break;
		}
		
		
		// finishing
		fclose( stream );
		std::cout << "Sender: file transfer complete" << std::endl;
		std::cout << "Sender: number of packets sent     : " << packets_sent << std::endl;
		std::cout << "Sender: number of packets received : " << packets_received << std::endl;
		std::cout << "Sender: number of bytes read       : " << total_bytes_read << std::endl << std::endl;
		if (TRACE)
		{ 
			fout << "Sender: file transfer complete" << std::endl;
			fout << "Sender: number of packets sent     : " << packets_sent << std::endl;
			fout << "Sender: number of packets received : " << packets_received << std::endl;
			fout << "Sender: number of bytes read       : " << total_bytes_read << std::endl << std::endl;
		}
		return true;
	}
	else
	{
		if (TRACE) fout << "Sender: problem opening the file." << std::endl;
		           std::cout << "Sender: problem opening the file." << std::endl;
		return false;		
	}
}

bool UdpClient::ReceiveFile(int sock, char * filename, char * receiving_hostname, int client_number, int WINDOW_SIZE)
{
	if (TRACE) fout << "Receiver started on host " << receiving_hostname << std::endl;
	           std::cout << "Receiver started on host " << receiving_hostname << std::endl;

	Frame frame;
	FrameResponse response;

	long byte_count=0;
	int packets_received=0, packets_sent=0, bytes_written=0, bytes_written_total=0;
	int i, sequence_number = client_number % SEQUENCE_WIDTH; // expected sequence start

	int sequence_ubound = 2*WINDOW_SIZE + 1;

	/* Open file stream in writable binary mode */
	FILE * stream = fopen(filename, "w+b"); rewind(stream);
	
	if (stream != NULL)
	{
		/* Receive packets */
		while (1)
		{
			/* Block until a packet comes in */
			while ( ReceivePacket(sock, &recv_packet) == TIMEOUT ) {;}
			packets_received++;
			
			if (recv_packet.type == HANDSHAKE) // Send last handshake again, server didnt receive properly
			{
				/* Copy the recv_packet's buffer to the handshake */
				memcpy(&handshake, recv_packet.buffer, sizeof(frame));

				if (handshake.state == SERVER_ACKS)
				{
					if (TRACE) fout << "Receiver: received handshake 2 (C" << client_number << " S" << handshake.server_number << ")" << std::endl;
							   std::cout << "Receiver: received handshake 2 (C" << client_number << " S" << handshake.server_number << ")" << std::endl;

					/* Place handshake in send_packet's buffer and send it out */
					handshake.state = CLIENT_ACKS;
					send_packet.type = HANDSHAKE;
					send_packet.buffer_length = sizeof(handshake);
					memcpy(send_packet.buffer, &handshake, sizeof(handshake));
					if ( SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet) ) { err_sys("Sending packet error!"); }
					
					if (TRACE) fout << "Receiver: sent handshake 3 (C" << client_number << " S" << handshake.server_number << ")" << std::endl;
							   std::cout << "Receiver: sent handshake 3 (C" << client_number << " S" << handshake.server_number << ")" << std::endl;
					packets_sent++;
				}
			}
			else if (recv_packet.type == FRAME)
			{
				/* Copy the recv_packet's buffer to the frame */
				memcpy(&frame, recv_packet.buffer, sizeof(frame));

				if (TRACE) fout << "Receiver: received frame " << (int)frame.sequence;
				           std::cout << "Receiver: received frame " << (int)frame.sequence;
				
				if ( (int)frame.sequence == sequence_number )
				{
					/* Prepare ACK, place it in send_packet's buffer and send it out */
					response.type = ACK;
					response.number = sequence_number;
					send_packet.type = FRAME_RESPONSE;
					send_packet.buffer_length = sizeof(response);
					memcpy(send_packet.buffer, &response, sizeof(response));
					if ( SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet) ) { err_sys("Sending packet error!"); }

					/* Write frame buffer to file stream */
					byte_count = frame.buffer_length;
					bytes_written = fwrite(frame.buffer, sizeof(char), byte_count, stream );
					bytes_written_total += bytes_written;

					if (TRACE) fout << " ... sent ACK " << response.number << " (bytes written " << bytes_written_total << ")" << std::endl;
					           std::cout << " ... sent ACK " << response.number << " (bytes written " << bytes_written_total << ")" << std::endl;
					packets_sent++;

					/* Next expected sequence number */
					sequence_number = (sequence_number+1) % (sequence_ubound+1);

					/* Exit loop if last frame */
					if ( frame.last )
					{
						/* Send the last ACK MAX_RETRIES times to ensure it's received */
						for ( i = 0 ; i < MAX_RETRIES ; i++)
						{
							if ( SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet) ) { err_sys("Sending packet error!"); }
							if (TRACE) fout << "Receiver: sent ACK " << response.number << " (final)" << std::endl;
					                   std::cout << "Receiver: sent ACK " << response.number << " (final)" << std::endl;
							packets_sent++;
						}
						break;
					}
				}
				else
				{
					/* Prepare NAK, place it in send_packet's buffer and send it out */
					response.type = NAK;
					response.number = sequence_number;
					send_packet.type = FRAME_RESPONSE;
					send_packet.buffer_length = sizeof(response);
					memcpy(send_packet.buffer, &response, sizeof(response));
					
					if ( SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet) ) { err_sys("Sending packet error!"); }

					if (TRACE) fout << " ... sent NAK " << response.number << std::endl;
					           std::cout << " ... sent NAK " << response.number << std::endl;
					packets_sent++;
				}
			}
		}
		

		// finishing
		fclose( stream );
		std::cout << "Receiver: file transfer complete!" << std::endl;
		std::cout << "Receiver: number of packets received : " << packets_received << std::endl;
		std::cout << "Receiver: number of packets sent     : " << packets_sent << std::endl;
		std::cout << "Receiver: number of bytes written    : " << bytes_written_total << std::endl << std::endl;
		if (TRACE)
		{ 
			fout << "Receiver: file transfer complete!" << std::endl;
			fout << "Receiver: number of packets received : " << packets_received << std::endl;
			fout << "Receiver: number of packets sent     : " << packets_sent << std::endl;
			fout << "Receiver: number of bytes written    : " << bytes_written_total << std::endl << std::endl;
		}
		return true;
	}
	else
	{
		std::cout << "Receiver: problem opening the file." << std::endl;
        if (TRACE) { fout << "Receiver: problem opening the file." << std::endl; }
		return false;
	}
}

int UdpClient::SendPacket(int sock, Packet * ptr_packet, struct sockaddr_in * sa_in) // fills sa_in struct
{
	return sendto(sock, (const char *)ptr_packet, sizeof(*ptr_packet), 0, (struct sockaddr *)sa_in, sizeof(*sa_in));
}

PacketType UdpClient::ReceivePacket(int sock, Packet * ptr_packet)
{
	fd_set readfds;			// fd_set is a type
	FD_ZERO(&readfds);		// initialize
	FD_SET(sock, &readfds);	// put the socket in the set

	int bytes_recvd;
	int outfds = select(1 , &readfds, NULL, NULL, &timeouts);
	
	switch (outfds)
	{
		case 0:
			return TIMEOUT; break;
		case 1:
			bytes_recvd = recvfrom(sock, (char *)ptr_packet, sizeof(*ptr_packet),0, (struct sockaddr*)&sa_in, &sa_in_size);
			return ptr_packet->type; break;
		default:
			err_sys("select() error!");
	}
}


bool UdpClient::listFiles(int sock, int ServerNum)
{	
	Frame frame;
	FrameResponse response;
	int bytes_recvd;

	/* Place the frame in the send packet buffer and send it off */
	memcpy(send_packet.buffer, "LIST", sizeof("LIST"));
	send_packet.type = FRAME;
	send_packet.buffer_length = sizeof("LIST");
	SendPacket(sock, &send_packet, &sa_in);
	bytes_recvd = recvfrom(sock, (char *)&recv_packet, sizeof(*&recv_packet),0, (struct sockaddr*)&sa_in, &sa_in_size);
	cout << " -------------------------- " << endl;
	std::cout << recv_packet.buffer << endl;
	cout << " -------------------------- " << endl;
	return true;

}

void UdpClient::run()
{
	char server[INPUT_LENGTH]; char filename[INPUT_LENGTH]; char direction[INPUT_LENGTH]; char wind_size[INPUT_LENGTH];
	char hostname[HOSTNAME_LENGTH]; char username[USERNAME_LENGTH]; char remotehost[HOSTNAME_LENGTH];
	unsigned long filename_length = (unsigned long)	FILENAME_LENGTH;
	bool bInputDetailsValid; bool bContinue;

	/* Initialize winsocket */
	if (WSAStartup(0x0202,&wsadata) != 0)
	{  
		WSACleanup();
	    err_sys("Error in starting WSAStartup()\n");
	}

	/* Get username of client */
	if ( !GetUserName(username, &filename_length) )
		err_sys("Cannot get the user name");

	/* Get hostname of client */
	if ( gethostname(hostname, (int)HOSTNAME_LENGTH) != 0 ) 
		err_sys("Cannot get the host name");

	std::cout<<"Enter Window Size: "; std::cin >> WINDOW_SIZE;

	if (TRACE) fout << "WINDOW SIZE: " << WINDOW_SIZE << std::endl << std::endl;
	           std::cout << "WINDOW SIZE: " << WINDOW_SIZE << std::endl << std::endl ;

	
	/* Loop until username inputs "quit" or "exit" as servername */
	std::cout << "Enter server name : "; std::cin >> server;
	while ( strcmp(server, "quit") != 0 && strcmp(server, "exit") != 0)
	{
		bInputDetailsValid = true;
		bContinue = true;

		/* Create a datagram/UDP socket */
		if ( (sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0 ) { err_sys("UDP socket creation error!"); }
		
		/* Client network information */
		memset(&sa, 0, sizeof(sa));				// zero out structure
		sa.sin_family = AF_INET;				// internet address family
		sa.sin_addr.s_addr = htonl(INADDR_ANY); // any incoming interface
		sa.sin_port = htons(CLIENT_PORT);		// set the port
				
		/* Bind client socket to port 5000 */
		if (bind( sock, (LPSOCKADDR)&sa, sizeof(sa) ) < 0) { err_sys("Socket binding error!"); }

		srand((unsigned)time(NULL));
		client_number = rand() % MAX_RANDOM; // [0..255]

		/* User input*/
		std::cout << "Enter router host : "; std::cin >> remotehost;
		std::cout << "Enter get to recieve file   : " << endl;
		cout<< "Enter put to send file   : "<<endl;
		cout<< "Enter list to show the server files   : "; std::cin >> direction;
		

		/* Copy user-input into handshake */
		strcpy(handshake.hostname, hostname);
		strcpy(handshake.username, username);
		
		handshake.win_size=WINDOW_SIZE;
		
		/* File transfer direction */
		if ( strcmp(direction, "get") != 0 && strcmp(direction, "put") != 0 && strcmp(direction, "list") != 0 && strcmp(direction, "rename") != 0)
		{
			std::cout << "Invalid direction. Use \"get\" or \"put\"." << std::endl;
			bInputDetailsValid = false;
		}
		else if ( strcmp(direction, "get") == 0 )
		{
			std::cout << "Enter file name   : "; std::cin >> filename; std::cout << std::endl;
			strcpy(handshake.filename, filename);
			handshake.direction = GET;
		}
		else if ( strcmp(direction, "put") == 0 )
		{
			std::cout << "Enter file name   : "; std::cin >> filename; std::cout << std::endl;
			strcpy(handshake.filename, filename);
			if ( !FileExists(handshake.filename) )
			{
				std::cout << "File \"" << handshake.filename << "\" does not exist on client side." << std::endl;
				bInputDetailsValid = false;
			}
			else
				handshake.direction = PUT;
		}
		else if(strcmp(direction, "list")==0)
		{
			handshake.direction=LIST;
		}

		if (bInputDetailsValid)
		{
			/* Router network information */
			struct hostent * rp; // structure containing router
			rp = gethostbyname(remotehost);
			memset(&sa_in, 0, sizeof(sa_in) );
			memcpy(&sa_in.sin_addr, rp->h_addr, rp->h_length); // fill sa_in with rp info
			sa_in.sin_family = rp->h_addrtype;
			sa_in.sin_port = htons(REMOTE_PORT);
			sa_in_size = sizeof(sa_in);

			handshake.client_number = client_number;
			handshake.state = CLIENT_REQ;

			/* Place handshake in send_packet's buffer */
			send_packet.type = HANDSHAKE;
			send_packet.buffer_length = sizeof(handshake);
			memcpy(send_packet.buffer, &handshake, sizeof(handshake));

			/* Initiate handshaking protocol */
			do
			{
				if ( SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet) ) { err_sys("Sending packet error!"); }
				
				if (TRACE) fout << "Client: sent handshake 1 (C" << client_number << ")" << std::endl;
				           std::cout << "Client: sent handshake 1 (C" << client_number << ")" << std::endl;
				
				if (ReceivePacket(sock, &recv_packet) == HANDSHAKE)
				{
					/* Copy the received packet's buffer back in handshake */
					memcpy(&handshake, recv_packet.buffer, sizeof(handshake));

					/* Check how the server responds */	
					if (handshake.state == SERVER_ACKS && handshake.client_number == client_number)
					{
						server_number = handshake.server_number;
						bContinue = true; break;
					}
					else if (handshake.state == FILE_NOT_EXIST)
					{
						if (TRACE) fout << "Client: requested file does not exist!" << std::endl;
								   std::cout << "Client: requested file does not exist!" << std::endl;
						bContinue = false; break;
					}
					else if (handshake.state == INVALID)
					{
						if (TRACE) fout << "Client: invalid request." << std::endl;
								   std::cout << "Client: invalid request." << std::endl;
						bContinue = false; break;
					}
					
				}
			} while (1);

			if (bContinue)
			{
				if (TRACE) fout << "Client: received handshake 2 (C" << client_number << " S" << server_number << ")" << std::endl;
				           std::cout << "Client: received handshake 2 (C" << client_number << " S" << server_number << ")" << std::endl;
				
				// Third shake. Acknowledge server's number by sending back the handshake
				handshake.state = CLIENT_ACKS;

				/* Place handshake in send_packet's buffer and send it out */
				send_packet.type = HANDSHAKE;
				send_packet.buffer_length = sizeof(handshake);
				memcpy(send_packet.buffer, &handshake, sizeof(handshake));
				
				for (int k = 0 ; k < MAX_RETRIES ; k++)
					if ( SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet) ) { err_sys("Error in sending packet."); }

				if (TRACE) fout << "Client: sent handshake 3 (C" << client_number << " S" << server_number << ")" << std::endl;
				           std::cout << "Client: sent handshake 3 (C" << client_number << " S" << server_number << ")" << std::endl;

				switch (handshake.direction)
				{
					case GET: // Client is receiving host, server will send client seq
						if ( ! ReceiveFile(sock, handshake.filename, hostname, client_number, WINDOW_SIZE) )
							err_sys("An error occurred while receiving the file.");
						break;
					case PUT: // Client is sending host, server expects seq
						if ( ! SendFile(sock, handshake.filename, hostname, server_number, WINDOW_SIZE) )
							err_sys("An error occurred while sending the file.");
						break;
					case LIST:
						listFiles(sock, server_number);
						break;
					default:
						break;
				}
			}
		}
		             
		if (TRACE) fout << "Closing client socket." << std::endl;
		           std::cout << "Closing client socket." << std::endl;

	closesocket(sock);
	std::cout  << std::endl << "Enter server name : "; std::cin >> server;		// prompt user for server name
		//ContinueFunction();
	}
    
}

void UdpClient::err_sys(char * fmt,...)
{ // from Richard Stevens's source code
	perror(NULL);
	va_list args;
	va_start(args,fmt);
	fprintf(stderr,"error: ");
	vfprintf(stderr,fmt,args);
	fprintf(stderr,"\n");
	va_end(args);
	printf(("Press Enter to exit.\n")); getchar();
	exit(1);
}

unsigned long UdpClient::ResolveName(char name[])
{
	struct hostent *host; // Structure containing host information
	if ((host = gethostbyname(name)) == NULL)
		err_sys("gethostbyname() failed");
	return *((unsigned long *) host->h_addr_list[0]); // return the binary, network byte ordered address
}

UdpClient::UdpClient(char * fn) // constructor
{
	/* For timeout timer */
	timeouts.tv_sec = STIMER;
	timeouts.tv_usec = UTIMER;
	
	/* Set the window size and sequence upperbound */
	//sequence_ubound = 2*WINDOW_SIZE - 1;

	/* Open the log file */
	fout.open(fn);
} 

UdpClient::~UdpClient() // destructor
{
	/* Close the log file */	
	fout.close();

	/* Uninstall winsock.dll */
	WSACleanup();
}

int main(int argc, char *argv[])
{
	UdpClient * client = new UdpClient();
	client->run();
	return 0;
}

void UdpClient::ContinueFunction(){
    int cont;
    cout << "Enter '1' to continue and '2' to exit : " << flush ;  
    cin >> cont;
    if(cont==1){
        main(0,0);
    }
    else if(cont==2){
        exit(0);
    }
    else{
        cout << "Invalid entry please enter again" << endl;
        ContinueFunction();
    }
}