#include <stdio.h> 
#include <math.h> 
#include <errno.h> 
#include <string.h> 
#include <netdb.h> 
#include <sys/types.h> 
#include <netinet/in.h> 
#include <sys/socket.h>
#include <arpa/inet.h> 
#include <stdlib.h> 
#include <poll.h> 
#include "raylib.h"
#include "globals.h"

#define WINDOW_WIDTH 900
#define WINDOW_HEIGHT 900
#define GRID_SPACING 300
#define LINE_LENGTH 400
#define LINE_THICKNESS 8
#define LINE_THICKNESS_STRIKE 15
#define NOUGHT_OUTER_RADIUS 125
#define NOUGHT_INNER_RADIUS NOUGHT_OUTER_RADIUS - LINE_THICKNESS
#define LEFT_MOUSE_BUTTON 0
#define MIDDLE_MOUSE_BUTTON 1
#define RIGHT_MOUSE_BUTTON 2
#define IP "127.0.0.1"
#define POLL_WAIT_TIME 25 // poll wait time in ms

typedef struct {
    int row;
    int col;
}GridCell;

void recieveAndUpdateGameData(int sockfd, int grid[][MAX_COL], int temp[MAX_ROW*MAX_COL]);
void draw_grid_lines(void);
void draw_cross(size_t, size_t);
void draw_nought(size_t, size_t);
void draw_message(char* message, int x, int y, Color c);
void draw_waiting_for_opponent_message(void);
void draw_opponent_dissconnected_message(void);
void draw_game_result_message(char*);
void strikeThroughWinningLine(int grid[][MAX_COL]);
void strikeThrough(Vector2, Vector2);
void strikeThroughRow(int row);
void strikeThroughCol(int col);
void strikeThroughl2rDiagonal(void);
void strikeThroughr2lDiagonal(void);
Vector2 getGridCellCentreCoord(int row, int col);
void renderGame(int grid[3][3]);
Bool isGridValid(int*);
GridCell getGridCell(Vector2);
void* get_in_addr(struct sockaddr*);

// DEBUG Functions
void printGrid(int grid[3][3]);



int main(void)
{
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "BATTLE TTT!");
    SetTargetFPS(30);

    // networking code
    int sockfd;
    struct addrinfo hints, *serverinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    rv = getaddrinfo(IP, PORT, &hints, &serverinfo);
    if(rv != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through addrinfo linkedlist and connect to the first result we can
    for(p = serverinfo; p!=NULL; p =p->ai_next)
    {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if(sockfd == -1)
        {
            perror("client: socket");
            continue;
        }

        if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if(p == NULL)
    {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr*)p->ai_addr), s, sizeof s);
    printf("client: connected to %s\n", s);
    freeaddrinfo(serverinfo); // struct not needed anymore

    int grid[3][3] = {{0,0,0}, {0,0,0}, {0,0,0}};
    Bool game_in_progress = False;
    Bool waiting_for_opponent = False;
    Bool opponent_dissconnected = False;
    char* game_result = NULL;
    int numbytes;
    int buf[SERVER_MESSAGE_LENGTH];
    int temp[SERVER_MESSAGE_LENGTH_WITHOUT_SERVER_MESSAGE_CODE] = {0};
    struct pollfd pfds[1];
    pfds[0].fd = sockfd;
    pfds[0].events = POLLIN;
    int fd_count = 1;

    while (!WindowShouldClose())
    {
        if(IsMouseButtonPressed(LEFT_MOUSE_BUTTON) && game_in_progress)
        {
            GridCell clickedGridCell = getGridCell(GetMousePosition());
            int row = htonl((int)clickedGridCell.row);
            int col = htonl((int)clickedGridCell.col);
            int s;
            s = send(sockfd, &row, sizeof row, 0);
            if(s <= 0)
            {
                perror("send:row");
                break;
            }
            s = send(sockfd, &col, sizeof col, 0);
            if (s <= 0)
            {
                perror("send:col");
                break;
            }
        }

        // poll the server connection socket to see if the server has sent any game updates
        // if yes retrive the data from the socket and update the game grid stored locally.
        int poll_count = poll(pfds, fd_count, POLL_WAIT_TIME);
        if(poll_count == -1)
        {
            perror("pool");
            exit(1);
        }
        for(size_t i=0; i<fd_count; i++)
        {
            // check if socket is ready to read
            if(pfds[i].revents & POLLIN)
            {
                // clear reciver buffer before reading
                // memset(buf, 0, sizeof(int)*(MAX_ROW*MAX_COL+1));
                // numbytes = recv(sockfd, buf, sizeof(int)*(MAX_ROW*MAX_COL+1), 0);
                memset(buf, 0, SERVER_MESSAGE_LENGTH_BYTES);
                numbytes = recv(sockfd, buf, SERVER_MESSAGE_LENGTH_BYTES, 0);
                if (numbytes == -1)
                {
                    perror("recv");
                    exit(1);
                }

                int serverCode = ntohl(buf[0]);
                switch (serverCode)
                {
                case SERVER_MESSAGE_CODE_INVALID:
                    break;
                case SERVER_MESSAGE_CODE_WAITING_FOR_OPPONENT:
                    waiting_for_opponent = True;
                    break;
                case SERVER_MESSAGE_CODE_GAME_STARTED:
                    printf("Game started\n");
                    game_in_progress = True;
                    waiting_for_opponent = False;
                    break;
                case SERVER_MESSAGE_CODE_OPPONENT_DISSCONNECTED:
                    game_in_progress = False;
                    waiting_for_opponent = False;
                    opponent_dissconnected = True;
                    break;
                case SERVER_MESSAGE_CODE_GAME_DATA_UPDATE:
                    // clear temp array before reading
                    memset(temp, 0, SERVER_MESSAGE_LENGTH_WITHOUT_SERVER_MESSAGE_CODE_BYTES);
                    // copy game data to temp buffer
                    memcpy(temp, buf + 1, SERVER_MESSAGE_LENGTH_WITHOUT_SERVER_MESSAGE_CODE_BYTES);
                    recieveAndUpdateGameData(sockfd, grid, temp);
                    break;
                case SERVER_MESSAGE_CODE_GAME_OVER_WIN:
                    game_in_progress = False;
                    game_result = "Win";
                    break;
                case SERVER_MESSAGE_CODE_GAME_OVER_LOSS:
                    game_in_progress = False;
                    game_result = "Lose";
                    break;
                case SERVER_MESSAGE_CODE_GAME_OVER_DRAW:
                    game_in_progress = False;
                    game_result = "Drew";
                    break;
                default:
                    break;
                }
            }
        }

        // Rendering logic
        BeginDrawing();
        ClearBackground(RAYWHITE);
        if(opponent_dissconnected)
        {
            draw_opponent_dissconnected_message();
        }
        else if(waiting_for_opponent)
        {
            draw_waiting_for_opponent_message();
        }
        else
        {
            renderGame(grid);
            strikeThroughWinningLine(grid);
            draw_game_result_message(game_result);
        }

        EndDrawing();
    }
    
    CloseWindow();
    close(sockfd);
    return 0;
}

