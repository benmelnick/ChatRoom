#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h> 
#include <pthread.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <time.h>
#include <signal.h>
#include "protocol.h"

#define MAX_CLIENTS   128 
#define PASSWORD      "cs3251secret"
#define LOG_FILE_PATH "server_log.txt"
#define SERVER_IP     "127.0.0.1"

/* Global variables observed by all threads */

// Array of clients connected to the server
// Insertion order preserved
// Access to list must be protected by mutex lock
client_t *clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// Clients metadata
int client_id = 1;
_Atomic int client_count = 0;

// Server log file
FILE *log_file;

// Socket file descriptor for socket that accepts new client connections
int listening_sock;

// Global flag that all socket threads loop on
// Cleared upon server shutdown (Ctrl-C)
_Atomic int server_running = 1;

// Logging methods
// Prints the string to stdout and writes it to the server's log file
void server_log(char *s)
{
  printf("%s", s);  // print to console
  fprintf(log_file, "%s", s);  // write to log file
}

// Prints the string to stderr and writes it to the server's log file
void server_error(char *s)
{
  perror(s);
  fprintf(log_file, "%s", s);
}

// Appends the IP address and port number of a socket to a string
void append_sock_addr(struct sockaddr_in addr, char *buff)
{
  char temp_buff[1024];
  sprintf(temp_buff, "IP: %d.%d.%d.%d port: %d",
        addr.sin_addr.s_addr & 0xff,
        (addr.sin_addr.s_addr & 0xff00) >> 8,
        (addr.sin_addr.s_addr & 0xff0000) >> 16,
        (addr.sin_addr.s_addr & 0xff000000) >> 24,
        addr.sin_port);

  strcat(buff, temp_buff);
}

/* clients data structure methods */

// Adds a client to the clients array
// Insert at the end of the array
void add_client(client_t *client)
{
  pthread_mutex_lock(&clients_mutex);
  clients[client_count++] = client;
  pthread_mutex_unlock(&clients_mutex);
}

// Removes a client from the clients array
// Shifts elements after client 1 spot to the left
void delete_client(client_t *client)
{
  pthread_mutex_lock(&clients_mutex);

  // Find index of client in the array
  int pos;
  for (pos = 0; pos < client_count; pos++) {
    if (clients[pos]->id == client->id) {
      break;
    }
  }

  // Shift elements
  for (int i = pos; i < client_count - 1; i++) {
    clients[i] = clients[i + 1];
  }
  clients[--client_count] = 0;

  pthread_mutex_unlock(&clients_mutex);

  free(client); // free the memory
}

// Sends a message to a client
// If src is null then the message is being sent by the server
void send_message_to_client(char *s, client_t *dst, client_t *src)
{
  struct server_message msg;
  char log_buff[1024];

  msg.status = OPEN;
  strcpy(msg.data, s);
  if (src == NULL) {
    msg.uid = 0;
    strcpy(msg.username, SERVER_USERNAME);
  } else {
    msg.uid = src->id;
    strcpy(msg.username, src->name);
  }
  if (send(dst->connection_sock, &msg, sizeof(msg), 0) < 0) {
    // todo: better error handling??
    sprintf(log_buff, "Write to client %d failed\n", dst->id);
    server_log(log_buff);
  } 
}

// Sends messages to all clients connected to server
void send_message_to_all(char *s, client_t *src)
{
  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < client_count; i++) {
    if (clients[i]) {
      send_message_to_client(s, clients[i], src);
    }
  }
  pthread_mutex_unlock(&clients_mutex);
}

// Sends message to all clients in server except the client who sent the message
void send_message_to_all_except(char *s, client_t *src, client_t *except)
{
  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < client_count; i++) {
    if (clients[i] && clients[i] != except) {
      send_message_to_client(s, clients[i], src);
    }
  }
  pthread_mutex_unlock(&clients_mutex);
}

void send_menu(client_t *client)
{
  char buff_out[2048];
  strcat(buff_out, "*** :)        Send 'Feeling happy'\r\n");
  strcat(buff_out, "*** :(        Send 'Feeling sad'\r\n");
  strcat(buff_out, "*** :mytime   Send the current time\r\n");
  strcat(buff_out, "*** :+1hr     Send the current time + 1 hour\r\n");
  strcat(buff_out, "*** :Exit     Quit\r\n");
  strcat(buff_out, "*** :help     Show help\r\n");

  send_message_to_client(buff_out, client, NULL);
}

void send_closed_signal(client_t *client) {
  struct server_message msg;

  msg.status = CLOSED;
  msg.uid = 0;
  strcpy(msg.username, SERVER_USERNAME);
  strcpy(msg.data, "*** Server has terminated connection\n");
  if (send(client->connection_sock, &msg, sizeof(msg), 0) < 0) {
    // todo: better error handling??
    printf("Write to client %d failed\n", client->id);
  } 
}

// Closes a client connection
void close_connection(client_t *client)
{
  char log_buff[2048];

  close(client->connection_sock);  // close the socket

  sprintf(log_buff, "Client %d (%s) disconnected\n", client->id, client->name);
  server_log(log_buff);
}

