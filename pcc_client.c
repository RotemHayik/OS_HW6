
#include <arpa/inet.h> // for inet_pton
#include <netinet/in.h> // for struct sockaddr_in
#include <stdio.h> // for printf, perror
#include <stdlib.h> // for atoi, exit
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>



int parse_args(int argc, char *argv[], struct sockaddr_in *server_addr, char **file_path) {
    
    // check the number of arguments
    if (argc != 4) {
        perror("This program accepts exactly 3 arguments: server's IP address, server's port number, and a file path\n");
        return -1;
    }

    // process each argument
    const char *server_ip;
    server_ip = argv[1];
    // converting a string containing an IP address to binary representation
    // assumption: valid IP address is provided
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // unsigned 16-bit integer for port number (given in host byte order)
    // assumption: valid port number is provided
    uint16_t server_port;
    server_port = (uint16_t)atoi(argv[2]);
    // converting port number to network byte order
    server_addr.sin_port = htons(server_port);

    const char *file_path;
    file_path = argv[3];

    return 0;
}

int file_open(const char *file_path) {
    int fd;
    fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        perror("Error opening file\n");
        return -1;
    }
    return fd;
}

int TCP_connect(struct sockaddr_in *server_addr) {
    int sockfd;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket\n");
        return -1;
    }
    if (connect(sockfd, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
        perror("Error connecting to server\n");
        close(sockfd);
        return -1;
    }
    return sockfd;
}


void main(int argc, char *argv[]) {
    
    int ret;
    int fd;
    char *file_path;

    // server address structure - ip+port
    struct sockaddr_in server_addr;

    // socket file descriptor for TCP connection
    int sockfd = -1; // not open


    // read arguments from command line - check the number of arguments
    ret = parse_args(argc, argv, &server_addr, &file_path);
    if (ret < 0) {
        exit(1);
    }

    // open a specified file - checking for errors
    fd = file_open(file_path);
    if (fd < 0) {
        exit(1);
    }

    // create a TCP connection to a specified server - checking for errors
    int sockfd;
    sockfd = TCP_connect(&server_addr);
    if (sockfd < 0) {
        close(fd);
        exit(1);
    }
// transmit data over the TCP connection - checking for errors

// receive data from the TCP connection - checking for errors

// print received data to standard output with printf "# of printable characters: %u\n"

// exit with 0
}

