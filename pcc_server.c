#include <signal.h>
#include <stdint.h> // for uint32_t, uint16_t
#include <stdio.h> // for printf, perror
#include <stdlib.h> // for atoi, exit
#include <arpa/inet.h>      // htons, htonl, ntohl
#include <netinet/in.h>    // struct sockaddr_in, INADDR_ANY
#include <sys/socket.h>    // socket, bind, listen, accept, setsockopt
#include <sys/types.h>     // socklen_t
#include <unistd.h>        // read, write, close
#include <string.h>        // memset
#include <errno.h>         // errno, EINTR


#define CLIENT_OK        0
#define CLIENT_FAILED   -2
#define SERVER_STOP     -1



// global variables
uint32_t pcc_total[95];
uint32_t clients_served = 0;

// SIGINT might happen anytime, therefore needs to be atomic to ensure full write and avoid undefined behavior
sig_atomic_t sigint_received = 0;

// -------------------------------------------------------------


void handle_sigint(int sig) {
    sigint_received = 1;
}

// -------------------------------------------------------------

int parse_args(int argc, char *argv[], struct sockaddr_in *server_addr) {
    
    if (argc != 2) {
    fprintf(stderr,"This program accepts exactly 1 arguments: server's port address\n");
       return -1;
    }

    memset(server_addr, 0, sizeof(struct sockaddr_in));
    server_addr->sin_family = AF_INET;
    server_addr->sin_addr.s_addr = INADDR_ANY;
    uint16_t port;
    // argv[1] is the port number assumed to be a 16-bit unsigned integer
    if(sscanf(argv[1], "%hu", &port) != 1) {
        perror("could not convert port number to 16 bit unsigned int");
        return -1;
    }
    server_addr->sin_port = htons(port);

    return 0;
}

// -------------------------------------------------------------

void TCP_listen(struct sockaddr_in *server_addr, int *listenfd) {

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket");
        exit(1);
    }
        
    // set SO_REUSEADDR for a quick reuse of the address
    int opt = 1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Error in setsockopt");
        close(sockfd);
        exit(1);
    }

    if (bind(sockfd, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
        perror("Error binding socket");
        close(sockfd);
        exit(1);
    }

    if (listen(sockfd, 10) < 0) {
        perror("Error listening on socket");
        close(sockfd);
        exit(1);
    }

    *listenfd = sockfd;
}

// -------------------------------------------------------------


