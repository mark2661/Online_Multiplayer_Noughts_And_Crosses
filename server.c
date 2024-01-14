#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include "globals.h"

#define BACKLOG 10

// globals
time_t t;
struct tm tm;
char c1addr[INET6_ADDRSTRLEN];
char c2addr[INET6_ADDRSTRLEN];

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

typedef enum ServerLogCode{
    SERVER_LOG_CODE_ERROR,
    SERVER_LOG_CODE_CONNECTION_RECIEVED,
    SERVER_LOG_CODE_CONNECTION_LOST,
    SERVER_LOG_CODE_GAME_STARTED,
    SERVER_LOG_CODE_GAME_ENDED,
    SERVER_LOG_CODE_GAME_UPDATED,
    SERVER_LOG_CODE_MESSEGE_RECIEVED,
    SERVER_LOG_CODE_MESSEGE_SENT
}ServerLogCode;

typedef struct{
    int grid[MAX_ROW][MAX_COL];
} Game;

typedef struct{
    int* array;
    int* size;
}HeapArrayInt;

typedef struct{
    int row;
    int col;
}ClientInput;

// DEBUG Functions
void printGrid(int grid[3][3]);

void sendGameStartMessage(int clientsFD[], size_t clientsFDLength);
void sendWaitingForOpponentMessage(int clientFD);
void sendOpponentDisconnectedMessage(int clientFD);
Bool updateServerLog(FILE* log, ServerLogCode code, char* ip_address);
Bool appendMessageToServerLog(FILE* log, char* message);
Bool appendErrorMessageToServerLog(FILE* log);
Bool appendConnectionRecievedMessageToServerLog(FILE* log, char* IPv4_address);
Bool appendConnectionLostMessageToServerLog(FILE* log, char* IPv4_address);
Bool appendGameStartedMessageToServerLog(FILE* log);
Bool appendGameEndedMessageToServerLog(FILE* log);
Bool appendGameUpdatedMessageToServerLog(FILE* log);
Bool appendGameMessageRecievedMessageToServerLog(FILE* log, char* IPv4_adress);
Bool appendGameMessageSentMessageToServerLog(FILE* log, char* IPv4_adress);


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
    
    int r2lDiagonal = game->grid[0][2] + game->grid[1][1] + game->grid[2][0];
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
    // prepend server message code in network byte order
    noGameGridWithSererMessageCode[0] = htonl(SERVER_MESSAGE_CODE_GAME_DATA_UPDATE);
    memcpy(noGameGridWithSererMessageCode+1, noGameGrid.array, *(noGameGrid.size));
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
        playerOneMessege[0] = htonl(SERVER_MESSAGE_CODE_GAME_OVER_WIN);
        playerTwoMessege[0] = htonl(SERVER_MESSAGE_CODE_GAME_OVER_LOSS);
    }
    else if(winner == PLAYER_TWO)
    {
        playerOneMessege[0] = htonl(SERVER_MESSAGE_CODE_GAME_OVER_LOSS);
        playerTwoMessege[0] = htonl(SERVER_MESSAGE_CODE_GAME_OVER_WIN);
    }
    else // DRAW
    {
        playerOneMessege[0] = htonl(SERVER_MESSAGE_CODE_GAME_OVER_DRAW);
        playerTwoMessege[0] = htonl(SERVER_MESSAGE_CODE_GAME_OVER_DRAW);
    }

    // pad remainder of client messages with zeros to fit the server message length
    memset(playerOneMessege + 1, 0, SERVER_MESSAGE_LENGTH_WITHOUT_SERVER_MESSAGE_CODE_BYTES);
    memset(playerTwoMessege + 1, 0, SERVER_MESSAGE_LENGTH_WITHOUT_SERVER_MESSAGE_CODE_BYTES);

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

void sendGameStartMessage(int clientsFD[], size_t clientsFDLength)
{
    for(size_t i=0; i<clientsFDLength; i++)
    {
        int startGameMessage[SERVER_MESSAGE_LENGTH];
        memset(startGameMessage, 0, SERVER_MESSAGE_LENGTH_BYTES);
        startGameMessage[0] = htonl(SERVER_MESSAGE_CODE_GAME_STARTED);
        int s = send(clientsFD[i], startGameMessage, SERVER_MESSAGE_LENGTH_BYTES, 0);
        if (s == -1)
        {
            perror("sendGameStartMessage:send");
            exit(1);
        }
    }
}

