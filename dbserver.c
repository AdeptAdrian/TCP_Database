#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include "msg.h"

void Usage(char *progname);
int  Listen(char *portnum, int *sock_family);
void *HandleClientThread(void *args);

int 
main(int argc, char **argv) {
  // Expect the port number as a command line argument.
  if (argc != 2) {
    Usage(argv[0]);
  }

  int sock_family;
  int listen_fd = Listen(argv[1], &sock_family);
  if (listen_fd <= 0) {
    // We failed to bind/listen to a socket.  Quit with failure.
    printf("Couldn't bind to any addresses.\n");
    return EXIT_FAILURE;
  }

  // Loop forever, accepting a connection from a client and doing
  // an echo trick to it.
  pthread_t p1;
  while(1) {
    struct sockaddr_storage caddr;
    socklen_t caddr_len = sizeof(caddr);
    int client_fd = accept(listen_fd,
                           (struct sockaddr *)(&caddr),
                           &caddr_len);
    if (client_fd < 0) {
      if ((errno == EINTR) || (errno == EAGAIN) || (errno == EWOULDBLOCK))
       continue;
      printf("Failure on accept:%d \n ", errno);
       break;
    }
    pthread_create(&p1, NULL, HandleClientThread, &client_fd);
  }
  pthread_join(p1, NULL);

  // Close socket
  close(listen_fd);
  return EXIT_SUCCESS;
}

void Usage(char *progname) {
  printf("usage: %s port \n", progname);
  exit(EXIT_FAILURE);
}

int
Listen(char *portnum, int *sock_family) {

  // Populate the "hints" addrinfo structure for getaddrinfo().
  // ("man addrinfo")
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;       // IPv6 (also handles IPv4 clients)
  hints.ai_socktype = SOCK_STREAM;  // stream
  hints.ai_flags = AI_PASSIVE;      // use wildcard "in6addr_any" address
  hints.ai_flags |= AI_V4MAPPED;    // use v4-mapped v6 if no v6 found
  hints.ai_protocol = IPPROTO_TCP;  // tcp protocol
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;

  // Use argv[1] as the string representation of our portnumber to
  // pass in to getaddrinfo().  getaddrinfo() returns a list of
  // address structures via the output parameter "result".
  struct addrinfo *result;
  int res = getaddrinfo(NULL, portnum, &hints, &result);

  // Did addrinfo() fail?
  if (res != 0) {
	printf( "getaddrinfo failed: %s", gai_strerror(res));
    return -1;
  }

  // Loop through the returned address structures until we are able
  // to create a socket and bind to one.  The address structures are
  // linked in a list through the "ai_next" field of result.
  int listen_fd = -1;
  struct addrinfo *rp;
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    listen_fd = socket(rp->ai_family,
                       rp->ai_socktype,
                       rp->ai_protocol);
    if (listen_fd == -1) {
      // Creating this socket failed.  So, loop to the next returned
      // result and try again.
      printf("socket() failed:%d \n ", errno);
      listen_fd = -1;
      continue;
    }

    // Configure the socket; we're setting a socket "option."  In
    // particular, we set "SO_REUSEADDR", which tells the TCP stack
    // so make the port we bind to available again as soon as we
    // exit, rather than waiting for a few tens of seconds to recycle it.
    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
               &optval, sizeof(optval));

    // Try binding the socket to the address and port number returned
    // by getaddrinfo().
    if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
      // Bind worked!  
      // Return to the caller the address family.
      *sock_family = rp->ai_family;
      break;
    }

    // The bind failed.  Close the socket, then loop back around and
    // try the next address/port returned by getaddrinfo().
    close(listen_fd);
    listen_fd = -1;
  }

  // Free the structure returned by getaddrinfo().
  freeaddrinfo(result);

  // If we failed to bind, return failure.
  if (listen_fd == -1)
    return listen_fd;

  // Success. Tell the OS that we want this to be a listening socket.
  if (listen(listen_fd, SOMAXCONN) != 0) {
    printf("Failed to mark socket as listening:%d \n ", errno);
    close(listen_fd);
    return -1;
  }

  // Return to the client the listening file descriptor.
  return listen_fd;
}

void* 
HandleClientThread(void *args) {
  int c_fd = *(int *)args;
  struct msg message;
  //Open the database file, or create a new one if doesn't exist
  int db_fd = open("database.txt", O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
  if(db_fd < 0){
  	perror("Database file open failed");
	message.type = FAIL;
	write(c_fd, &message, sizeof(message));
	pthread_exit(NULL);
  }
									   
  // Loop, reading data and echo'ing it back, until the client
  // closes the connection.
  while (1) {
    //Read the message
    ssize_t res = read(c_fd, &message, sizeof(message));
    if (res == 0) {
      printf("[The client disconnected.] \n");
      break;
    } else if (res == -1) {
   	printf("Read failure");
      if ((errno == EAGAIN) || (errno == EINTR))
        continue;
	  printf(" Error on client socket:%d \n ", errno);
      break;
    //If no errors found, see if it's PUT or GET
    } else if (message.type == PUT) {
         //If PUT, go to the end of the file and add the info
    	 if(lseek(db_fd, 0, SEEK_END) == -1){
	 	perror("Seek failed");
		message.type = FAIL;
		if(write(c_fd, &message, sizeof(message)) == -1){
			perror("Failed to communicate with the client");
			break;
		}
		continue;
	 }
	 if(write(db_fd, &message.rd, sizeof(struct record)) == -1){
	 	perror("Write in database failed");
		if(write(c_fd, &message, sizeof(message)) == -1){
			perror("Failed to communicate with the client");
			break;
		}
		continue;
	 }
	 //If no errors with the write, send the SUCCESS message back to the client
	 message.type = SUCCESS;
	 if(write(c_fd, &message, sizeof(message)) == -1){
	 	perror("Failed to communicate with the client");
		break;
	 }
    //If GET, go to the beggining of the file
    } else if (message.type == GET) {
    	 if(lseek(db_fd, 0, SEEK_SET) == -1){
	 	perror("Seek failed");
		message.type = FAIL;
		if(write(c_fd, &message, sizeof(message)) == -1){
			perror("Failed to communicate with the client");
			break;
		}
		continue;
	 }
	 struct record rec;
	 //Assume failure by deafult
	 message.type = FAIL;
	 //Go though the file until a match in ID is found
	 while(read(db_fd, &rec, sizeof(rec))){
	 	if(rec.id == message.rd.id){
			//If matched, try sending to the client
			message.rd = rec;
			message.type = SUCCESS;
			if(write(c_fd, &message, sizeof(message)) == -1){
				perror("Failed to communicate with the client");
				message.type = FAIL;
			}
			break;
		}
	 }
	 //If not found or if failed to write back to the client, try sending the fail messag again
	 if(message.type == FAIL){
	 	if(write(c_fd, &message, sizeof(message)) == -1){
			perror("Failed to communicate with the client");
			break;
		}
	 }
	 
    } else {
      printf("Error in the signal communication between client and server");
      break;
    }
  }

  close(c_fd);
  pthread_exit(NULL);
}
