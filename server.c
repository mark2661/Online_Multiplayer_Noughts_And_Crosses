#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>

#define PORT "3490"
#define BACKLOG 10
#define MAX_ROW 3
#define MAX_COL 3
#define WINNING_SCORE MAX_COL
#define PLAYER_ONE_GRID_MARKER 1
#define PLAYER_TWO_GRID_MARKER -1
#define SERVER_MESSAGE_LENGTH (MAX_ROW*MAX_COL) + 1
#define SERVER_MESSAGE_LENGTH_BYTES sizeof(int)*(SERVER_MESSAGE_LENGTH)

// DEBUG Functions
void printGrid(int grid[3][3]);

typedef enum ServerMessageCode{
    INVALID = -1,
    WAITING_FOR_OPPONENT,
    OPPONENT_DISSCONNECTED,
    GAME_DATA_UPDATE,
    GAME_OVER_WIN,
    GAME_OVER_LOSS,
    GAME_OVER_DRAW
} ServerMessageCode;

typedef enum Bool{
    False,
    True
} Bool;

enum GameResult{
    GAME_IN_PROGRESS,
    P1WIN,
    P2WIN,
    DRAW
};

enum Player{
    PLAYER_NONE,
    PLAYER_ONE,
    PLAYER_TWO
};

typedef struct{
    int grid[3][3];
} Game;

typedef struct{
    int* array;
    int* size;
}HeapArrayInt;

typedef struct{
    int row;
    int col;
}ClientInput;

Bool updateGameGrid(Game* game, ClientInput clientInput, int value)
{
    int row = clientInput.row;
    int col = clientInput.col;

    if(row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL) { return False; }
    // only set grid cell if it hasn't alredy been set. i.e it equals zero.
    if(game->grid[row][col] == 0)
    {
        game->grid[row][col] = value;
        return True;
    }
    // grid cell already set -> invalid update
    else{ return False; }

    return True;
}

void resetGameGrid(Game* game)
{
    for(size_t row=0; row<MAX_ROW; row++)
    {
        for(size_t col=0; col<MAX_COL; col++)
        {
            game->grid[row][col] = 0;
        }
    }
}

enum GameResult isGameOver(Game* game)
{
    // TODO: Improve this function in the future tell notify which player won
    // could use an enum with a set of return codes 0 indicates game not over (False value), 
    // 1 = player1 has won, 2 = player2 has won, 3 = draw (All True values)


    // Check horizontal win conditions
    for(size_t row=0; row<MAX_ROW; row++)
    {
        int sum = game->grid[row][0] + game->grid[row][1] + game->grid[row][2];
        if(abs(sum) == WINNING_SCORE) { return (sum == 3) ? P1WIN : P2WIN; }
    }

    // Check vertical win conditions
    for (size_t col = 0; col < MAX_COL; col++)
    {
        int sum = game->grid[0][col] + game->grid[1][col] + game->grid[2][col];
        if(abs(sum) == WINNING_SCORE) { return (sum == 3) ? P1WIN : P2WIN; }
    }

    // Check diagonal win conditons
    int l2rDiagonal = game->grid[0][0] + game->grid[1][1] + game->grid[2][2];
    if(abs(l2rDiagonal) == 3) { return (l2rDiagonal == 3) ? P1WIN : P2WIN; }
    
    int r2lDiagonal = game->grid[0][2] + game->grid[1][1] + game->grid[0][2];
    if(abs(r2lDiagonal) == 3) { return (r2lDiagonal == 3) ? P1WIN : P2WIN; }

    //check draw condtion
    for (size_t row = 0; row < MAX_ROW; row++)
    {
        for (size_t col = 0; col < MAX_COL; col++)
        {
            if(game->grid[row][col] == 0) { return GAME_IN_PROGRESS;}
        }
    }
    
    // Draw -> No win condtion found but grid is full (no zero cells left)
    return DRAW;
}

HeapArrayInt getGameGridInNetworkByteOrder(Game* game)
{
    int* size = (int*)malloc(sizeof(int));
    *size = sizeof(int)*(MAX_ROW*MAX_COL);
    int* noGameGrid = (int*)malloc(*size);
    for(size_t row=0; row<MAX_ROW; row++)
    {
        for(size_t col=0; col<MAX_COL; col++)
        {
            noGameGrid[col + MAX_COL*row] = htonl(game->grid[row][col]);
        }
    }
    HeapArrayInt arr = {noGameGrid, size};
    return arr;
}

