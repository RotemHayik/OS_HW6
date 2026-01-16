
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
#include <fcntl.h>


#define MAX_BUFFER_SIZE 1024


int parse_args(int argc, char *argv[], struct sockaddr_in *server_addr, char **file_path) {
    
    // check the number of arguments
    if (argc != 4) {
        fprintf(stderr, "This program accepts exactly 3 arguments: server's IP address, server's port number, and a file path\n");
        return -1;
    }
    memset(server_addr, 0, sizeof(*server_addr));

    // process each argument
    // all arguments are char*
    const char *server_ip;
    server_ip = argv[1];
    // converting a string containing an IP address to binary representation
    // assumption: valid IP address is provided
    inet_pton(AF_INET, server_ip, &server_addr->sin_addr);

    // unsigned 16-bit integer for port number (given in host byte order)
    // assumption: valid port number is provided
    uint16_t server_port;
    server_port = (uint16_t)atoi(argv[2]);
    // converting port number to network byte order
    server_addr->sin_port = htons(server_port);

    server_addr->sin_family = AF_INET;

    *file_path = argv[3];

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


int transmit_data(int sockfd, int fd)
{
    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_read;

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        // starting a new buffer transmission
        ssize_t total_written = 0;

        // ensure all bytes from the current buffer are written
        while (total_written < bytes_read) {
            // write to socket
            // start from the position after the last written byte
            // the number of remaining bytes
            ssize_t bytes_written = write(sockfd, buffer + total_written, bytes_read - total_written);
            if (bytes_written < 0) {
                perror("Error writing to socket\n");
                return -1;
            }
            total_written += bytes_written;
        }
    }

    if (bytes_read < 0){
        perror("Error reading from file\n");
        return -1;}

    // if reached EOF, bytes_read == 0 and transmission is complete
    return 0;
}

int receive_data(int sockfd, unsigned int *printable_count) {
    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_received;
    *printable_count = 0;

    // when socket is closed by the server, read() returns 0 (EOF)
    while ((bytes_received = read(sockfd, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < bytes_received; i++) {
            if (buffer[i] >= 32 && buffer[i] <= 126) {
                (*printable_count)++;
            }
        }
    }

    if (bytes_received < 0) {
        perror("Error reading from socket\n");
        return -1;
    }

    return 0;
}



int main(int argc, char *argv[]) {
    
    int ret;
    int fd;
    char *file_path;
    unsigned int printable_count = 0;

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
    sockfd = TCP_connect(&server_addr);
    if (sockfd < 0) {
        exit(1);
    }

    // transmit data over the TCP connection - checking for errors
    ret = transmit_data(sockfd, fd);
    if (ret < 0) {
        close(fd);
        close(sockfd);
        exit(1);
    }

    close(fd); // close the file descriptor after transmission is complete

    // indicate the end of data transmission
    shutdown(sockfd, SHUT_WR);

    // receive data from the TCP connection - checking for errors
    ret = receive_data(sockfd, &printable_count);
    if (ret < 0) {
        close(fd);
        close(sockfd);
        exit(1);
    }   

    // print received data to standard output with printf "# of printable characters: %u\n"
    printf("# of printable characters: %u\n", printable_count);

    // close the socket
    close(sockfd);
    exit(0);

}

