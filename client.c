#include <stdio.h>
#include <math.h>
#include "raylib.h"

#define WINDOW_WIDTH 900
#define WINDOW_HEIGHT 900
#define GRID_SPACING 300
#define LINE_LENGTH 400
#define LINE_THICKNESS 3
#define NOUGHT_OUTER_RADIUS 125
#define NOUGHT_INNER_RADIUS NOUGHT_OUTER_RADIUS - LINE_THICKNESS
#define LEFT_MOUSE_BUTTON 0
#define MIDDLE_MOUSE_BUTTON 1
#define RIGHT_MOUSE_BUTTON 2



typedef struct {
    int row;
    int col;
}GridCell;

void draw_grid_lines();
void draw_cross(size_t, size_t);
void draw_nought(size_t, size_t);
void renderGame(int grid[3][3]);
GridCell getGridCell(Vector2);
// TODO: Add networking code for client

int main(void)
{
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "BATTLE TTT!");
    SetTargetFPS(30);

    // test game grid
    int arr[3][3] = {{-1, -1, -1}, {0, 1, 0}, {1, 1, 1}};

    while (!WindowShouldClose())
    {
        if(IsMouseButtonPressed(LEFT_MOUSE_BUTTON))
        {
            GridCell clickedGridCell = getGridCell(GetMousePosition());
        }
        BeginDrawing();
        ClearBackground(RAYWHITE);
        renderGame(arr);
        EndDrawing();
    }
    
    CloseWindow();
    return 0;
}

void draw_grid_lines()
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

void renderGame(int grid[3][3])
{
    draw_grid_lines();
    for(size_t row=0; row<3; row++)
    {
        for(size_t col=0; col<3; col++)
        {
            switch (grid[row][col])
            {
            case -1:
                draw_nought(row, col);
                break;
            case 1:
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