void freeHeapArrayInt(HeapArrayInt* arr)
{
    free(arr->array);
    free(arr->size);
}

int sendClientUpdate(int client, HeapArrayInt noGameGrid)
{
    // TODO: may have to refactor this it may be a little messy and hard to follow
    int noGameGridWithSererMessageCode[*(noGameGrid.size)+1];  
    // prepend server message code
    noGameGridWithSererMessageCode[0] = htonl(GAME_DATA_UPDATE);
    memcpy(noGameGridWithSererMessageCode+1, noGameGrid.array, *(noGameGrid.size));
    // int s = send(client, noGameGrid.array, *(noGameGrid.size), 0);
    int s = send(client, noGameGridWithSererMessageCode, (*(noGameGrid.size)+(1*sizeof(int))), 0);
    if (s == -1)
    {
        perror("send");
        exit(1);
    }
    return s;
}

void sendGameOverUpdate(int clients[2], enum Player winner)
{
    int playerOneMessege[SERVER_MESSAGE_LENGTH];
    int playerTwoMessege[SERVER_MESSAGE_LENGTH];
    if (winner == PLAYER_ONE)
    {
        playerOneMessege[0] = htonl(GAME_OVER_WIN);
        playerTwoMessege[0] = htonl(GAME_OVER_LOSS);
    }
    else if(winner == PLAYER_TWO)
    {
        playerOneMessege[0] = htonl(GAME_OVER_LOSS);
        playerTwoMessege[0] = htonl(GAME_OVER_WIN);
    }
    else // DRAW
    {
        playerOneMessege[0] = htonl(GAME_OVER_DRAW);
        playerTwoMessege[0] = htonl(GAME_OVER_DRAW);
    }

    // pad remainder of client messages with zeros to fit the server message length
    memset(playerOneMessege + 1, 0, SERVER_MESSAGE_LENGTH_BYTES-(1*sizeof(int)));
    memset(playerTwoMessege + 1, 0, SERVER_MESSAGE_LENGTH_BYTES-(1*sizeof(int)));

    int s1 = send(clients[0], playerOneMessege, SERVER_MESSAGE_LENGTH_BYTES, 0);
    if (s1 == -1)
    {
        perror("sendGameOverUpdate:send");
        exit(1);
    }

    int s2 = send(clients[1], playerTwoMessege, SERVER_MESSAGE_LENGTH_BYTES, 0);
    if (s2 == -1)
    {
        perror("sendGameOverUpdate:send");
        exit(1);
    }
}

ClientInput getClientInput(int client)
{
    int r, c;
    ClientInput cInput;
    if (recv(client, &r, sizeof(int), 0) == -1)
    {
        perror("recv:r");
        exit(1);
    }
    if (recv(client, &c, sizeof(int), 0) == -1)
    {
        perror("recv:c");
        exit(1);
    }
    cInput.row = ntohl(r);
    cInput.col = ntohl(c);
    return cInput;
}

void rejectClientInput(int client)
{
    // get the client input but ignore (and don't return) it.
    getClientInput(client);
}

