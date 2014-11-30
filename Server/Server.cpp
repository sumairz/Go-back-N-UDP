/*
Name- Sumair (7099347) & Mandeep Singh (7163738)

*/


/* Server */

#pragma comment(lib,"wsock32.lib")
#define _CRT_SECURE_NO_DEPRECATE
#define _CRT_SECURE_NO_WARNINGS
#include <winsock.h>
#include <iostream>
#include <fstream>
#include <windows.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <process.h>
#include "server.h"
#include "Timer.h"
#include <vector>
#include <string>
#include <direct.h>

using namespace std;

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

bool UdpServer::SendFile(int sock, char * filename, char * sending_hostname, int client_number, int WINDOW_SIZE)
{	
	if (TRACE) fout << "Sender started on host " << sending_hostname << endl;
	           cout << "Sender started on host " << sending_hostname << endl;
	
	Timer * timer = new Timer(); Frame frame; FrameResponse response;
	int sequence_number = client_number % SEQUENCE_WIDTH; // expected sequence start
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
				if (TRACE) fout << "Sender: sent frame " << sequence_number << " (bytes offset " << ftell(stream) << ")" << endl;
				           cout << "Sender: sent frame " << sequence_number << " (bytes offset " << ftell(stream) << ")" << endl;
	
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
						if (TRACE) fout << "Sender: timed out!" << endl;
						           cout << "Sender: timed out!" << endl;
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
					if (TRACE) fout << "Sender: received ACK " << response.number << endl;
						       cout << "Sender: received ACK " << response.number << endl;
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
					if (TRACE) fout << "Sender: received NAK " << response.number << endl;
						       cout << "Sender: received NAK " << response.number << endl;
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
		cout << "Sender: file transfer complete" << endl;
		cout << "Sender: number of packets sent     : " << packets_sent << endl;
		cout << "Sender: number of packets received : " << packets_received << endl;
		cout << "Sender: number of bytes read       : " << total_bytes_read << endl << endl;
		if (TRACE)
		{ 
			fout << "Sender: file transfer complete" << endl;
			fout << "Sender: number of packets sent     : " << packets_sent << endl;
			fout << "Sender: number of packets received : " << packets_received << endl;
			fout << "Sender: number of bytes read       : " << total_bytes_read << endl << endl;
		}
		return true;
	}
	else
	{
		if (TRACE) fout << "Sender: problem opening the file." << endl;
		           cout << "Sender: problem opening the file." << endl;
		return false;		
	}
}

bool UdpServer::ReceiveFile(int sock, char * filename, char * receiving_hostname, int server_number, int WINDOW_SIZE)
{
	if (TRACE) fout << "Receiver started on host " << receiving_hostname << endl;
	           cout << "Receiver started on host " << receiving_hostname << endl;

	Frame frame;
	FrameResponse response;

	long byte_count=0;
	int packets_received=0, packets_sent=0, bytes_written=0, bytes_written_total=0;
	int i, sequence_number = server_number % SEQUENCE_WIDTH; // expected sequence start

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
				//cout<<"WINDOW_SIZ from sender : "<<handshake.win_size;
				if (handshake.state == SERVER_ACKS)
				{
					if (TRACE) fout << "Receiver: received handshake 2 (C" << client_number << " S" << handshake.server_number << ")" << endl;
							   cout << "Receiver: received handshake 2 (C" << client_number << " S" << handshake.server_number << ")" << endl;

					/* Place handshake in send_packet's buffer and send it out */
					handshake.state = CLIENT_ACKS;
					send_packet.type = HANDSHAKE;
					send_packet.buffer_length = sizeof(handshake);
					memcpy(send_packet.buffer, &handshake, sizeof(handshake));
					if ( SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet) ) { err_sys("Sending packet error!"); }
					
					if (TRACE) fout << "Receiver: sent handshake 3 (C" << client_number << " S" << handshake.server_number << ")" << endl;
							   cout << "Receiver: sent handshake 3 (C" << client_number << " S" << handshake.server_number << ")" << endl;
					packets_sent++;
				}
			}
			else if (recv_packet.type == FRAME)
			{
				/* Copy the recv_packet's buffer to the frame */
				memcpy(&frame, recv_packet.buffer, sizeof(frame));

				if (TRACE) fout << "Receiver: received frame " << (int)frame.sequence;
				           cout << "Receiver: received frame " << (int)frame.sequence;
				
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

					if (TRACE) fout << " ... sent ACK " << response.number << " (bytes written " << bytes_written_total << ")" << endl;
					           cout << " ... sent ACK " << response.number << " (bytes written " << bytes_written_total << ")" << endl;
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
							if (TRACE) fout << "Receiver: sent ACK " << response.number << " (final)" << endl;
					                   cout << "Receiver: sent ACK " << response.number << " (final)" << endl;
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

					if (TRACE) fout << " ... sent NAK " << response.number << endl;
					           cout << " ... sent NAK " << response.number << endl;
					packets_sent++;
				}
			}
		}
		
		// finishing
		fclose( stream );
		cout << "Receiver: file transfer complete!" << endl;
		cout << "Receiver: number of packets received : " << packets_received << endl;
		cout << "Receiver: number of packets sent     : " << packets_sent << endl;
		cout << "Receiver: number of bytes written    : " << bytes_written_total << endl << endl;
		if (TRACE)
		{ 
			fout << "Receiver: file transfer complete!" << endl;
			fout << "Receiver: number of packets received : " << packets_received << endl;
			fout << "Receiver: number of packets sent     : " << packets_sent << endl;
			fout << "Receiver: number of bytes written    : " << bytes_written_total << endl << endl;
		}
		return true;
	}
	else
	{
		cout << "Receiver: problem opening the file." << endl;
        if (TRACE) { fout << "Receiver: problem opening the file." << endl; }
		return false;
	}
}


