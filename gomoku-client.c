#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

int get_server_connection(char *hostname, char *port);
void print_ip(struct addrinfo *ai);

int main(int argc, char *argv[]) {
    ssize_t sent, received;
    char buffer[512];
    int sockfd;

    if (argc != 3) {
        fprintf(stderr, "arg requirement: %s hostname port#\n", argv[0]);
        return 1;
    }

    sockfd = get_server_connection(argv[1], argv[2]);
    if (sockfd < 0) {
        perror("connection failed!");
        return 1;
    }

    printf("Connected to server.\n");

    // Login or Register
    received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        perror("recv failed");
        close(sockfd);
        return 1;
    }
    buffer[received] = '\0';
    printf("%s", buffer);

    // Send choice
    int choice;
    scanf("%d", &choice);
    snprintf(buffer, sizeof(buffer), "%d", choice);
    sent = send(sockfd, buffer, strlen(buffer), 0);
    if (sent == -1) {
        perror("send failed");
        close(sockfd);
        return 1;
    }

    if (choice == 2) {
        // Registration flow
        // Email
        received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            perror("recv failed");
            close(sockfd);
            return 1;
        }
        buffer[received] = '\0';
        printf("%s", buffer);

        char email[51];
        scanf("%50s", email);
        sent = send(sockfd, email, strlen(email), 0);
        if (sent == -1) {
            perror("send failed");
            close(sockfd);
            return 1;
        }

        // Password
        received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            perror("recv failed");
            close(sockfd);
            return 1;
        }
        buffer[received] = '\0';
        printf("%s", buffer);

        char password[51];
        scanf("%50s", password);
        sent = send(sockfd, password, strlen(password), 0);
        if (sent == -1) {
            perror("send failed");
            close(sockfd);
            return 1;
        }

        // First name
        received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            perror("recv failed");
            close(sockfd);
            return 1;
        }
        buffer[received] = '\0';
        printf("%s", buffer);

        char name[51];
        scanf("%50s", name);
        sent = send(sockfd, name, strlen(name), 0);
        if (sent == -1) {
            perror("send failed");
            close(sockfd);
            return 1;
        }

        // Registration result
        received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            perror("recv failed");
            close(sockfd);
            return 1;
        }
        buffer[received] = '\0';
        printf("%s", buffer);

        // Check if registration failed
        if (strstr(buffer, "already registered") != NULL || 
            strstr(buffer, "full") != NULL) {
            close(sockfd);
            return 1;
        }

        // After successful registration, continue to login (no extra prompt)
    }

    // Login flow (for both new registrations and existing users)
    // Email
    received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        perror("recv failed");
        close(sockfd);
        return 1;
    }
    buffer[received] = '\0';
    printf("%s", buffer);

    char email[51];
    scanf("%50s", email);
    sent = send(sockfd, email, strlen(email), 0);
    if (sent == -1) {
        perror("send failed");
        close(sockfd);
        return 1;
    }

    // Password
    received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        perror("recv failed");
        close(sockfd);
        return 1;
    }
    buffer[received] = '\0';
    printf("%s", buffer);

    char password[51];
    scanf("%50s", password);
    sent = send(sockfd, password, strlen(password), 0);
    if (sent == -1) {
        perror("send failed");
        close(sockfd);
        return 1;
    }

    // Login result
    received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        perror("recv failed");
        close(sockfd);
        return 1;
    }
    buffer[received] = '\0';
    printf("%s", buffer);

    // Check if login failed
    if (strstr(buffer, "Invalid") != NULL) {
        close(sockfd);
        return 1;
    }

    // Receive player name and opponent info
    received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        perror("recv failed");
        close(sockfd);
        return 1;
    }
    buffer[received] = '\0';
    printf("%s", buffer);

    // Game loop
    while (1) {
        // Receive board or prompt
        received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            printf("Connection closed by server\n");
            break;
        }
        buffer[received] = '\0';
        printf("%s", buffer);
        
        // Check if game is over (look for final scores in format "XW/YL/ZT")
        if (strstr(buffer, "W/") != NULL && strstr(buffer, "L/") != NULL && 
            strstr(buffer, "T") != NULL) {
            break;
        }
        
        // If it's a turn prompt, send move
        if (strstr(buffer, "turn") != NULL) {
            int x, y;
            if (scanf("%d", &x) != 1) {
                fprintf(stderr, "Invalid input\n");
                continue;
            }
            if (scanf("%d", &y) != 1) {
                fprintf(stderr, "Invalid input\n");
                continue;
            }
            
            snprintf(buffer, sizeof(buffer), "%d %d", x, y);
            sent = send(sockfd, buffer, strlen(buffer), 0);
            if (sent == -1) {
                perror("send failed");
                break;
            }
        }
    }

    close(sockfd);
    return 0;
}

int get_server_connection(char *hostname, char *port) {
    int serverfd;
    struct addrinfo hints, *servinfo, *p;
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(hostname, port, &hints, &servinfo)) != 0) {
        printf("getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }

    print_ip(servinfo);
    for (p = servinfo; p != NULL; p = p->ai_next) {
        // create a socket
        if ((serverfd = socket(p->ai_family, p->ai_socktype,
                            p->ai_protocol)) == -1) {
            printf("socket socket \n");
            continue;
        }

        // connect to the server
        if ((status = connect(serverfd, p->ai_addr, p->ai_addrlen)) == -1) {
            close(serverfd);
            printf("socket connect \n");
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo);
   
    if (status != -1) return serverfd;
    else return -1;
}

void print_ip(struct addrinfo *ai) {
    struct addrinfo *p;
    void *addr;
    char *ipver;
    char ipstr[INET6_ADDRSTRLEN];
    struct sockaddr_in *ipv4;
    struct sockaddr_in6 *ipv6;
    short port = 0;

    for (p = ai; p != NULL; p = p->ai_next) {
        if (p->ai_family == AF_INET) {
            ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
            port = ipv4->sin_port;
            ipver = "IPV4";
        }
        else {
            ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
            port = ipv6->sin6_port;
            ipver = "IPV6";
        }
        inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
        printf("serv ip info: %s - %s @%d\n", ipstr, ipver, ntohs(port));
    }
}