int client_connection(int listenfd) {
    
    //-------------------------------------
    // Accept a new TCP connection

    struct sockaddr_in peer_addr;
    socklen_t addrsize = sizeof(peer_addr);

    int connfd = accept(listenfd, (struct sockaddr *)&peer_addr, &addrsize);
    
    //-------------------------------------
    // no clients
    if (connfd < 0) {
    
		// if there is SIGINT now, exit and print statistics
        if (errno == EINTR && sigint_received) 
		{
            return SERVER_STOP;
        }

        // accept was interrupted by a signal other than SIGINT - retry accept
        if(errno == EINTR && !sigint_received)
        {
            return CLIENT_FAILED;
        }

        // accept failed for other reasons 
        perror("accept failed");
		exit(1);
        
    }

    //-------------------------------------
    // client connected - NOT STOPPING DUE TO SIGNALS
    
    // STEP 1 : Receive the file size - exactly 4 bytes

    uint32_t net_file_size;
    size_t total_read = 0;

    while (total_read < sizeof(net_file_size)) {
        ssize_t n = read(connfd, ((char *)&net_file_size) + total_read, sizeof(net_file_size) - total_read);

        if (n < 0) {

            // check fail reason:

            // 1) read was interrupted by a signal – retry
            if (errno == EINTR) {
                continue;
            }

            // 2)  TCP-related errors - move to next client
            if (errno == ETIMEDOUT ||
                errno == ECONNRESET ||
                errno == EPIPE) {

                fprintf(stderr, "TCP error while reading file size\n");
                close(connfd);
                return CLIENT_FAILED;
            }

            // 3) Any other error - must exit
            perror("read file size failed");
            close(connfd);
            exit(1);
        }

        if (n == 0) {
            // we are reading exactly 4 bytes, but client closed connection before sending all 4 bytes - move to next client
            fprintf(stderr, "Client disconnected while sending file size\n");
            close(connfd);
            return CLIENT_FAILED;
        }

        total_read += n;
    }

    // Convert file size from network byte order to host byte order
    uint32_t file_size = ntohl(net_file_size);

    // STEP 2 : Receive the file data and count printable characters

     //-------------------------------------
    // Receive the file content - in buffer chunks or the remaining bytes

    uint32_t client_counts[95] = {0};
    uint32_t printable_count = 0;

    char buffer[16384];
    size_t total_bytes_read = 0;

    while (total_bytes_read < file_size) {

        size_t remaining = file_size - total_bytes_read;
        size_t to_read = remaining < sizeof(buffer) ? remaining : sizeof(buffer);

        ssize_t n = read(connfd, buffer, to_read);

         if (n < 0) {

            // check fail reason:

            // 1) read was interrupted by a signal – retry
            if (errno == EINTR) {
                continue;
            }

            // 2)  TCP-related errors - move to next client
            if (errno == ETIMEDOUT ||
                errno == ECONNRESET ||
                errno == EPIPE) {

                fprintf(stderr, "TCP error while reading file content\n");
                close(connfd);
                return CLIENT_FAILED;
            }

            // 3) Any other error - must exit
            perror("read file content failed");
            close(connfd);
            exit(1);
        }

        if (n == 0) {
            // we are reading exactly the file size, but client closed connection before sending all bytes - move to next client
            fprintf(stderr, "Client disconnected while sending file content\n");
            close(connfd);
            return CLIENT_FAILED;
        }


        for (ssize_t i = 0; i < n; i++) {
            unsigned char ch = (unsigned char)buffer[i];
            if (ch >= 32 && ch <= 126) {
                client_counts[ch - 32]++;
                printable_count++;
            }
        }

        total_bytes_read += n;
    }

    // STEP 3 : Send back the printable character count - exactly 4 bytes

        //------------------------
    // send printable characters count back to client

    uint32_t net_count = htonl(printable_count);
    size_t total_written = 0;

    while (total_written < sizeof(net_count)) {
        ssize_t n = write(connfd, ((char *)&net_count) + total_written, sizeof(net_count) - total_written);

        if (n < 0) {

            // 1) write was interrupted by a signal – retry
            if (errno == EINTR) {
                continue;
            }

            // 2) TCP-related errors – move to next client
            if (errno == ETIMEDOUT ||
                errno == ECONNRESET ||
                errno == EPIPE) {

                fprintf(stderr, "TCP error while writing result to client\n");
                close(connfd);
                return CLIENT_FAILED;
            }

            // 3) Any other error – must exit
            perror("write result failed");
            close(connfd);
            exit(1);
        }

        total_written += n;
    }

    // STEP 4 : Update global statistics 

    for (int i = 0; i < 95; i++) {
        pcc_total[i] += client_counts[i];
    }

    // ALL CLIENT STEPS ARE DONE SUCCESSFULLY - CAN ADD TO clients_served
    close(connfd);
    clients_served++;

    return CLIENT_OK;
}


//---------------------------------------------------------------

int main(int argc, char *argv[]) {

    memset(pcc_total, 0, sizeof(pcc_total));
    struct sockaddr_in server_addr; 
    int ret = 0;
    int listenfd = 0;

    // handle SIGINT signal
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    
    // Registering handle_sigint for SIGINT
    if(sigaction(SIGINT, &sa, NULL) == -1){
        perror("Signal handle registration failed");
        exit(1);
    }
    
    // read arguments from command line - check the number of arguments
    ret = parse_args(argc, argv, &server_addr);
    if (ret < 0) {
        exit(1);
    }

    // listen for incoming TCP connections 
    TCP_listen(&server_addr, &listenfd);
    
    // accept a TCP connection from a client and count printable characters
    // no client connected yet
    // if SIGINT is received, print statistics
    while (!sigint_received) {
        ret = client_connection(listenfd);      
        if (ret == CLIENT_FAILED) {
            // client failed, server continues to accept new clients
            continue;
        }

        if (ret == SERVER_STOP) {
            break; // SIGINT received, stop accepting new clients, print statistics
        }
    }

    // print character statistics
    for (int i = 0; i < 95; i++) { 
        if (pcc_total[i] > 0) {
            // prints the ASCII character itself
            printf("char '%c' : %u times\n", i + 32, pcc_total[i]);
        }
    }    
    printf("Served %u client(s) successfully\n", clients_served);

    // clean up and exit
    // assumption: no need to clean up fds or free memory
    exit(0);


}