void sigchild_handler(int s)
{
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

void* get_in_addr(struct sockaddr* sa)
{
    if(sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void)
{
    int sockfd, client1, client2;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage client1_addr, client2_addr;
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;


    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my ip

    rv = getaddrinfo(NULL, PORT, &hints, &servinfo);
    if(rv != 0)
    {
        perror("server: socket");
        return 1;
    }

    // loop through all results and bind to the first one we can
    for(p = servinfo; p!=NULL; p=p->ai_next)
    {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if(sockfd == -1)
        {
            perror("server: socket");
            continue;
        }

        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
        {
            perror("setsockopt");
            exit(1);
        }

        if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo);

    if(p == NULL)
    {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if(listen(sockfd, BACKLOG) == -1)
    {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchild_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if(sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");


    while(1)
    {
        sin_size = sizeof client1_addr;
        client1 = accept(sockfd, (struct sockaddr*)&client1_addr, &sin_size);
        if(client1 == -1)
        {
            perror("accept");
            continue;
        }
        inet_ntop(client1_addr.ss_family, get_in_addr((struct sockaddr*)&client1_addr), s, sizeof s);
        printf("Server: got connection from %s\n", s);

        client2 = accept(sockfd, (struct sockaddr *)&client2_addr, &sin_size);
        if(client2 == -1)
        {
            perror("accept");
            continue;
        }
        inet_ntop(client2_addr.ss_family, get_in_addr((struct sockaddr*)&client2_addr), s, sizeof s);
        printf("Server: got connection from %s\n", s);
        printf("Starting Game!\n");

        if(!fork())
        {
            close(sockfd);
            int quit = 0;
            Game game;
            // Zero out game grid
            resetGameGrid(&game);
            Bool playerOneTurn = True;
            struct pollfd pfds[2];
            pfds[0].fd = client1;
            pfds[1].fd = client2;
            pfds[0].events = POLLIN;
            pfds[1].events = POLLIN;
            int fd_count = 2;
            

            while(!quit)
            {
                ClientInput c1Input;
                ClientInput c2Input;
                HeapArrayInt noGameGrid;

                // poll indefinetly until data recived from one of the client sockets
                int poll_count = poll(pfds, fd_count, -1);
                if(poll_count == -1)
                {
                    perror("poll");
                    exit(1);
                }

                // loop over poll results and check for data from client sockets 
                for(size_t i=0; i<fd_count; i++)
                {
                    if(pfds[i].revents & POLLIN)
                    {
                        if(playerOneTurn)
                        {
                            if (pfds[i].fd == client1)
                            {
                                // process client1 input and switch turn to client2
                                c1Input = getClientInput(client1);
                                Bool successfulUpdate = updateGameGrid(&game, c1Input, PLAYER_ONE_GRID_MARKER);
                                // printf("Is valid update p1: %d\n", successfulUpdate);
                                if (successfulUpdate)
                                {
                                    noGameGrid = getGameGridInNetworkByteOrder(&game);
                                    // (1)TODO: need to design a protocol for sending messages so the server and
                                    // client can send different types of messages to one another.
                                    sendClientUpdate(client1, noGameGrid);
                                    sendClientUpdate(client2, noGameGrid);
                                    freeHeapArrayInt(&noGameGrid);

                                    // Switch turn to other player
                                    playerOneTurn = !playerOneTurn;
                                }
                            }

                            // reject client2's inputs whilst it is client1's turn
                            else if (pfds[i].fd == client2) { rejectClientInput(client2); }
                        }

                        else if(!playerOneTurn)
                        {
                            if (pfds[i].fd == client2)
                            {
                                // process client2 input and switch turn to client1
                                c2Input = getClientInput(client2);
                                Bool successfulUpdate = updateGameGrid(&game, c2Input, PLAYER_TWO_GRID_MARKER);
                                if (successfulUpdate)
                                {
                                    noGameGrid = getGameGridInNetworkByteOrder(&game);
                                    sendClientUpdate(client1, noGameGrid);
                                    sendClientUpdate(client2, noGameGrid);
                                    freeHeapArrayInt(&noGameGrid);

                                    // Switch turn to other player
                                    playerOneTurn = !playerOneTurn;
                                }
                               
                            }
                            // reject client1's inputs whilst it is client2's turn
                            else if (pfds[i].fd == client1) { rejectClientInput(client1); }
                        }

                        enum GameResult current_game_status = isGameOver(&game);
                        if(current_game_status)
                        {
                            int c[2] = {client1, client2};
                            switch (current_game_status)
                            {
                                // (2)TODO: handle server shutdown, end game, and notify clients of results (see (1))
                                // need to also notify loseing player
                            case P1WIN:
                                printf("Game over! Player 1 won\n");
                                sendGameOverUpdate(c, PLAYER_ONE);
                                break;
                            case P2WIN: 
                                printf("Game over! Player 2 won\n");
                                sendGameOverUpdate(c, PLAYER_TWO);
                                break;
                            case DRAW: 
                                printf("Game over! Draw\n");
                                sendGameOverUpdate(c, PLAYER_NONE);
                                break;
                            default:
                                break;
                            }
                        }

                    }
                }
            }
            close(client1);
            close(client2);
            exit(0);
        }
        close(client1);
        close(client2);
    }

    // return 0;
}
// DEBUG Functions
void printGrid(int grid[3][3])
{
    for (size_t row = 0; row < MAX_ROW; row++)
    {
        printf("[ ");
        for (size_t col = 0; col < MAX_COL; col++)
        {
            printf("%d ", grid[row][col]);
        }
        printf("]\n");
    }
    printf("***********************************************\n");
}