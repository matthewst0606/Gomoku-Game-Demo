#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <netdb.h>
#include <pthread.h>
#include <crypt.h>

#define MAX_PLAYERS 10
#define TRUE 1
#define FALSE 0

typedef struct PLAYERRECORD {
    char email[51];
    char password[128];  // encrypted password
    char name[51];
    int wins;
    int losses;
    int ties;
    int active;  // 1 if slot is used, 0 if empty
} PlayerRecord;

typedef struct GAME {
    int nMoves;
    int gameOver;
    char stone;
    int x, y;
    char board[8][8];
    pthread_mutex_t lock;
    int player1_fd;
    int player2_fd;
    PlayerRecord *player1;
    PlayerRecord *player2;
    PlayerRecord *scoreboard;
    pthread_mutex_t *scoreboard_lock;
} Game;

// Global scoreboard
PlayerRecord scoreboard[MAX_PLAYERS];
pthread_mutex_t scoreboard_lock = PTHREAD_MUTEX_INITIALIZER;

// server functions
int start_server(char *hostname, char *port, int backlog);
int accept_client(int serv_sock);
void *get_in_addr(struct sockaddr * sa);
int get_server_socket(char *hostname, char *port);
void print_ip( struct addrinfo *ai);

// Thread functions
void *horizontalCheck(void *ptr);
void *verticalCheck(void *ptr);
void *diagonalCheck(void *ptr);
void *handle_game(void *ptr);

// Game functions
void initializeBoard(Game *game);
void sendBoard(Game *game, int fd);
int checkMove(Game *game);

// Authentication functions
void initialize_scoreboard();
int register_player(int client_fd);
PlayerRecord* login_player(int client_fd);
char* encrypt_password(const char *password);
PlayerRecord* find_player_by_email(const char *email);
int add_player_to_scoreboard(const char *email, const char *password, const char *name);

int main(int argc, char *argv[]) {
    int serv_socket;
    
    if (argc != 2) {
        fprintf(stderr, "Usage: %s port\n", argv[0]);
        return 1;
    }
    
    initialize_scoreboard();
    
    serv_socket = start_server(NULL, argv[1], 10);
    if (serv_socket == -1) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }
    
    printf("Server started on port %s\n", argv[1]);
    printf("Waiting for clients...\n");
    
    while (1) {
        Game *game = (Game *)malloc(sizeof(Game));
        if (game == NULL) {
            fprintf(stderr, "Memory allocation failed\n");
            continue;
        }
        
        pthread_mutex_init(&game->lock, NULL);
        game->scoreboard = scoreboard;
        game->scoreboard_lock = &scoreboard_lock;
        
        // Accept and authenticate Player 1
        printf("Waiting for Player 1...\n");
        game->player1_fd = accept_client(serv_socket);
        if (game->player1_fd < 0) {
            pthread_mutex_destroy(&game->lock);
            free(game);
            continue;
        }
        
        game->player1 = login_player(game->player1_fd);
        if (game->player1 == NULL) {
            printf("Player 1 authentication failed\n");
            close(game->player1_fd);
            pthread_mutex_destroy(&game->lock);
            free(game);
            continue;
        }
        printf("Player 1 authenticated: %s\n", game->player1->name);
        
        // Accept and authenticate Player 2
        printf("Waiting for Player 2...\n");
        game->player2_fd = accept_client(serv_socket);
        if (game->player2_fd < 0) {
            close(game->player1_fd);
            pthread_mutex_destroy(&game->lock);
            free(game);
            continue;
        }
        
        game->player2 = login_player(game->player2_fd);
        if (game->player2 == NULL) {
            printf("Player 2 authentication failed\n");
            close(game->player1_fd);
            close(game->player2_fd);
            pthread_mutex_destroy(&game->lock);
            free(game);
            continue;
        }
        printf("Player 2 authenticated: %s\n", game->player2->name);
        
        // Create thread to handle the game
        pthread_t game_thread;
        if (pthread_create(&game_thread, NULL, handle_game, (void *)game) != 0) {
            fprintf(stderr, "Failed to create game thread\n");
            close(game->player1_fd);
            close(game->player2_fd);
            pthread_mutex_destroy(&game->lock);
            free(game);
            continue;
        }
        pthread_detach(game_thread);
    }
    
    close(serv_socket);
    return 0;
}

