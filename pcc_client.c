
#include <arpa/inet.h> // for inet_pton
#include <netinet/in.h> // for struct sockaddr_in
#include <stdio.h> // for printf, perror
#include <stdlib.h> // for atoi, exit
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h> // for fstat
#include <stdint.h> // for uint32_t, uint16_t


#define MAX_BUFFER_SIZE 16384 // 16 KB < 1MB


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
    if(inet_pton(AF_INET, server_ip, &server_addr->sin_addr) <= 0) {
        perror("Invalid IP address format");
        return -1;
    }

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
        perror("Error opening file");
        return -1;
    }
    return fd;
}

int TCP_connect(struct sockaddr_in *server_addr) {
    int sockfd;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket");
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
        perror("Error connecting to server");
        close(sockfd);
        return -1;
    }
    return sockfd;
}


int transmit_data(int sockfd, int fd)
{
    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_read;

    // get file size
   off_t file_size;

    file_size = lseek(fd, 0, SEEK_END);
    if (file_size < 0) {
        perror("Error seeking to end of file");
        return -1;
    }

    // return file offset back to start 
    if (lseek(fd, 0, SEEK_SET) < 0) {
        perror("Error returning to start of file");
        return -1;
    }

    uint32_t file_size_N = (uint32_t) file_size;
    uint32_t net_file_size_N = htonl(file_size_N);


    // send the file size first
    size_t total_written = 0;
    while (total_written < sizeof(net_file_size_N)) {
        ssize_t n = write(sockfd,((char *)&net_file_size_N) + total_written,sizeof(net_file_size_N) - total_written);
        if (n < 0) {
            perror("Error writing file size to socket");
            return -1;
        }
        total_written += n;
    }

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
                perror("Error writing to socket");
                return -1;
            }
            total_written += bytes_written;
        }
    }

    if (bytes_read < 0){
        perror("Error reading from file");
        return -1;}

    // if reached EOF, bytes_read == 0 and transmission is complete
    return 0;
}

int receive_data(int sockfd, uint32_t *printable_count) {
    uint32_t result_net;
    size_t total_read = 0;

    // assumption: the number of printable characters is 4 bytes
    // therefore, loop until we read 4 bytes into result_net
    while (total_read < sizeof(result_net)) {
        ssize_t ret = read(sockfd,((char *)&result_net) + total_read,sizeof(result_net) - total_read);
        if (ret < 0) {
            perror("Error reading result from server");
            return -1;
        }
        if (ret == 0) {
            fprintf(stderr, "Connection closed before receiving complete result\n");
            return -1;
        }
        total_read += ret;
    }

    // convert from network byte order to host byte order
    *printable_count = ntohl(result_net);
    return 0;
}



int main(int argc, char *argv[]) {
    
    int ret;
    int fd;
    char *file_path;
    uint32_t printable_count = 0;

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