int UdpServer::SendPacket(int sock, Packet * ptr_packet, struct sockaddr_in * sa_in) // fills sa_in struct
{ 
	return sendto(sock, (const char *)ptr_packet, sizeof(*ptr_packet), 0, (struct sockaddr *)sa_in, sizeof(*sa_in));
}

PacketType UdpServer::ReceivePacket(int sock, Packet * ptr_packet)
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

vector<string> get_all_files_within_folder(string folder)
{
    vector<string> names;
    char search_path[200];
    sprintf_s(search_path, "%s*.", folder.c_str());
    WIN32_FIND_DATA fd; 
    HANDLE hFind = ::FindFirstFile(".\\*", &fd); 
    if(hFind != INVALID_HANDLE_VALUE) 
    { 
        do 
        { 
            // read all (real) files in current folder, delete '!' read other 2 default folder . and ..
            if(! (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ) 
            {
                names.push_back(fd.cFileName);
            }
        }while(::FindNextFile(hFind, &fd)); 
        ::FindClose(hFind); 
    } 
	return names;
}

bool UdpServer::listFiles(int sock, int ServerNum)
{
	char * dir = _getcwd(NULL, 0);
	string p;
	char getval[1];

	for(int i=0;dir[i] != 0;i++)
	{
		p += dir[i];
	}
	
	string c;
		
	vector<string> names_got = get_all_files_within_folder(p);

	for (vector<string>::iterator n = names_got.begin();n != names_got.end();++n)
	{
		c = c + *n + "\n";
	}
	
	char *cstr = &c[0];	
	
	sprintf(send_packet.buffer, cstr);
	send_packet.type = FRAME;
	send_packet.buffer_length = sizeof(cstr);
	SendPacket(sock, &send_packet, &sa_in);
	
	return true;

}


void UdpServer::run()
{
	bool bContinue;

	/* Initialize winsocket */
	if (WSAStartup(0x0202,&wsadata) != 0)
	{  
		WSACleanup();  
	    err_sys("Error in starting WSAStartup()\n");
	}
	
	/* Get hostname of server */
	if(gethostname(server_name, HOSTNAME_LENGTH)!=0)
		err_sys("Server gethostname() error.");
	
	//if (TRACE) fout << "WINDOW SIZE: " << WINDOW_SIZE << endl << endl;
	    //       cout << "WINDOW SIZE: " << WINDOW_SIZE << endl << endl ;

	printf("=========== ftpd_server v0.3 ===========\n");
	printf("Server started on host [%s]\n", server_name);
	printf("Awaiting request for file transfer...\n", server_name);
	printf("========================================\n\n");


	/* Create a datagram/UDP socket */
	if ( (sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0 )
		err_sys("socket() failed");
	
	/* Fill in server network information */
	memset(&sa, 0, sizeof(sa));				// Zero out structure
	sa.sin_family = AF_INET;                // Internet address family
	sa.sin_addr.s_addr = htonl(INADDR_ANY); // Any incoming interface
	sa.sin_port = htons(SERVER_PORT);       // local server port (5001)
	sa_in_size = sizeof(sa_in);

	/* Bind server socket to port 5001*/
	if (bind(sock, (LPSOCKADDR)&sa, sizeof(sa) ) < 0)
		err_sys("Socket binding error");
	
	/* Wait until a handshake (of type CLIENT_REQ) comes in */
	while (1)
	{
		if (ReceivePacket(sock, &recv_packet) == HANDSHAKE)
		{
			/* Copy the received packet's buffer back in handshake */
			memcpy(&handshake, recv_packet.buffer, sizeof(handshake));
			if (handshake.state == CLIENT_REQ)
			{
				client_number = handshake.client_number;
				if (TRACE) fout << "Server: received handshake 1 (C" << client_number << ")" << endl;
				           cout << "Server: received handshake 1 (C" << client_number << ")" << endl;
				break;
			}
		}
	}

	if ( handshake.direction == GET )
	{
		if (TRACE) fout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests GET file: \"" << handshake.filename << "\"" << endl;
				   cout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests GET file: \"" << handshake.filename << "\"" << endl;
		
		if ( FileExists(handshake.filename) )
		{
			bContinue = true;
			handshake.state = SERVER_ACKS; // server ACKs client's request
		}
		else
		{
			bContinue = false;
			handshake.state = FILE_NOT_EXIST;
			if (TRACE) fout << "Server: requested file does not exist." << endl;
					   cout << "Server: requested file does not exist." << endl;	
		}
	}
	else if ( handshake.direction == PUT )
	{
		bContinue = true;
		handshake.state = SERVER_ACKS; // server ACKs client's request
		if (TRACE) fout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests PUT file: \"" << handshake.filename << "\"" << endl;
				   cout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests PUT file: \"" << handshake.filename << "\"" << endl;
	}
	else if( handshake.direction == LIST)
	{
		bContinue=true;
		handshake.state = SERVER_ACKS; // server ACKs client's request
		cout << "Client requested for list of files" << endl;
	}
	else
	{
		bContinue = false;
		handshake.state = INVALID;
		if (TRACE) fout << "Server: invalid request." << endl;
				   cout << "Server: invalid request." << endl;	
	}
	
	if (!bContinue) // just send, don't expect a reply.
	{
		/* Place handshake in send_packet's buffer */
		send_packet.type = HANDSHAKE;
		send_packet.buffer_length = sizeof(handshake);
		memcpy(send_packet.buffer, &handshake, sizeof(handshake));
		
		/* Send MAX_RETRIES times */
		if ( SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet) ) { err_sys("Error in sending packet."); }

		if (TRACE) fout << "Server: sent error message to client" << endl;
				   cout << "Server: sent error message to client" << endl;
	}
	else // continue
	{
		srand((unsigned)time(NULL));
		server_number = rand() % MAX_RANDOM; // [0..255]	

		/* Prepare handshake and place in packet's buffer */
		handshake.server_number = server_number;
		send_packet.type = HANDSHAKE;
		send_packet.buffer_length = sizeof(handshake);
		memcpy(send_packet.buffer, &handshake, sizeof(handshake));

		/* Send the second handshake and keep at until until a response comes */
		do {
			if ( SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet) ) { err_sys("Error in sending packet."); }
			
			if (TRACE) fout << "Server: sent handshake 2 (C" << client_number << " S" << server_number << ")" << endl;
					   cout << "Server: sent handshake 2 (C" << client_number << " S" << server_number << ")" << endl;

			if ( ReceivePacket(sock, &recv_packet) == HANDSHAKE)
			{
				/* Copy the received packet's buffer back in handshake */
				memcpy(&handshake, recv_packet.buffer, sizeof(handshake));

				if (handshake.state == CLIENT_ACKS && handshake.server_number == server_number)
				{
					if (TRACE) fout << "Server: received handshake 3 (C" << client_number << " S" << server_number << ")" << endl;
					           cout << "Server: received handshake 3 (C" << client_number << " S" << server_number << ")" << endl;
					break;
				}
			}
		} while(1);

		WINDOW_SIZE=handshake.win_size;
		/* We know for sure that the right handshake has been received */		
		switch (handshake.direction)
		{
			case GET:
				if ( ! SendFile(sock, handshake.filename, server_name, client_number, WINDOW_SIZE) )
					err_sys("An error occurred while sending the file.");	
				break;

			case PUT:
				if ( ! ReceiveFile(sock, handshake.filename, server_name, server_number, WINDOW_SIZE) )
					err_sys("An error occurred while receiving the file.");	
				break;
			case LIST:				
					listFiles(sock, server_number);
					break;
			default:
				break;
		}

	}
	
	if (TRACE) fout << "Closing server socket." << endl;
	           cout << "Closing server socket." << endl;
	
	closesocket(sock);
	cout << "Press Enter to exit..." << endl; getchar();
}

void UdpServer::err_sys(char * fmt,...)
{     
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

unsigned long UdpServer::ResolveName(char name[])
{
	struct hostent *host; // Structure containing host information
	if ((host = gethostbyname(name)) == NULL)
		err_sys("gethostbyname() failed");
	return *((unsigned long *) host->h_addr_list[0]); // return the binary, network byte ordered address
}

UdpServer::UdpServer(char * fn) // constructor
{
	/* For timer */
	timeouts.tv_sec = STIMER;
	timeouts.tv_usec = UTIMER;
	
	/* Set the window size and sequence upperbound */
	//sequence_ubound = 2*WINDOW_SIZE - 1;

	/* Open log file */
	fout.open(fn);
} 

UdpServer::~UdpServer() // destructor
{
	/* Close log file */
	fout.close();
	
	/* Uninstall winsock.dll */
	WSACleanup();
}

int main(void)
{
	UdpServer * server = new UdpServer();
	server->run();
	return 0;
}
