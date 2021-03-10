#include <stdio.h> 
#include <sys/socket.h> 
#include <sys/select.h>
#include <arpa/inet.h> 
#include <unistd.h> 
#include <string.h> 
#include <stdlib.h> 
#include <pthread.h>
#include <getopt.h>
#include <ctype.h>
#include "protocol.h"

/* Global variables available to all threads */

char display_name[USERNAME_LENGTH];
int client_socket;
int client_id;

// Global flag that send() and recv() threads read and write during execution
// Clearing this flag will stop both threads execution
_Atomic int client_running = 1;

// Checks if a provided string contains entirely alphanumeric characters
int isalnum_str(char *s)
{
  int i = 0;
  char ch;

  while ((ch = s[i++]) != '\0') {
    if (!isalnum(ch))
      return -1;
  }

  return 0;
}

// Prompts user to enter login credentials
// Returns 0 if logged in, -1 if not
struct login_response login(char *username, char *pwd)
{
  struct login_request login_request;
  struct login_response login_resp;

  // Send login request to server
  strcpy(login_request.username, username);  // copy name into global variable
  strcpy(login_request.password, pwd);
  strcpy(display_name, username);
  send(client_socket, &login_request, sizeof(login_request), 0);

  // Receive login response from server
  recv(client_socket, &login_resp, sizeof(login_resp), 0);

  return login_resp;
}

// Thread for reading input from stdin and sending messages to the chat room
void *send_messages() 
{
  char data_buff[DATA_LENGTH];
  struct client_message client_msg;

  fd_set rfds;
  int rc;
  struct timeval no_timeout;
  no_timeout.tv_sec = 0;
  no_timeout.tv_usec = 0;

  // Prompt for input
  printf("> ");
  fflush(stdout);    // print the character right away, don't wait for new line to show up in stdout buffer

  while (client_running) {
    // Check if stdin has any input to read with select()
    // Set a timeout of 0 so we can check for input without blocking
    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    if ((rc = select(1, &rfds, NULL, NULL, &no_timeout)) < 0) {
      printf("select() error with stdin\n");
      continue;
    }

    // Check if data is available
    if (!FD_ISSET(0, &rfds))
      continue;

    // Read the data from stdin
    fgets(data_buff, DATA_LENGTH, stdin);

    // Check how much the user entered
    if (strlen(data_buff) == 1) {
      // User just hit enter key by itself, don't send a message
      continue;
    } else if (strchr(data_buff, '\n') == NULL) {
      // New line was not read - input is too long
      // Need to read in and discard the rest of the input
      int c;
      while ((c = getc(stdin)) != '\n' && c != EOF);
      printf("Input is too long, cannot exceed %d characters\n", DATA_LENGTH);
      continue;
    }

    // Remove new line character
    data_buff[strlen(data_buff) - 1] = '\0';

    if (strcmp(data_buff, ":)") == 0) {
      client_msg.command = HAPPY_COMMAND;
    } else if (strcmp(data_buff, ":(") == 0) {
      client_msg.command = SAD_COMMAND;
    } else if (strcmp(data_buff, ":mytime") == 0) {
      client_msg.command = MYTIME_COMMAND;
    } else if (strcmp(data_buff, ":+1hr") == 0) {
      client_msg.command = MYTIMEPLUS_COMMAND;
    } else if (strcmp(data_buff, ":help") == 0) {
      client_msg.command = HELP_COMMAND;
    } else if (strcmp(data_buff, ":Exit") == 0) {
      // client closed the connection
      client_msg.command = QUIT_COMMAND;
    } else {
      client_msg.command = SENDMSG_COMMAND;
      strcpy(client_msg.data, data_buff);
    }

    send(client_socket, &client_msg, sizeof(client_msg), 0);

    // Prompt for input
    printf("> ");
    fflush(stdout);
  }

  return 0;
}

// Thread for receiving messages from chat server and printing to stdout
// Need a separate thread for receiving messages so the client can still listen
// for messages from the server while the user is prompted for input
void *recv_messages() 
{
  struct server_message server_msg;

  // Spin as long as client is still running and connection is alive
  fd_set rfds;
  int rc;
  struct timeval no_timeout;
  no_timeout.tv_sec = 0;
  no_timeout.tv_usec = 0;
  while (client_running) {
    // Use select() to check if there is data to be read in the socket
    FD_ZERO(&rfds);
    FD_SET(client_socket, &rfds);
    if ((rc = select(client_socket + 1, &rfds, NULL, NULL, &no_timeout)) < 0) {
      // Error, close connection
      printf("select() error after connection established, shutting down...\n");
      client_running = 0; // shutdown client
      break;
    }

    // Check if the data is available
    if (!FD_ISSET(client_socket, &rfds))
      continue;

    // Read the data
    // Break the loop if recv() does not return > 0 (error or connection was closed client side)
    if (recv(client_socket, &server_msg, sizeof(server_msg), 0) <= 0) {
      printf("Could not receive data from server, shutting down...\n");
      client_running = 0;   // shutdown the client if the connection breaks
      break;
    }

    if (server_msg.uid == 0) {
      // print the message outright if from the server
      printf("\r%s", server_msg.data);
    } else if (server_msg.uid == client_id) {
      // message originally sent by this client and returned to us
      // happens for special commands like ':)'
      // print the message without the username
      printf("\r> %s", server_msg.data);
    } else {
      printf("\r> %s: %s", server_msg.username, server_msg.data);
    }

    // Stop looping if the server sent a signal indicating that it closed the connection
    // Server would send a closed signal either when the server is shut down OR after
    //   the client sends QUIT command/closes the connection
    // Setting client_running will cause both threads to return
    if (server_msg.status == CLOSED) {
      client_running = 0;
    } else {
      // Connection still open, print another '>' to prompt for user input
      printf("> ");
      fflush(stdout);
    }
  }

  return 0;
}

