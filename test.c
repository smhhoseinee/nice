#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#define SERVER_IP "10.224.79.16"  // Change to the target server's IP
#define SERVER_PORT 8080       // Change to the target server's port

void print_tcp_info(int sockfd) {
    struct tcp_info info;
    socklen_t len = sizeof(info);
    
    if (getsockopt(sockfd, IPPROTO_TCP, TCP_INFO, &info, &len) == 0) {
        printf("CWND: %u, SSTHRESH: %u\n", info.tcpi_snd_cwnd, info.tcpi_snd_ssthresh);
    } else {
        perror("getsockopt");
    }
}

int main() {
    int sockfd;
    struct sockaddr_in server_addr;

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set congestion control algorithm to "nice"
    char cc_algo[] = "nice";
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_CONGESTION, cc_algo, strlen(cc_algo)) < 0) {
        perror("setsockopt failed to set congestion control algorithm");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server. Tracking CWND and SSTHRESH...\n");

    // While sending the data, you can track cwnd and ssthresh periodically.
    // read/write code is not present.
    for (int i = 0; i < 10; i++) {
        print_tcp_info(sockfd);
        sleep(1);
    }

    close(sockfd);
    return 0;
}