void sendWaitingForOpponentMessage(int clientFD)
{
    int waitingForOpponentMessage[SERVER_MESSAGE_LENGTH];
    memset(waitingForOpponentMessage, 0, SERVER_MESSAGE_LENGTH_BYTES);
    waitingForOpponentMessage[0] = htonl(SERVER_MESSAGE_CODE_WAITING_FOR_OPPONENT);
    int s = send(clientFD, waitingForOpponentMessage, SERVER_MESSAGE_LENGTH_BYTES, 0);
    if (s == -1)
    {
        perror("sendWaitingForOpponentMessage:send");
        exit(1);
    }
}

void sendOpponentDisconnectedMessage(int clientFD)
{
    int opponentDisconnectedMessage[SERVER_MESSAGE_LENGTH];
    memset(opponentDisconnectedMessage, 0, SERVER_MESSAGE_LENGTH_BYTES);
    opponentDisconnectedMessage[0] = htonl(SERVER_MESSAGE_CODE_OPPONENT_DISSCONNECTED);
    int s = send(clientFD, opponentDisconnectedMessage, SERVER_MESSAGE_LENGTH_BYTES, 0);
    if (s == -1)
    {
        perror("sendOpponentDisconnectedMessage:send");
        exit(1);
    }
}

Bool updateServerLog(FILE* log, ServerLogCode code, char* ip_address)
{    
    switch (code)
    {
    case SERVER_LOG_CODE_ERROR:
        return appendErrorMessageToServerLog(log);
        break;
    case SERVER_LOG_CODE_CONNECTION_RECIEVED:
        return appendConnectionRecievedMessageToServerLog(log, ip_address);
        break;
    case SERVER_LOG_CODE_CONNECTION_LOST:
        return appendConnectionLostMessageToServerLog(log, ip_address);
        break;
    case SERVER_LOG_CODE_GAME_STARTED:
        return appendGameStartedMessageToServerLog(log);
        break;
    case SERVER_LOG_CODE_GAME_ENDED:
        return appendGameEndedMessageToServerLog(log);
        break;
    case SERVER_LOG_CODE_GAME_UPDATED:
        return appendGameUpdatedMessageToServerLog(log);
        break;
    case SERVER_LOG_CODE_MESSEGE_RECIEVED:
        return appendGameMessageRecievedMessageToServerLog(log, ip_address);
        break;
    case SERVER_LOG_CODE_MESSEGE_SENT:
        return appendGameMessageSentMessageToServerLog(log, ip_address);
        break;
    default:
        return False;
        break;
    }
}

Bool appendMessageToServerLog(FILE* log, char* message)
{
    if(log == NULL || message == NULL) { return False; }

    char* server_message;
    t = time(NULL);
    tm = *localtime(&t);
    asprintf(&server_message, "Server: %d/%02d/%02d %02d:%02d:%02d\t %s\n", 
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, message);

    fprintf(log, "%s",server_message);
    // free(server_message);
    return True;
}

Bool appendErrorMessageToServerLog(FILE* log)
{
    char* message = "An Error has occured with the server";
    return appendMessageToServerLog(log, message);
}

Bool appendConnectionRecievedMessageToServerLog(FILE* log, char* IPv4_address)
{
    if (IPv4_address == NULL) { return False; }
    char* message;
    asprintf(&message, "Connection recieved from: %s", IPv4_address);
    Bool res = appendMessageToServerLog(log, message);
    free(message);
    return res;
}

Bool appendConnectionLostMessageToServerLog(FILE* log, char* IPv4_address)
{
    if (IPv4_address == NULL) { return False; }
    char* message;
    asprintf(&message, "Connection to %s lost", IPv4_address);
    Bool res = appendMessageToServerLog(log, message);
    free(message);
    return res;
}

Bool appendGameStartedMessageToServerLog(FILE* log)
{
    char* message = "Game started";
    return appendMessageToServerLog(log, message); 
}

Bool appendGameEndedMessageToServerLog(FILE* log)
{
    char* message = "Game ended";
    return appendMessageToServerLog(log, message); 
}

Bool appendGameUpdatedMessageToServerLog(FILE* log)
{
    char* message = "Game updated";
    return appendMessageToServerLog(log, message); 
}