void initialize_scoreboard() {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        scoreboard[i].active = 0;
        scoreboard[i].wins = 0;
        scoreboard[i].losses = 0;
        scoreboard[i].ties = 0;
    }
}

char* encrypt_password(const char *password) {
    // Use a fixed salt for simplicity (in production, use unique salts per user)
    static char *salt = "$6$rounds=5000$randomsaltstring";
    return crypt(password, salt);
}

PlayerRecord* find_player_by_email(const char *email) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (scoreboard[i].active && strcmp(scoreboard[i].email, email) == 0) {
            return &scoreboard[i];
        }
    }
    return NULL;
}

int add_player_to_scoreboard(const char *email, const char *password, const char *name) {
    pthread_mutex_lock(&scoreboard_lock);
    
    // Check if email already exists
    if (find_player_by_email(email) != NULL) {
        pthread_mutex_unlock(&scoreboard_lock);
        return -1;  // Email already registered
    }
    
    // Find empty slot
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!scoreboard[i].active) {
            strcpy(scoreboard[i].email, email);
            strcpy(scoreboard[i].password, password);
            strcpy(scoreboard[i].name, name);
            scoreboard[i].wins = 0;
            scoreboard[i].losses = 0;
            scoreboard[i].ties = 0;
            scoreboard[i].active = 1;
            pthread_mutex_unlock(&scoreboard_lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&scoreboard_lock);
    return -2;  // Scoreboard full
}

int register_player(int client_fd) {
    char buffer[256];
    char email[51], password[51], name[51];
    ssize_t received;
    
    // Get email
    send(client_fd, "Enter email: ", 13, 0);
    received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) return -1;
    buffer[received] = '\0';
    sscanf(buffer, "%50s", email);
    
    // Get password
    send(client_fd, "Enter password: ", 16, 0);
    received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) return -1;
    buffer[received] = '\0';
    sscanf(buffer, "%50s", password);
    
    // Get name
    send(client_fd, "Enter first name: ", 18, 0);
    received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) return -1;
    buffer[received] = '\0';
    sscanf(buffer, "%50s", name);
    
    // Encrypt password
    char *encrypted = encrypt_password(password);
    
    // Add to scoreboard
    int result = add_player_to_scoreboard(email, encrypted, name);
    
    if (result == 0) {
        send(client_fd, "Registration successful!\n", 25, 0);
        return 0;
    } else if (result == -1) {
        send(client_fd, "Email already registered!\n", 26, 0);
        return -1;
    } else {
        send(client_fd, "Scoreboard full!\n", 17, 0);
        return -1;
    }
}

PlayerRecord* login_player(int client_fd) {
    char buffer[256];
    char email[51], password[51];
    ssize_t received;
    int choice;
    
    // Ask for login or register
    send(client_fd, "1. Login\n2. Register\nChoice: ", 30, 0);
    received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) return NULL;
    buffer[received] = '\0';
    sscanf(buffer, "%d", &choice);
    
    if (choice == 2) {
        // Registration process
        if (register_player(client_fd) != 0) {
            return NULL;
        }
        // After successful registration, don't ask for choice again
        // Just proceed to login
    }
    
    // Login process
    send(client_fd, "Enter email: ", 13, 0);
    received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) return NULL;
    buffer[received] = '\0';
    sscanf(buffer, "%50s", email);
    
    send(client_fd, "Enter password: ", 16, 0);
    received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) return NULL;
    buffer[received] = '\0';
    sscanf(buffer, "%50s", password);
    
    // Verify credentials
    pthread_mutex_lock(&scoreboard_lock);
    PlayerRecord *player = find_player_by_email(email);
    
    if (player != NULL) {
        char *encrypted = encrypt_password(password);
        if (strcmp(player->password, encrypted) == 0) {
            send(client_fd, "Login successful!\n", 18, 0);
            pthread_mutex_unlock(&scoreboard_lock);
            return player;
        }
    }
    
    pthread_mutex_unlock(&scoreboard_lock);
    send(client_fd, "Invalid credentials!\n", 21, 0);
    return NULL;
}