void recieveAndUpdateGameData(int sockfd, int grid[][MAX_COL], int temp[MAX_ROW*MAX_COL])
{
        // If the temp grid is invalid reject it and DON'T copy content of temp to the "grid" array.
    if (isGridValid(temp))
    {
        // // copy contents of temp to "grid" array and convert values to host byte order
        int idx = 0;
        for (size_t row = 0; row < MAX_ROW; row++)
        {
            for (size_t col = 0; col < MAX_COL; col++)
            {
                grid[row][col] = ntohl(temp[idx]);
                idx++;
            }
        }

        printGrid(grid);
    }
}

void draw_grid_lines(void)
{
    for(size_t i=1; i<3; i++)
    {
        // Draw vertical line
        DrawLine(i*GRID_SPACING, 0, i*GRID_SPACING, WINDOW_HEIGHT, BLACK);
        // Draw horizontal line
        DrawLine(0, i*GRID_SPACING, WINDOW_WIDTH, i*GRID_SPACING, BLACK);
    }
}
void draw_cross(size_t gridRow, size_t gridCol)
{
    int centreX = (gridCol * GRID_SPACING) + (GRID_SPACING / 2);
    int centreY = (gridRow * GRID_SPACING) + (GRID_SPACING / 2);
    double z = (LINE_LENGTH / 2) * (1.0f / sqrt(2));

    Vector2 startL2R = {(centreX - z), (centreY - z)};
    Vector2 endL2R = {(centreX + z), (centreY + z)};
    Vector2 startR2L = {(centreX + z), (centreY - z)};
    Vector2 endR2L = {(centreX - z), (centreY + z)};

    DrawLineEx(startL2R, endL2R, LINE_THICKNESS, BLACK);
    DrawLineEx(startR2L, endR2L, LINE_THICKNESS, BLACK);
}

void draw_nought(size_t gridRow, size_t gridCol)
{
    int centreX = (gridCol * GRID_SPACING) + (GRID_SPACING / 2);
    int centreY = (gridRow * GRID_SPACING) + (GRID_SPACING / 2);

    DrawCircle(centreX, centreY, NOUGHT_OUTER_RADIUS, BLACK);
    DrawCircle(centreX, centreY, NOUGHT_INNER_RADIUS, RAYWHITE);
}

void draw_message(char* message, int x, int y, Color c)
{
    DrawText(message, x, y, FONT_SIZE, c);
}