Bool appendGameMessageRecievedMessageToServerLog(FILE* log, char* IPv4_address)
{
    if (IPv4_address == NULL) { return False; }
    char* message;
    asprintf(&message, "Message recieved from: %s", IPv4_address);
    Bool res = appendMessageToServerLog(log, message);
    free(message);
}

Bool appendGameMessageSentMessageToServerLog(FILE* log, char* IPv4_address)
{
    if (IPv4_address == NULL) { return False; }
    char* message;
    asprintf(&message, "Message sent to: %s", IPv4_address);
    Bool res = appendMessageToServerLog(log, message);
    free(message);
}


ClientInput getClientInput(int client)
{
    int r, c, recieve_r, recieve_c;
    ClientInput cInput;

    // add timeout to recv calls, prevents blocking in the case of a disconnected opponent
    struct timeval tv;
    tv.tv_sec = SERVER_RECV_TIMEOUT_VALUE_IN_SECONDS;
    tv.tv_usec = 0;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);


    if ((recieve_r = recv(client, &r, sizeof(int), 0)) == -1)
    {
        perror("getClientInput:recv:r");
        exit(1);
    }

    if ((recieve_c = recv(client, &c, sizeof(int), 0)) == -1)
    {
        perror("getClientInput:recv:c");
        exit(1);
    }

    if (recieve_r == 0 || recieve_c == 0)
    {
        // setting values to -1 will be used to indicate error
        cInput.row = -1;
        cInput.col = -1;
    }

    else
    {
        cInput.row = ntohl(r);
        cInput.col = ntohl(c);
    }

    return cInput;
}