void *handle_game(void *ptr) {
    Game *game = (Game *)ptr;
    char buffer[512];
    ssize_t received;
    
    // Send player names and opponent info
    snprintf(buffer, sizeof(buffer), "Your name: %s, Opponent name: %s\n", 
             game->player1->name, game->player2->name);
    send(game->player1_fd, buffer, strlen(buffer), 0);
    
    snprintf(buffer, sizeof(buffer), "Your name: %s, Opponent name: %s\n", 
             game->player2->name, game->player1->name);
    send(game->player2_fd, buffer, strlen(buffer), 0);
    
    // Initialize game
    game->nMoves = 0;
    game->gameOver = 0;
    game->stone = 'B';
    initializeBoard(game);
    
    // Send initial board to both players
    sendBoard(game, game->player1_fd);
    sendBoard(game, game->player2_fd);
    
    // Game loop
    while (game->gameOver == 0) {
        int current_fd = (game->stone == 'B') ? game->player1_fd : game->player2_fd;
        
        // Prompt current player
        snprintf(buffer, sizeof(buffer), "\n%c stone's turn. Enter x and y (0-7): ", game->stone);
        send(current_fd, buffer, strlen(buffer), 0);
        
        // Receive move
        received = recv(current_fd, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            close(game->player1_fd);
            close(game->player2_fd);
            pthread_mutex_destroy(&game->lock);
            free(game);
            return NULL;
        }
        buffer[received] = '\0';
        
        // Parse move
        if (sscanf(buffer, "%d %d", &game->x, &game->y) != 2) {
            send(current_fd, "Invalid input format. Try again.\n", 33, 0);
            continue;
        }
        
        // Check if move is valid
        if (checkMove(game) == 1) {
            snprintf(buffer, sizeof(buffer), "Invalid move at (%d,%d). Try again.\n", 
                     game->x, game->y);
            send(current_fd, buffer, strlen(buffer), 0);
            continue;
        }
        
        // Make move
        game->board[game->x][game->y] = game->stone;
        game->nMoves++;
        
        // Check for win
        pthread_t hThread, vThread, dThread;
        pthread_create(&hThread, NULL, horizontalCheck, (void *)game);
        pthread_create(&vThread, NULL, verticalCheck, (void *)game);
        pthread_create(&dThread, NULL, diagonalCheck, (void *)game);
        
        pthread_join(hThread, NULL);
        pthread_join(vThread, NULL);
        pthread_join(dThread, NULL);
        
        // Send updated board to both players
        sendBoard(game, game->player1_fd);
        sendBoard(game, game->player2_fd);
        
        // Check game status and update scoreboard
        pthread_mutex_lock(game->scoreboard_lock);
        
        if (game->nMoves == 64 && game->gameOver == 0) {
            game->gameOver = 2;
            game->player1->ties++;
            game->player2->ties++;
            
            snprintf(buffer, sizeof(buffer), "It was a draw\n%s: %dW/%dL/%dT - %s: %dW/%dL/%dT\n",
                     game->player1->name, game->player1->wins, game->player1->losses, game->player1->ties,
                     game->player2->name, game->player2->wins, game->player2->losses, game->player2->ties);
            send(game->player1_fd, buffer, strlen(buffer), 0);
            send(game->player2_fd, buffer, strlen(buffer), 0);
            
        } else if (game->gameOver == 1) {
            if (game->stone == 'B') {
                game->player1->wins++;
                game->player2->losses++;
                
                snprintf(buffer, sizeof(buffer), "You won and %s lost\n%s: %dW/%dL/%dT - %s: %dW/%dL/%dT\n",
                         game->player2->name,
                         game->player1->name, game->player1->wins, game->player1->losses, game->player1->ties,
                         game->player2->name, game->player2->wins, game->player2->losses, game->player2->ties);
                send(game->player1_fd, buffer, strlen(buffer), 0);
                
                snprintf(buffer, sizeof(buffer), "You lost and %s won\n%s: %dW/%dL/%dT - %s: %dW/%dL/%dT\n",
                         game->player1->name,
                         game->player1->name, game->player1->wins, game->player1->losses, game->player1->ties,
                         game->player2->name, game->player2->wins, game->player2->losses, game->player2->ties);
                send(game->player2_fd, buffer, strlen(buffer), 0);
            } else {
                game->player2->wins++;
                game->player1->losses++;
                
                snprintf(buffer, sizeof(buffer), "You won and %s lost\n%s: %dW/%dL/%dT - %s: %dW/%dL/%dT\n",
                         game->player1->name,
                         game->player2->name, game->player2->wins, game->player2->losses, game->player2->ties,
                         game->player1->name, game->player1->wins, game->player1->losses, game->player1->ties);
                send(game->player2_fd, buffer, strlen(buffer), 0);
                
                snprintf(buffer, sizeof(buffer), "You lost and %s won\n%s: %dW/%dL/%dT - %s: %dW/%dL/%dT\n",
                         game->player2->name,
                         game->player2->name, game->player2->wins, game->player2->losses, game->player2->ties,
                         game->player1->name, game->player1->wins, game->player1->losses, game->player1->ties);
                send(game->player1_fd, buffer, strlen(buffer), 0);
            }
        } else {
            game->stone = (game->stone == 'W') ? 'B' : 'W';
        }
        
        pthread_mutex_unlock(game->scoreboard_lock);
    }
    
    close(game->player1_fd);
    close(game->player2_fd);
    pthread_mutex_destroy(&game->lock);
    free(game);
    return NULL;
}