// Per-connection server thread for communicating with a client
// Client information (i.e. display name, id, socket number) passed in via arg
// Reads in input from client and sends it to all other clients in the server
void *client_comm(void *arg)
{
  char in_buff[2048], out_buff[2048], log_buff[2048];
  struct client_message client_msg;   // data received from client

  client_t *client = (client_t *)arg;

  // Time info
  time_t tme;
  struct tm *time_info;

  sprintf(log_buff, "Client %d (%s) accepted from ", client->id, client->name);
  append_sock_addr(client->addr, log_buff);
  strcat(log_buff, "\n");
  server_log(log_buff);

  // Tell other clients that a new client has joined
  sprintf(out_buff, "*** %s has joined the chat room!\n", client->name);
  server_log(out_buff);
  send_message_to_all_except(out_buff, NULL, client);

  // Print the help menu to the new client
  send_menu(client);

  // Spin as long as server is still running and the connection is still alive
  fd_set rfds;
  int rc;
  struct timeval no_timeout; 
  no_timeout.tv_sec = 0;  // set timeout to 0 so select() immediately returns
  no_timeout.tv_usec = 0;
  while (server_running) {
    // Use select() to check if there is data to be read in the socket
    // Can go right to next iteration of loop if no data to read instead of blocking on recv() all the time
    FD_ZERO(&rfds);
    FD_SET(client->connection_sock, &rfds);
    if ((rc = select(client->connection_sock + 1, &rfds, NULL, NULL, &no_timeout)) < 0) {
      // Error, close connection
      server_error((char *)"select() error after connection established\n");
      break;
    }

    // Check if the data is available
    if (!FD_ISSET(client->connection_sock, &rfds))
      continue;

    // Read the data
    // Break the loop if recv() does not return > 0 (error or connection was closed client side)
    if (recv(client->connection_sock, &client_msg, sizeof(client_msg), 0) <= 0)
      break;

    // Setup buffers
    memset(out_buff, 0, sizeof(out_buff));
    memset(log_buff, 0, sizeof(log_buff));
    sprintf(log_buff, "> %s: ", client->name); // need to log the message from the client 

    if (client_msg.command == HAPPY_COMMAND) {
      sprintf(out_buff, "Feeling happy\n");
      send_message_to_all(out_buff, client);
    } else if (client_msg.command == SAD_COMMAND) {
      sprintf(out_buff, "Feeling sad\n");
      send_message_to_all(out_buff, client);
    } else if (client_msg.command == MYTIME_COMMAND) {
      time(&tme);
      time_info = localtime(&tme);
      sprintf(out_buff, "My current time is: %02d:%02d:%02d\n", time_info->tm_hour, time_info->tm_min, time_info->tm_sec);
      send_message_to_all(out_buff, client);
    } else if (client_msg.command == MYTIMEPLUS_COMMAND) {
      time(&tme);
      time_info = localtime(&tme);
      sprintf(out_buff, "My time in one hour will be: %02d:%02d:%02d\n", 
              (time_info->tm_hour == 23 ? 0 : time_info->tm_hour + 1), time_info->tm_min, time_info->tm_sec);
      send_message_to_all(out_buff, client);
    } else if (client_msg.command == HELP_COMMAND) {
      send_menu(client);
    } else if (client_msg.command == QUIT_COMMAND) {
      // Break the loop and close the connection to this client
      break;
    } else if (client_msg.command == SENDMSG_COMMAND) {
      strcpy(in_buff, client_msg.data);
      strcat(out_buff, in_buff);
      strcat(out_buff, "\n");
      send_message_to_all_except(out_buff, client, client);
    } else {
      // unknown command
      memset(log_buff, 0, sizeof(log_buff)); // clear the log buffer (remove the user name from the front)
      sprintf(out_buff, "*** Unknown command passed in by client %d: %d\n", client->id, client_msg.command);
      send_message_to_client(out_buff, client, NULL);
    }
    
    // Log whatever message was sent to client(s)
    strcat(log_buff, out_buff);
    server_log(log_buff);
  }

  // Client entered QUIT or closed the connection otherwise (i.e. Ctrl-C)
  // Send a message that the client left the chat room
  sprintf(out_buff, "*** %s has left the chat room!\n", client->name);
  server_log(out_buff);
  send_message_to_all_except(out_buff, NULL, client);
  
  // Close connection
  send_closed_signal(client);   // send a CLOSED status code to the client
  close_connection(client);
  delete_client(client);

  // Release the thread
  pthread_detach(pthread_self());

  return 0;
}

void print_usage()
{
  printf("Usage: server -s -p <portnumber>\n");
}

// Set the shutdown flag upon Ctrl-C
// Server was shutdown:
//   1. wait for all threads to finish so connections are closed
//   2. close the log file
// Other threads will see the change and close their connections
// Only the main thread runs this code since the handler is set inside main()
void catch_ctrl_c()
{
  // Reset back to the default signal
  signal(SIGINT, SIG_DFL);

  server_log((char *)"Server shutting down...\n");

  // Set the server_running flag so all threads will begin to shut down
  server_running = 0;
  
  // Wait for all threads to finish
  while (client_count != 0);

  // Close the listening socket
  close(listening_sock);

  server_log((char *)"Server has terminated all connections.\n-----\n");
  fclose(log_file);
  printf("Server logs are available at server_log.txt");
  fflush(stdout); // immediately print what is in the stdout buffer

  raise(SIGINT); // raise the signal so the main process gets killed
}
   