// Prints CLI usage
void print_usage()
{
  printf("Usage: client -j -h <hostname> -p <portnumber> -u <username> -c <passcode>\n");
}

// Main thread for logging into the chat room
int main(int argc, char *argv[]) 
{ 
  // Parse command line arguments
  int opt, option_index;
  int join_flag = 0;
  int port;
  char hostname[1024], username[USERNAME_LENGTH], passcode[PASSWORD_LENGTH];

  struct option long_options[] = {
    {"join", no_argument, NULL, 'j'},
    {"host", required_argument, NULL, 'h'},
    {"port", required_argument, NULL, 'p'},
    {"username", required_argument, NULL, 'u'},
    {"passcode", required_argument, NULL, 'c'},
    {0, 0, 0, 0}
  };

  char optstring[10] = "jh:p:u:c:";
  while ((opt = getopt_long_only(argc, argv, optstring, long_options, &option_index)) != -1) {
    switch (opt) {
      case 'j': 
        join_flag = 1;
        break;
      case 'h': 
        strcpy(hostname, optarg);
        break;
      case 'p': 
        port = atoi(optarg);
        break;
      case 'u': 
        strcpy(username, optarg);
        break;
      case 'c': 
        strcpy(passcode, optarg);
        break;
      default: 
        printf("Error!\n");
        return EXIT_FAILURE;
    }
  }

  // Check join flag
  if (!join_flag) {
    printf("Must include --join or -j to connect to the chat room\n");
    print_usage();
    return EXIT_FAILURE;
  }

  // Check if any arguments are missing
  if (!port || strlen(hostname) == 0 || strlen(username) == 0 || strlen(passcode) == 0) {
    printf("One or more of the required arguments is missing\n");
    print_usage();
    return EXIT_FAILURE;
  }

  // Check if the provided username is alphanumeric
  if (isalnum_str(username) < 0) {
    printf("Username must only contain alphanumeric characters\n");
    print_usage();
    return EXIT_FAILURE;
  }

  // If localhost was passed in, change to the appropriate IP address
  if (strcmp(hostname, "localhost") == 0) 
    strcpy(hostname, "127.0.0.1");
  
  int ret;
  struct sockaddr_in serv_addr; 
  pthread_t send_tid, recv_tid; 

  if ((client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) { 
    printf("Socket creation error.\n"); 
    return EXIT_FAILURE;
  } 
  
  // Set the destination address and port
  serv_addr.sin_family = AF_INET; 
  serv_addr.sin_port = htons(port); 
  
  // Convert IPv4 and IPv6 addresses from text to binary form 
  if(inet_pton(AF_INET, hostname, &serv_addr.sin_addr) <= 0) { 
    printf("Invalid address. Address not supported: IP %s port %d\n", hostname, port); 
    return EXIT_FAILURE;
  } 
  
  // Establish connection with server
  // Connection is identified by the specific client-server pair
  if ((ret = connect(client_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) < 0) { 
    printf("Could not connect to server at IP %s port %d.\n", hostname, port); 
    return EXIT_FAILURE;
  } 

  // Wait to receive a signal from server indicating if connection was successful
  int conn;
  recv(client_socket, &conn, sizeof(conn), 0);
  if (conn == REJECTED) {
    printf("Rejected by server at IP %s port %d.\n", hostname, port);
    return EXIT_FAILURE;
  }
  printf("Connected to server at IP %s port %d.\n", hostname, port); 

  // Prompt user to login until successful
  struct login_response login_resp = login(username, passcode);
  if (login_resp.status == UNAUTHORIZED) {
    printf("Incorrect password, login request denied.\n"); 
    return EXIT_FAILURE;
  }

  // Set client id
  client_id = login_resp.id;
  printf("\n~~~~~~~~~~~Welcome to the chat room, %s (uid %d)~~~~~~~~~~~\n\n", username, client_id);

  // Create threads for sending and receiving messages
  pthread_create(&recv_tid, NULL, &recv_messages, NULL);
  pthread_create(&send_tid, NULL, &send_messages, NULL);

  // Wait for threads to finish
  pthread_join(recv_tid, NULL);
  pthread_join(send_tid, NULL); 

  close(client_socket);

  return 0; 
} 
