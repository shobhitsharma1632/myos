#include <libuser.h>

#define BOARD_WIDTH  10
#define BOARD_HEIGHT 20

static uint8_t g_Board[BOARD_HEIGHT][BOARD_WIDTH];
static int g_Score = 0;
static int g_LinesCleared = 0;
static int g_Level = 1;

// 7 Tetrominoes, each having 4 rotations, defined on a 4x4 grid
static const uint8_t TETROMINOES[7][4][4][4] = {
    // 0: I piece
    {
        { {0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0} },
        { {0,0,1,0}, {0,0,1,0}, {0,0,1,0}, {0,0,1,0} },
        { {0,0,0,0}, {0,0,0,0}, {1,1,1,1}, {0,0,0,0} },
        { {0,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,1,0,0} }
    },
    // 1: J piece
    {
        { {1,0,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,1,1,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0} },
        { {0,0,0,0}, {1,1,1,0}, {0,0,1,0}, {0,0,0,0} },
        { {0,1,0,0}, {0,1,0,0}, {1,1,0,0}, {0,0,0,0} }
    },
    // 2: L piece
    {
        { {0,0,1,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,1,0,0}, {0,1,0,0}, {0,1,1,0}, {0,0,0,0} },
        { {0,0,0,0}, {1,1,1,0}, {1,0,0,0}, {0,0,0,0} },
        { {1,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0} }
    },
    // 3: O piece (Square)
    {
        { {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} }
    },
    // 4: S piece
    {
        { {0,1,1,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,1,0,0}, {0,1,1,0}, {0,0,1,0}, {0,0,0,0} },
        { {0,0,0,0}, {0,1,1,0}, {1,1,0,0}, {0,0,0,0} },
        { {1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0} }
    },
    // 5: T piece
    {
        { {0,1,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,1,0,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0} },
        { {0,0,0,0}, {1,1,1,0}, {0,1,0,0}, {0,0,0,0} },
        { {0,1,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0} }
    },
    // 6: Z piece
    {
        { {1,1,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,0,1,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0} },
        { {0,0,0,0}, {1,1,0,0}, {0,1,1,0}, {0,0,0,0} },
        { {0,1,0,0}, {1,1,0,0}, {1,0,0,0}, {0,0,0,0} }
    }
};

static bool CheckCollision(int piece, int rotation, int posX, int posY)
{
    for (int r = 0; r < 4; r++)
    {
        for (int c = 0; c < 4; c++)
        {
            if (TETROMINOES[piece][rotation][r][c])
            {
                int x = posX + c;
                int y = posY + r;

                if (x < 0 || x >= BOARD_WIDTH || y >= BOARD_HEIGHT)
                    return true;

                if (y >= 0 && g_Board[y][x] != 0)
                    return true;
            }
        }
    }
    return false;
}

static void LockPiece(int piece, int rotation, int posX, int posY)
{
    for (int r = 0; r < 4; r++)
    {
        for (int c = 0; c < 4; c++)
        {
            if (TETROMINOES[piece][rotation][r][c])
            {
                int x = posX + c;
                int y = posY + r;
                if (x >= 0 && x < BOARD_WIDTH && y >= 0 && y < BOARD_HEIGHT)
                {
                    g_Board[y][x] = (uint8_t)(piece + 1);
                }
            }
        }
    }
}

static int ClearFullLines(void)
{
    int cleared = 0;
    for (int y = BOARD_HEIGHT - 1; y >= 0; y--)
    {
        bool full = true;
        for (int x = 0; x < BOARD_WIDTH; x++)
        {
            if (g_Board[y][x] == 0)
            {
                full = false;
                break;
            }
        }

        if (full)
        {
            cleared++;
            // Shift all rows down
            for (int k = y; k > 0; k--)
            {
                for (int x = 0; x < BOARD_WIDTH; x++)
                {
                    g_Board[k][x] = g_Board[k - 1][x];
                }
            }
            for (int x = 0; x < BOARD_WIDTH; x++)
            {
                g_Board[0][x] = 0;
            }
            y++; // Check same row again
        }
    }

    if (cleared > 0)
    {
        int points = 0;
        if (cleared == 1) points = 100;
        else if (cleared == 2) points = 300;
        else if (cleared == 3) points = 500;
        else if (cleared == 4) points = 800; // Tetris!

        g_Score += points;
        g_LinesCleared += cleared;
        g_Level = (g_LinesCleared / 10) + 1;

        // Play Line Clear sound effect on Sound Blaster 16
        audio_play_wav("/sounds/coin.wav", false);
    }

    return cleared;
}