void initializeBoard(Game *game) {
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            game->board[i][j] = '.';
        }
    }
}

void sendBoard(Game *game, int fd) {
    char buffer[1024];
    int offset = 0;
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n  0 1 2 3 4 5 6 7\n");
    for (int i = 0; i < 8; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%d ", i);
        for (int j = 0; j < 8; j++) {
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%c ", game->board[i][j]);
        }
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n");
    }
    send(fd, buffer, strlen(buffer), 0);
}

int checkMove(Game *game) {
    if (game->x < 0 || game->x >= 8 || game->y < 0 || game->y >= 8) {
        return 1;
    }
    if (game->board[game->x][game->y] != '.') {
        return 1;
    }
    return 0;
}

void *horizontalCheck(void *ptr) {
    Game *game = (Game *)ptr;
    int count = 0;
    
    for (int j = 0; j < 8; j++) {
        if (game->board[game->x][j] == game->stone) {
            count++;
            if (count == 5) {
                game->gameOver = 1;
                return NULL;
            }
        } else {
            count = 0;
        }
    }
    return NULL;
}

void *verticalCheck(void *ptr) {
    Game *game = (Game *)ptr;
    int count = 0;
    
    for (int i = 0; i < 8; i++) {
        if (game->board[i][game->y] == game->stone) {
            count++;
            if (count == 5) {
                game->gameOver = 1;
                return NULL;
            }
        } else {
            count = 0;
        }
    }
    return NULL;
}

