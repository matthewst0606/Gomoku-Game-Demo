## Features
- Multithreaded TCP client-server Gomoku implementation
- Concurrent game sessions using POSIX threads (`pthread`)
- Thread-safe shared scoreboard using mutex locks
- Win detection (horizontal, vertical, diagonal checks)


## Technologies Used
- C
- POSIX Threads (`pthread`)
- BSD Sockets (TCP)
- Password Hashing via `crypt()`
- Mutex Synchronization


## Architecture
- The server listens for incoming TCP connections.
- Players authenticate via login or registration.
- Two authenticated players are paired into a game session.
- Each game runs in a dedicated thread.
- Win detection logic validates moves and updates game state accordingly.
- A global scoreboard is synchronized using a mutex to ensure thread safety.


## Instructions

### Prerequisite
- Connect to AWS VPN (if using the remote server environment)
  
### Server Side

```bash
ssh <username>@<server-ip>

cd gomoku-server
gcc -o gomoku-server gomoku-server.c -lpthread -lcrypt
./gomoku-server <port>
```

### Client Side
```bash
cd gomoku
docker compose run --rm --name client1 client

gcc -o gomoku-client gomoku-client.c
./gomoku-client <server-ip> <port>
```

## Credits
- This team project was developed by three students at the University of Scranton.