// Main server thread for accepting new client connections   
int main(int argc, char *argv[])
{
  // Parse command line arguments
  int opt, option_index;
  int start_flag = 0;
  int port;

  struct option long_options[] = {
    {"start", no_argument, NULL, 's'},
    {"port", required_argument, NULL, 'p'},
    {0, 0, 0, 0}
  };

  char optstring[3] = "sp:"; // place colon after options requiring variables
  while ((opt = getopt_long_only(argc, argv, optstring, long_options, &option_index)) != -1) {
    switch (opt) {
      case 's': 
        start_flag = 1;
        break;
      case 'p': 
        port = atoi(optarg);
        break;
      default: 
        printf("Error!\n");
        return EXIT_FAILURE;
    }
  }

  // Start flag not set
  if (!start_flag) {
    printf("Must include --start or -s argument to begin server\n");
    print_usage();
    return EXIT_FAILURE;
  }

  if (port < 1 || port > 65535) {
    printf("Must provide a port number between 1 and 65535\n");
    print_usage();
    return EXIT_FAILURE;
  }

  // Create the log file
  log_file = fopen(LOG_FILE_PATH, "w");

  // Socket file descriptor for new incoming connections
  int connection_sock;

  // Socket address and port metadata for client and server 
  struct sockaddr_in server_addr, client_addr; 

  // Login info passed from client to server
  struct login_request login_request;

  // Login response sent back to client
  struct login_response login_resp;
      
  // Create TCP listening socket for accepting connections
  if ((listening_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) { 
    server_error((char *)"Socket creation failed.\n"); 
    return EXIT_FAILURE;
  } 
      
  // Set SO_REUSEADDR option to avoid binding issues after the server was previously shutdown
  int sock_opt = 1;
  if (setsockopt(listening_sock, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(sock_opt))) { 
    server_error((char *)"setsockopt failed"); 
    return EXIT_FAILURE;
  } 

  // Set server socket information
  server_addr.sin_family = AF_INET; 
  server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
  server_addr.sin_port = htons(port); 
      
  // Forcefully attaching socket to the port number and IP address
  // Incoming messages to the IP + port combo will come through this socket 
  if (bind(listening_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) { 
    server_error((char *)"Binding failed"); 
    return EXIT_FAILURE;
  } 

  // Starting listening for new connections
  if (listen(listening_sock, 3) < 0) { 
    server_error((char *)"listen"); 
    return EXIT_FAILURE;
  }

  char log_buff[2048];
  sprintf(log_buff, "-----\nSERVER STARTED. Listening on port %d...\n", port);
  server_log(log_buff);

  // Set handler for ctrl-c
  signal(SIGINT, catch_ctrl_c);

  // Loop forever accepting new clients until server encounters an error or is shut down
  while (1) {
    socklen_t addrlen = sizeof(client_addr);
    // Accept connection from client and create a new socket for the connection
    // Places source information about client in client_addr struct
    if ((connection_sock = accept(listening_sock, (struct sockaddr *)&client_addr, (socklen_t*)&addrlen)) < 0) { 
      server_error((char *)"accept"); 
      close(listening_sock);
      return EXIT_FAILURE;
    } 

    // Check if max number of clients have been reached
    int conn_successful;
    if (client_count == MAX_CLIENTS) {
      char buff[1024];
      sprintf(buff, "Max clients reached, rejecting login attempt by user at "); 
      append_sock_addr(client_addr, buff);
      strcat(buff, "\n");
      server_log(buff);
      conn_successful = REJECTED;
      send(connection_sock, &conn_successful, sizeof(conn_successful), 0);
      close(connection_sock);
      continue;
    }
    conn_successful = ACCEPTED;
    send(connection_sock, &conn_successful, sizeof(conn_successful), 0);

    // Receive login request and evaluate
    recv(connection_sock, &login_request, sizeof(login_request), 0);
    if (strcmp(login_request.password, PASSWORD) == 0) {
      login_resp.status = AUTHORIZED;
      // Initialize client and start communication
      client_t *new_client = (client_t*)malloc(sizeof(client_t));
      new_client->addr = client_addr;
      new_client->connection_sock = connection_sock;
      strcpy(new_client->name, login_request.username);
      new_client->id = client_id++;

      // Setup response to send back to client
      login_resp.id = new_client->id;

      // Add client to list of server's clients and create new thread for the connection
      add_client(new_client);
      pthread_create(&new_client->tid, NULL, &client_comm, (void*)new_client);
    } else {
      // Password does not match send rejection message and continue listening
      login_resp.status = UNAUTHORIZED;
    }
    send(connection_sock, &login_resp, sizeof(login_resp), 0);
  }

  // Unreachable, but close the listening sock just in case
  close(listening_sock);

  return EXIT_SUCCESS;
}