void *diagonalCheck(void *ptr) {
    Game *game = (Game *)ptr;
    int count;
    
    // Check all diagonals (top-left to bottom-right)
    for (int start = 0; start < 8; start++) {
        // Diagonals starting from top row
        count = 0;
        for (int i = 0, j = start; i < 8 && j < 8; i++, j++) {
            if (game->board[i][j] == game->stone) {
                count++;
                if (count == 5) {
                    game->gameOver = 1;
                    return NULL;
                }
            } else {
                count = 0;
            }
        }
        
        // Diagonals starting from left column (skip 0,0 as it's already covered)
        if (start > 0) {
            count = 0;
            for (int i = start, j = 0; i < 8 && j < 8; i++, j++) {
                if (game->board[i][j] == game->stone) {
                    count++;
                    if (count == 5) {
                        game->gameOver = 1;
                        return NULL;
                    }
                } else {
                    count = 0;
                }
            }
        }
    }
    
    // Check all diagonals (top-right to bottom-left)
    for (int start = 0; start < 8; start++) {
        // Diagonals starting from top row
        count = 0;
        for (int i = 0, j = start; i < 8 && j >= 0; i++, j--) {
            if (game->board[i][j] == game->stone) {
                count++;
                if (count == 5) {
                    game->gameOver = 1;
                    return NULL;
                }
            } else {
                count = 0;
            }
        }
        
        // Diagonals starting from right column (skip 0,7 as it's already covered)
        if (start > 0) {
            count = 0;
            for (int i = start, j = 7; i < 8 && j >= 0; i++, j--) {
                if (game->board[i][j] == game->stone) {
                    count++;
                    if (count == 5) {
                        game->gameOver = 1;
                        return NULL;
                    }
                } else {
                    count = 0;
                }
            }
        }
    }
    
    return NULL;
}

int get_server_socket(char *hostname, char *port) {
    struct addrinfo hints, *servinfo, *p;
    int status;
    int server_socket;
    int yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((status = getaddrinfo(hostname, port, &hints, &servinfo)) != 0) {
        printf("getaddrinfo: %s\n", gai_strerror(status));
        exit(1);
    }

    for (p = servinfo; p != NULL; p = p ->ai_next) {
        if ((server_socket = socket(p->ai_family, p->ai_socktype,
                            p->ai_protocol)) == -1) {
            printf("socket socket \n");
            continue;
        }
        
        if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            printf("socket option\n");
            continue;
        }

        if (bind(server_socket, p->ai_addr, p->ai_addrlen) == -1) {
            printf("socket bind \n");
            continue;
        }
        break;
    }
    print_ip(servinfo);
    freeaddrinfo(servinfo);

    return server_socket;
}

int start_server(char *hostname, char *port, int backlog) {
    int status = 0;
    int serv_socket = get_server_socket(hostname, port);
    
    if ((status = listen(serv_socket, backlog)) == -1) {
        printf("socket listen error\n");
    }
    return serv_socket;
}

int accept_client(int serv_sock) {
    int reply_sock_fd = -1;
    socklen_t sin_size = sizeof(struct sockaddr_storage);
    struct sockaddr_storage client_addr;
    char client_printable_addr[INET6_ADDRSTRLEN];

    if ((reply_sock_fd = accept(serv_sock, 
            (struct sockaddr *)&client_addr, &sin_size)) == -1) {
        printf("socket accept error\n");
    }
    else {
        inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr *)&client_addr), 
                    client_printable_addr, sizeof client_printable_addr);
        printf("server: connection from %s at port %d\n", client_printable_addr,
                   ((struct sockaddr_in*)&client_addr)->sin_port);
    }
    return reply_sock_fd;
}

void print_ip( struct addrinfo *ai) {
    struct addrinfo *p;
    void *addr;
    char *ipver;
    char ipstr[INET6_ADDRSTRLEN];
    struct sockaddr_in *ipv4;
    struct sockaddr_in6 *ipv6;
    short port = 0;

    for (p = ai; p !=  NULL; p = p->ai_next) {
        if (p->ai_family == AF_INET) {
            ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
            port = ipv4->sin_port;
            ipver = "IPV4";
        }
        else {
            ipv6= (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
            port = ipv4->sin_port;
            ipver = "IPV6";
        }
        inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
        printf("serv ip info: %s - %s @%d\n", ipstr, ipver, ntohs(port));
    }
}

void *get_in_addr(struct sockaddr * sa) {
    if (sa->sa_family == AF_INET) {
        printf("ipv4\n");
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }
    else {
        printf("ipv6\n");
        return &(((struct sockaddr_in6 *)sa)->sin6_addr);
    }
}
