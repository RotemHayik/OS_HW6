#include <signal.h>
#include <stdint.h> // for uint32_t, uint16_t
#include <stdio.h> // for printf, perror
#include <stdlib.h> // for atoi, exit


#define CLIENT_OK        0
#define CLIENT_FAILED   -2
#define SERVER_STOP     -1



// global variables
uint32_t pcc_total[128];
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
    fprintf(stderr,"This program accepts exactly 1 arguments: server's IP address\n");
       return -1;
    }

    memset(server_addr, 0, sizeof(struct sockaddr_in));
    server_addr->sin_family = AF_INET;
    server_addr->sin_addr.s_addr = INADDR_ANY;
    // argv[1] is the port number assumed to be a 16-bit unsigned integer
    uint16_t port = (uint16_t)atoi(argv[1]);
    server_addr->sin_port = htons(port);

    return 0;
}

// -------------------------------------------------------------

int TCP_listen(struct sockaddr_in *server_addr) {
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("Error creating socket\n");
        return -1;
    }
        
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(listenfd, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
        perror("Error binding socket\n");
        close(listenfd);
        return -1;
    }

    if (listen(listenfd, 10) < 0) {
        perror("Error listening on socket\n");
        close(listenfd);
        return -1;
    }

    return listenfd;
}

// -------------------------------------------------------------

int client_connection(int listenfd) {
    // accept a TCP connection from a client
    struct sockaddr_in peer_addr;
    socklen_t addrsize = sizeof(peer_addr);
    int connfd = accept(listenfd, (struct sockaddr *)&peer_addr, &addrsize);
    if (connfd < 0) {
    if (errno == EINTR && sigint_received) {
        return SERVER_STOP;
    }
    perror("Error accepting connection");
    return CLIENT_FAILED;
    }


    // finish processing the client connection, even if SIGINT is received

    // ----------------get file size from the client 
    uint32_t net_file_size_N;
    size_t total_read = 0;
    while (total_read < sizeof(net_file_size_N)) {
        ssize_t n = read(connfd,
                         ((char *)&net_file_size_N) + total_read,
                         sizeof(net_file_size_N) - total_read);
        if (n < 0) {
            perror("Error reading file size from connection\n");
            close(connfd);
            return CLIENT_FAILED;
        }
        if (n == 0) {
            fprintf(stderr, "Client disconnected while sending file size\n");
            close(connfd);
            return CLIENT_FAILED;
        }
        total_read += n;
    }
    // convert from network byte order to host byte order
    uint32_t file_size_N = ntohl(net_file_size_N);

    
    // ----------------count printable characters from the client
    uint32_t client_characters_count[128];
    uint32_t printable_count = 0;
    memset(client_characters_count, 0, sizeof(client_characters_count));

    // read bytes according to file_size_N from the connection
    // and update client_characters_count
    char buffer[16384];
    size_t total_bytes_read = 0;

    while (total_bytes_read < file_size_N) {
        size_t remaining = file_size_N - total_bytes_read;
        size_t to_read = remaining < sizeof(buffer) ? remaining : sizeof(buffer);

        ssize_t bytes_read = read(connfd, buffer, to_read);
        if (bytes_read < 0) {
            perror("Error reading from connection\n");
            close(connfd);
            return CLIENT_FAILED;
        }
        if (bytes_read == 0) {
            fprintf(stderr, "Client disconnected before sending all data\n");
            close(connfd);
            return CLIENT_FAILED;
        }

        for (ssize_t i = 0; i < bytes_read; i++) {
            // char might be signed, so convert to unsigned char first
            unsigned char ch = (unsigned char)buffer[i];
            if (ch >= 32 && ch <= 126) {
                // no need to normalize index since ch is already in [0,127]
                client_characters_count[ch]++;
                printable_count++;
            }
        }

        total_bytes_read += bytes_read;
    }

    // ---------------- send the printable characters count back to the client
    uint32_t printable_count_net = htonl(printable_count);
    size_t total_written = 0;
    while (total_written < sizeof(printable_count_net)) {
        ssize_t n = write(connfd,
                          ((char *)&printable_count_net) + total_written,
                          sizeof(printable_count_net) - total_written);
        if (n < 0) {
            perror("Error writing printable count to connection\n");
            close(connfd);
            return CLIENT_FAILED;
        }
        total_written += n;
    }


    // -------------- client was served successfully
    clients_served++;


    // -------------  update the total counts
    for (int i = 0; i < 128; i++) {
        pcc_total[i] += client_characters_count[i];
    }

    close(connfd);
    return 0;
}



int main(int argc, char *argv[]) {

    // initialize a data structure to count how many times each ASCII character was observed
    // in all client connections
    memset(pcc_total, 0, sizeof(pcc_total));

    struct sockaddr_in server_addr; 

    int ret = -1;

    // handle SIGINT signal
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);
    
    // read arguments from command line - check the number of arguments
    ret = parse_args(argc, argv, &server_addr);
    if (ret < 0) {
        exit(1);
    }

    // listen for incoming TCP connections 
    int listenfd = TCP_listen(&server_addr);
    if (listenfd < 0) {
        exit(1);
    }

    // accept a TCP connection from a client and count printable characters
    // update the data structure with the count 

    // as long as SIGINT is not received, keep accepting new clients
    while (!sigint_received) {
        ret = client_connection(listenfd);      
        if (ret == CLIENT_FAILED) {
            // client failed, server continues
            continue;
        }

        if (ret == SERVER_STOP) {
            break; // SIGINT received, stop accepting new clients
        }
    }


    // print character statistics
    for (int i = 32; i < 127; i++) { 
        if (pcc_total[i] > 0) {
            // prints the ASCII character itself
            printf("char '%c' : %u times\n", i, pcc_total[i]);
        }
    }    
    printf("Served %u client(s) successfully\n", clients_served);

    // clean up and exit
    // assumption: no need to clean up fds or free memory
    exit(0);


}