void draw_waiting_for_opponent_message(void)
{
    char* message = "Waiting for opponent";
    int messageWidth = MeasureText(message, FONT_SIZE);
    draw_message(message, (WINDOW_WIDTH / 2) - (messageWidth / 2), (WINDOW_HEIGHT / 2), BLACK);
}

void draw_opponent_dissconnected_message(void)
{
    char* message = "Opponent Dissconnected";
    int messageWidth = MeasureText(message, FONT_SIZE);
    draw_message(message, (WINDOW_WIDTH / 2) - (messageWidth / 2), (WINDOW_HEIGHT / 2), BLACK);
}

void draw_game_result_message(char* game_result)
{
    if(game_result == NULL) { return; }
    // render game result message
    int messageWidth = MeasureText(game_result, FONT_SIZE);
    Color c = (strcmp(game_result, "Drew") == 0) ? PURPLE : (strcmp(game_result, "Win") == 0) ? BLUE : RED;
    draw_message(game_result, (WINDOW_WIDTH / 2) - (messageWidth / 2), (WINDOW_HEIGHT / 2) - 10, c);

}

void renderGame(int grid[3][3])
{
    draw_grid_lines();
    for(size_t row=0; row<3; row++)
    {
        for(size_t col=0; col<3; col++)
        {
            switch (grid[row][col])
            {
            // TODO: May be an issue sending negative numbers over the network on certain machines
            // NOUGHT_SYMBOL = -1
            case NOUGHT_SYMBOL:
                draw_nought(row, col);
                break;
            case CROSS_SYMBOL:
                draw_cross(row, col);
                break;
            default:
                break;
            }
        }
    }
}

GridCell getGridCell(Vector2 mousePosition)
{
    GridCell gc;
    gc.row = mousePosition.y / GRID_SPACING;
    gc.col = mousePosition.x / GRID_SPACING;

    return gc;
}

void* get_in_addr(struct sockaddr* sa)
{
    if(sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

Bool isGridValid(int* grid)
{
    // Since array is passed as a pointer we can treat it as a 1D array of length (MAX_ROW*MAX_COL)?
    // As opposed to a 2D [3][3] array
    int value;
    for(size_t i=0; i<(MAX_ROW*MAX_COL); i++)
    {
        value = ntohl(grid[i]);
        if(abs(value) > 1)
        {
            return False;
        }
    }
    return True;
}

Vector2 getGridCellCentreCoord(int row, int col)
{
    double r = (row * GRID_SPACING) + (GRID_SPACING / 2);
    double c = (col * GRID_SPACING) + (GRID_SPACING / 2);

    Vector2 centreCoord = {c, r};
    return  centreCoord;
}

void strikeThroughWinningLine(int grid[][MAX_COL])
{
    // Check horizontal win conditions
    for (size_t row = 0; row < MAX_ROW; row++)
    {
        int sum = grid[row][0] + grid[row][1] + grid[row][2];
        if (abs(sum) == WINNING_SCORE)
        {
            strikeThroughRow(row);
        }
    }

    // Check vertical win conditions
    for (size_t col = 0; col < MAX_COL; col++)
    {
        int sum = grid[0][col] + grid[1][col] + grid[2][col];
        if (abs(sum) == WINNING_SCORE)
        {
            strikeThroughCol(col);
        }
    }

    // Check diagonal win conditons
    int l2rDiagonal = grid[0][0] + grid[1][1] + grid[2][2];
    if (abs(l2rDiagonal) == 3)
    {
        strikeThroughl2rDiagonal();
    }

    int r2lDiagonal = grid[0][2] + grid[1][1] + grid[2][0];
    if (abs(r2lDiagonal) == 3)
    {
        strikeThroughr2lDiagonal();
    }
}

void strikeThrough(Vector2 start, Vector2 end)
{
    DrawLineEx(start, end, LINE_THICKNESS_STRIKE, ColorAlpha(RED, 0.5f));
}

void strikeThroughRow(int row)
{
    Vector2 start = {0, (row*GRID_SPACING + (GRID_SPACING/2))};
    Vector2 end = {GRID_SPACING*MAX_COL, (row*GRID_SPACING + (GRID_SPACING/2))};
    strikeThrough(start, end);
}

void strikeThroughCol(int col)
{
    Vector2 start = {(col*GRID_SPACING +(GRID_SPACING / 2)), 0};
    Vector2 end = {(col*GRID_SPACING +(GRID_SPACING / 2)), GRID_SPACING*MAX_ROW};
    strikeThrough(start, end);
}

void strikeThroughl2rDiagonal(void)
{
    Vector2 start = {0, 0};
    Vector2 end = {(GRID_SPACING*MAX_COL), (GRID_SPACING*MAX_ROW)};
    strikeThrough(start, end);
}

void strikeThroughr2lDiagonal(void)
{
    Vector2 start = {(GRID_SPACING*MAX_COL), 0};
    Vector2 end = {0, (GRID_SPACING*MAX_ROW)};
    strikeThrough(start, end);
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