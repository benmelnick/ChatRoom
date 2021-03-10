# Chat Room

Created by: Ben Melnick, bmelnick3@gatech.edu

This project contains 3 files: `protocol.h`, `chatclient.c`, and `chatserver.c`.

### `protocol.h`

For this project, I developed a custom application-layer protocol that defines how clients of this chat room server must interact with the server. This file contains the definition of the protocol, specifically the message formats that the server sends and receives. The protocol has 4 possible messages that can be transmitted between server and client: 

- `login_request`: a payload of username and password that clients send when they want to login to the server

- `login_response`: the response sent by the server back to the requesting client
  
  - Tells the client if they are authorized or not

- `client_message`: data sent from client to server to send to other clients in the chat room
  - Contains a command indicating the type of message and the actual message being sent

- `server_message`: data sent from the server to the client (either a metadata message such as “User has entered the chat room!” or a message from another client)
  - Contains a status code, the id and username of the sender of the message (either the server or some client) and the message data itself

### `chatserver.c`

The server is responsible for processing client requests (either `login_request` or `client_message`) and sending messages back to clients in response to these requests (either `login_response` or `server_message`). Once successfully started, the server runs forever until it is shut down externally via Ctrl-C.

To handle multiple simultaneous connections, the server uses multithreading (specifically `pthreads`). The `main()` thread that begins upon startup creates the TCP welcoming socket that accepts new connections from clients. After a connection is accepted, the server waits to receive a login_request from the client. Upon receiving a login request, it checks the password and takes the appropriate action. The `main()` thread continues to loop and accept new connections and process login requests until the server shuts down.

If the login request contains the correct password, the `main()` thread creates a new data structure (`client_t`) to hold metadata about a client connection, such as the socket file descriptor, the name and ID of the user, and the ID of the thread that is sending and receiving messages to and from the client. This structure is passed to a new thread created specifically for communicating with this new client. This thread checks for messages from its client (i.e. a `client_message`) and sends a `server_message` to one or many clients in the chat room depending on the command type in the message. The connection is closed and the thread stops running when the client sends a `QUIT` command or when the server process is shut down externally via Ctrl-C, in which case all threads are terminated and socket connections are closed.

The server process maintains a global array of client data structures that all client communication threads can see. Maintaining this array allows a thread to send messages to all clients in the chatroom after receiving a message from the client it is responsible for. 

All messages sent to and from clients are logged in `server_log.txt`, which is available upon server shutdown.

### `chatclient.c`

The client file simply contains business logic for accepting input from the user and sending the appropriate command and message to the server as well as receiving any messages sent from the server. 

The client always starts by trying to login to the server using the username and password provided via the command line interface to the client. If the attempt is successful, the client can proceed with communicating with the client.

To accomplish simultaneous sending and receiving of messages, the client uses multithreading. The client has a thread for sending messages to the client and a thread for receiving messages from the client. Both of these threads run so long as the connection with the server is open (that is, neither the client nor the server has terminated the connection). Clients maintain a global variable indicating whether the connection is open and thus if the `send() `and `recv()` threads need to keep executing. The global flag is cleared in the `recv()` thread whenever the thread receives a message from the server with a` CLOSED` status code, indicating that the server has closed the connection (either because the client told it to or for some other reason unknown to the client). The threads loop until this flag is cleared, at which point the threads finish up, the socket connection is closed, and the program terminates.

### Build and Compilation

Compilation was tested on Linux (macOS and Ubuntu). GCC is used to link and compile code. The following flags are used:

`-Wall -Wextra -Wpointer-arith -Wshadow -Wpedantic -std=c11`

C11 is used in order to use the `_Atomic` type qualifier for the shared connection flag in chatclient.c. The `send()` and `recv()` threads can write this variable at the same time that the `main()` thread reads it, so to avoid data races, reads and writes must be atomic. The `_Atomic` qualifier ensures that any use of the integer is atomic.

`Pthread` is also specified as a necessary library in this package. To create an executable for the server for example, the Makefile compiles the code as such:

`gcc -Wall -Wextra -Wpointer-arith -Wshadow -Wpedantic -std=c11 src/server.c -o server -pthread`

To clean the directory (i.e. delete the executables), run make clean. To build the entire package so that it can run, simply run make.

Interface and Usage

Both client and server have their own specific command line interfaces. The server has the following command line options:

| **Option Flag** | **Argument Type** | **Description**                                                                         |
| --------------- | ----------------- | --------------------------------------------------------------------------------------- |
| --start (-s)    | N/A               | Required to start the server                                                            |
| --port (-p)     | Integer           | The port number on the host to bind the server process to (must be between 1 and 65535) |

The client has the following command line options:

| **Option Flag** | **Argument Type** | **Description**                                                            |
|:--------------- | ----------------- | -------------------------------------------------------------------------- |
| --join (-j)     | N/A               | Required to join the chat room                                             |
| --host (-h)     | String            | The host name on which the server is running                               |
| --port (-p)     | Integer           | The port number on the specified host to which the server process is bound |
| --username (-u) | String            | The display name to show to other users                                    |
| --pascode (-c)  | String            | The password of the chat room (the same for all users)                     |

To start up the server and then have a client connect to the server in order to join that chat room, the following commands would be run:

`./chatserver --start --port 5001`

`./chatclient --join --host localhost --port 5001 --username Ben --passcode 3251secret`
