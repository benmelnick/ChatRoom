//////// PROTOCOL DEFINITION ////////

#define SERVER_USERNAME "admin"  // Username added to messages sent explicitly by the server

// Buffer size constants
#define USERNAME_LENGTH   1024
#define PASSWORD_LENGTH   1024
#define DATA_LENGTH       1026   // Max message size is 1024, need 2 extra to account for '\n' and '\0'

// Commands available for client
#define HAPPY_COMMAND      1
#define SAD_COMMAND        2
#define MYTIME_COMMAND     3
#define MYTIMEPLUS_COMMAND 4
#define HELP_COMMAND       5
#define SENDMSG_COMMAND    6
#define QUIT_COMMAND       7

// Server response codes
#define OPEN               0     // Connection to client is still open
#define CLOSED             1     // Connection to client has been closed by the sever
#define UNAUTHORIZED       2     // Login attempt unsuccessful
#define AUTHORIZED         3     // Login attempt successful
#define ACCEPTED           4     // Client successfully connected to server
#define REJECTED           5     // Client was rejected from the server (too many users)

// Struct for passing login information b/w client and server
struct login_request {
  char username[USERNAME_LENGTH];
  char password[PASSWORD_LENGTH];
};

// Struct to return to user after login attempt
struct login_response {
  int status;               // Response code
  int id;                   // Client id
};

// Per TCP-connection client structure
typedef struct {
  struct sockaddr_in addr;     // Client source IP address and port
  int connection_sock;         // Connection socket file descriptor
  int id;                      // Client id
  pthread_t tid;               // Thread id for sending and receiving messages w/ this client
  char name[USERNAME_LENGTH];  // Client display name
} client_t;

// Client-server request/response definitions
struct client_message {
  int command;                // Command code for the type of message sent
  char data[DATA_LENGTH];     // Input from client application
};

struct server_message {
  int status;                       // Response code
  int uid;                          // Id of the client who sent the message (0 if sent by server)
  char username[USERNAME_LENGTH];   // Name of the person who sent the message ("admin" if sent by server)
  char data[DATA_LENGTH];           // Message from server to display in client
};