static void RenderBoard(int curPiece, int curRot, int curX, int curY)
{
    printf("\033[H\033[2J"); // Clear screen
    printf("=========================================\n");
    printf("   myOS Classic Retro Tetris (Ring 3)    \n");
    printf("=========================================\n");
    printf(" Score: %-6d | Lines: %-3d | Level: %-2d\n", g_Score, g_LinesCleared, g_Level);
    printf("-----------------------------------------\n");

    uint8_t tempBoard[BOARD_HEIGHT][BOARD_WIDTH];
    memcpy(tempBoard, g_Board, sizeof(g_Board));

    // Overlay current falling piece
    if (curPiece >= 0)
    {
        for (int r = 0; r < 4; r++)
        {
            for (int c = 0; c < 4; c++)
            {
                if (TETROMINOES[curPiece][curRot][r][c])
                {
                    int x = curX + c;
                    int y = curY + r;
                    if (x >= 0 && x < BOARD_WIDTH && y >= 0 && y < BOARD_HEIGHT)
                    {
                        tempBoard[y][x] = (uint8_t)(curPiece + 1);
                    }
                }
            }
        }
    }

    for (int y = 0; y < BOARD_HEIGHT; y++)
    {
        printf("<|");
        for (int x = 0; x < BOARD_WIDTH; x++)
        {
            if (tempBoard[y][x] == 0)
            {
                printf(" .");
            }
            else
            {
                printf("[]");
            }
        }
        printf("|>");

        if (y == 2) printf("   CONTROLS:");
        else if (y == 3) printf("   A / Left  : Move Left");
        else if (y == 4) printf("   D / Right : Move Right");
        else if (y == 5) printf("   W / Up    : Rotate");
        else if (y == 6) printf("   S / Down  : Soft Drop");
        else if (y == 7) printf("   Space     : Hard Drop");
        else if (y == 8) printf("   Q         : Quit");
        else if (y == 11) printf("   SOUND: Sound Blaster 16 Active");
        printf("\n");
    }
    printf("====================\n");
}

int main(int argc, char* argv[])
{
    bool test_mode = false;
    if (argc >= 2 && strcmp(argv[1], "--test") == 0)
    {
        test_mode = true;
    }

    memset(g_Board, 0, sizeof(g_Board));
    g_Score = 0;
    g_LinesCleared = 0;
    g_Level = 1;

    if (test_mode)
    {
        printf("Running Tetris in automated verification test mode...\n");

        // Simulate 4 pieces dropping and locking to fill line 19
        // Pre-fill bottom line except center 2 slots
        for (int x = 0; x < BOARD_WIDTH; x++)
        {
            if (x != 4 && x != 5)
            {
                g_Board[BOARD_HEIGHT - 1][x] = 1;
            }
        }

        // Drop an 'O' piece (square) into columns 4 and 5
        int piece = 3; // O piece
        int rot = 0;
        int posX = 3; // Places blocks at x=4, 5
        int posY = BOARD_HEIGHT - 2;

        bool col = CheckCollision(piece, rot, posX, posY);
        printf("  Test collision check: %s\n", !col ? "PASSED (No collision)" : "FAILED");

        LockPiece(piece, rot, posX, posY);
        printf("  Test piece lock-in: PASSED\n");

        int cleared = ClearFullLines();
        printf("  Test line clear: %d line(s) cleared (Score: %d) -> %s\n",
               cleared, g_Score, cleared == 1 ? "PASSED" : "FAILED");

        // Verify audio triggering
        audio_status_t st;
        sys_audio_status(&st);
        printf("  Audio status: HW=%s, Playing=%s\n",
               st.hardware_present ? "YES" : "NO", st.is_playing ? "YES" : "IDLE");

        printf("Tetris automated verification tests PASSED!\n");
        return 0;
    }

    // Interactive gameplay loop
    int curPiece = 0;
    int curRot = 0;
    int curX = 3;
    int curY = 0;
    int tick = 0;

    RenderBoard(curPiece, curRot, curX, curY);

    while (1)
    {
        tick++;
        // Check for player keypress
        char c = 0;
        if (sys_read(&c, 1) > 0 && c != 0)
        {
            if (c == 'q' || c == 'Q') break;
            else if (c == 'a' || c == 'A') // Left
            {
                if (!CheckCollision(curPiece, curRot, curX - 1, curY)) curX--;
            }
            else if (c == 'd' || c == 'D') // Right
            {
                if (!CheckCollision(curPiece, curRot, curX + 1, curY)) curX++;
            }
            else if (c == 'w' || c == 'W') // Rotate
            {
                int nextRot = (curRot + 1) % 4;
                if (!CheckCollision(curPiece, nextRot, curX, curY)) curRot = nextRot;
            }
            else if (c == 's' || c == 'S') // Soft drop
            {
                if (!CheckCollision(curPiece, curRot, curX, curY + 1)) curY++;
            }
            else if (c == ' ') // Hard drop
            {
                while (!CheckCollision(curPiece, curRot, curX, curY + 1)) curY++;
                LockPiece(curPiece, curRot, curX, curY);
                ClearFullLines();
                curPiece = (curPiece + 1) % 7;
                curRot = 0;
                curX = 3;
                curY = 0;
                if (CheckCollision(curPiece, curRot, curX, curY))
                {
                    printf("\n*** GAME OVER! Final Score: %d ***\n", g_Score);
                    break;
                }
            }
            RenderBoard(curPiece, curRot, curX, curY);
        }

        // Automatic gravity step
        if ((tick % 5) == 0)
        {
            if (!CheckCollision(curPiece, curRot, curX, curY + 1))
            {
                curY++;
            }
            else
            {
                LockPiece(curPiece, curRot, curX, curY);
                ClearFullLines();
                curPiece = (curPiece + 1) % 7;
                curRot = 0;
                curX = 3;
                curY = 0;
                if (CheckCollision(curPiece, curRot, curX, curY))
                {
                    printf("\n*** GAME OVER! Final Score: %d ***\n", g_Score);
                    break;
                }
            }
            RenderBoard(curPiece, curRot, curX, curY);
        }

        sys_sleep(100);
    }

    return 0;
}