Bool rejectClientInput(int client)
{
    // get the client input but ignore (and don't return) it.
    ClientInput input = getClientInput(client);
    return !(input.row == -1 || input.col == -1);

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
        inet_ntop(client1_addr.ss_family, get_in_addr((struct sockaddr*)&client1_addr), c1addr, sizeof c1addr);
        printf("Server: got connection from %s\n", c1addr);
        sendWaitingForOpponentMessage(client1);

        client2 = accept(sockfd, (struct sockaddr *)&client2_addr, &sin_size);
        if(client2 == -1)
        {
            perror("accept");
            continue;
        }
        inet_ntop(client2_addr.ss_family, get_in_addr((struct sockaddr*)&client2_addr), c2addr, sizeof c2addr);
        printf("Server: got connection from %s\n", c2addr);
        printf("Starting Game!\n");

        if(!fork())
        {
            close(sockfd);
            // server log
            t = time(NULL);
            tm = *localtime(&t);
            char* server_log_name;
            asprintf(&server_log_name, "server-log-%d-%02d-%02d_%02d:%02d:%02d.txt", tm.tm_year + 1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
            FILE* serverLog = fopen(server_log_name, "w");
            updateServerLog(serverLog, SERVER_LOG_CODE_GAME_STARTED, NULL);
            // end server log
            int clients[] = {client1, client2};
            size_t clients_length = 2;
            sendGameStartMessage(clients, clients_length);
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
            
            //TODO: add server logs calls
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

                // TODO: (1)fix bug when client 2 disconnects before client 1 (see (2))
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
                                if(c1Input.row == -1 && c1Input.col == -1)
                                {
                                    updateServerLog(serverLog, SERVER_LOG_CODE_CONNECTION_LOST, c1addr);
                                    // notify client2 that client1 has disconnected
                                    sendOpponentDisconnectedMessage(client2);
                                    updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_SENT, c2addr);
                                    quit = True;
                                    break;
                                }

                                else
                                {
                                    updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_RECIEVED, c1addr);
                                }

                                Bool successfulUpdate = updateGameGrid(&game, c1Input, PLAYER_ONE_GRID_MARKER);
                                // printf("Is valid update p1: %d\n", successfulUpdate);
                                if (successfulUpdate)
                                {
                                    updateServerLog(serverLog, SERVER_LOG_CODE_GAME_UPDATED, NULL);
                                    noGameGrid = getGameGridInNetworkByteOrder(&game);
                                    sendClientUpdate(client1, noGameGrid);
                                    updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_SENT, c1addr);
                                    sendClientUpdate(client2, noGameGrid);
                                    updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_SENT, c2addr);
                                    freeHeapArrayInt(&noGameGrid);
                                    // Switch turn to other player
                                    playerOneTurn = !playerOneTurn;
                                }
                            }

                            // reject client2's inputs whilst it is client1's turn
                            // TODO: (2) maybe need to handle disconnects here?
                            else if (pfds[i].fd == client2) 
                            { 
                                if(!rejectClientInput(client2))
                                {
                                    updateServerLog(serverLog, SERVER_LOG_CODE_CONNECTION_LOST, c2addr);
                                    // notify client1 that client2 has disconnected
                                    sendOpponentDisconnectedMessage(client1);
                                    updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_SENT, c1addr);
                                    quit = True;
                                    break;
                                }
                            }
                        }

                        else if(!playerOneTurn)
                        {
                            if (pfds[i].fd == client2)
                            {
                                // process client2 input and switch turn to client1
                                c2Input = getClientInput(client2);
                                if(c2Input.row == -1 && c2Input.col == -1)
                                {
                                    // notify client1 that client2 has disconnected
                                    updateServerLog(serverLog, SERVER_LOG_CODE_CONNECTION_LOST, c2addr);
                                    sendOpponentDisconnectedMessage(client1);
                                    updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_SENT, c1addr);
                                    quit = True;
                                    break;
                                }
                                else
                                {
                                    updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_RECIEVED, c2addr);
                                }
                                Bool successfulUpdate = updateGameGrid(&game, c2Input, PLAYER_TWO_GRID_MARKER);
                                if (successfulUpdate)
                                {
                                    updateServerLog(serverLog, SERVER_LOG_CODE_GAME_UPDATED, NULL);
                                    noGameGrid = getGameGridInNetworkByteOrder(&game);
                                    sendClientUpdate(client1, noGameGrid);
                                    updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_SENT, c1addr);
                                    sendClientUpdate(client2, noGameGrid);
                                    updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_SENT, c2addr);
                                    freeHeapArrayInt(&noGameGrid);

                                    // Switch turn to other player
                                    playerOneTurn = !playerOneTurn;
                                }
                               
                            }
                            // reject client1's inputs whilst it is client2's turn
                            else if (pfds[i].fd == client1) 
                            { 
                                if(!rejectClientInput(client1))
                                {
                                    updateServerLog(serverLog, SERVER_LOG_CODE_CONNECTION_LOST, c1addr);
                                    // notify client2 that client1 has disconnected
                                    sendOpponentDisconnectedMessage(client2);
                                    updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_SENT, c2addr);
                                    quit = True;
                                    break;
                                }
                            }
                        }

                        enum GameResult current_game_status = isGameOver(&game);
                        if(current_game_status)
                        {
                            int c[2] = {client1, client2};
                            switch (current_game_status)
                            {
                            case P1WIN:
                                // printf("Game over! Player 1 won\n");
                                updateServerLog(serverLog, SERVER_LOG_CODE_GAME_ENDED, NULL);
                                sendGameOverUpdate(c, PLAYER_ONE);
                                updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_SENT, c1addr);
                                updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_SENT, c2addr);
                                break;
                            case P2WIN: 
                                // printf("Game over! Player 2 won\n");
                                updateServerLog(serverLog, SERVER_LOG_CODE_GAME_ENDED, NULL);
                                sendGameOverUpdate(c, PLAYER_TWO);
                                updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_SENT, c1addr);
                                updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_SENT, c2addr);
                                break;
                            case DRAW: 
                                // printf("Game over! Draw\n");
                                updateServerLog(serverLog, SERVER_LOG_CODE_GAME_ENDED, NULL);
                                sendGameOverUpdate(c, PLAYER_NONE);
                                updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_SENT, c1addr);
                                updateServerLog(serverLog, SERVER_LOG_CODE_MESSEGE_SENT, c2addr);
                                break;
                            default:
                                break;
                            }
                        }

                    }
                }
            }
            fclose(serverLog);
            printf("Save server log? Y or N: ");
            char answer;
            scanf("%c", &answer);
            // convert answer to lower case
            if(answer>=65 && answer<=90)
            {
                answer += 32;
            }

            if(answer == 'n')
            {
                remove(server_log_name);
            }

            free(server_log_name);
            close(client1);
            close(client2);
            exit(0);
        }
        close(client1);
        close(client2);
    }

    return 0;
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