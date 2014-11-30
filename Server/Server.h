/*
Name- Sumair (7099347) & Mandeep Singh (7163738)

*/


//#define WINDOW_SIZE 19
#define MAX_RETRIES 20
#define TRACE 1

int WINDOW_SIZE;

#define STIMER 0
#define UTIMER 30000             // in nanoseconds
#define OLDEST_FRAME_TIMER	400  // in milliseconds
#define SERVER_PORT 5001
#define REMOTE_PORT 7001

#define INPUT_LENGTH    20
#define HOSTNAME_LENGTH 20
#define USERNAME_LENGTH 20
#define FILENAME_LENGTH 20
#define FRAME_BUFFER_SIZE 60
#define PACKET_BUFFER_SIZE 80
#define MAX_RANDOM 256;
#define SEQUENCE_WIDTH 3

typedef enum { HANDSHAKE=1, FRAME, FRAME_RESPONSE, TIMEOUT, PACKET_RECV_ERROR } PacketType;
typedef enum { CLIENT_REQ=1, SERVER_ACKS, CLIENT_ACKS, FILE_NOT_EXIST, INVALID } HandshakeState;
typedef enum { GET=1, PUT=2, LIST=3 } Direction;
typedef enum { ACK=1, NAK } FrameResponseType;

typedef struct {
	PacketType type;
	int buffer_length;
	char buffer[PACKET_BUFFER_SIZE];
} Packet;

typedef struct {
	HandshakeState state;
	Direction direction;
	int client_number;
	int server_number;
	int win_size;
	char hostname[HOSTNAME_LENGTH];
	char username[USERNAME_LENGTH];
	char filename[FILENAME_LENGTH];
} Handshake;

typedef struct {
	unsigned int sequence;
	bool last;
	int buffer_length;
	char buffer[FRAME_BUFFER_SIZE];
} Frame;

typedef struct { 
	FrameResponseType type;
	int number;
} FrameResponse;

class UdpServer
{
	WSADATA wsadata;
	int sock;						// Socket descriptor server and client
	struct sockaddr_in sa;			// server info, IP, port 5001
	struct sockaddr_in sa_in;		// router info, IP, port 7000
	int sa_in_size;
	int client_number;
	int server_number;
	struct timeval timeouts;

	int window_size;
	int sequence_ubound;

	Handshake handshake;
	Packet send_packet;
	Packet recv_packet;
	
	char server_name[HOSTNAME_LENGTH];

private:
		std::ofstream fout;	//log file

public: 
	UdpServer(char *fn="server_log.txt");	// constructor
	~UdpServer();	// destructor
	void run();

	bool SendFile(int, char *, char *, int, int);
	int SendPacket(int, Packet *, struct sockaddr_in *);

	bool ReceiveFile(int, char *, char *, int, int);
	PacketType ReceivePacket(int, Packet *);
	bool listFiles(int, int);

	unsigned long ResolveName(char name[]);
    void err_sys(char * fmt,...);
};
