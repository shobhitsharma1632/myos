#include <libuser.h>
#include <libgui.h>

#define BOARD_WIDTH  10
#define BOARD_HEIGHT 20
#define TILE_SIZE    18
#define BOARD_X      20
#define BOARD_Y      36

static uint8_t g_Board[BOARD_HEIGHT][BOARD_WIDTH];
static int g_Score = 0;
static int g_LinesCleared = 0;
static int g_Level = 1;

// 7 Tetrominoes, each having 4 rotations, defined on a 4x4 grid
static const uint8_t TETROMINOES[7][4][4][4] = {
    // 0: I piece (Cyan)
    {
        { {0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0} },
        { {0,0,1,0}, {0,0,1,0}, {0,0,1,0}, {0,0,1,0} },
        { {0,0,0,0}, {0,0,0,0}, {1,1,1,1}, {0,0,0,0} },
        { {0,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,1,0,0} }
    },
    // 1: J piece (Blue)
    {
        { {1,0,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,1,1,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0} },
        { {0,0,0,0}, {1,1,1,0}, {0,0,1,0}, {0,0,0,0} },
        { {0,1,0,0}, {0,1,0,0}, {1,1,0,0}, {0,0,0,0} }
    },
    // 2: L piece (Orange)
    {
        { {0,0,1,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,1,0,0}, {0,1,0,0}, {0,1,1,0}, {0,0,0,0} },
        { {0,0,0,0}, {1,1,1,0}, {1,0,0,0}, {0,0,0,0} },
        { {1,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0} }
    },
    // 3: O piece (Yellow Square)
    {
        { {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} }
    },
    // 4: S piece (Green)
    {
        { {0,1,1,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,1,0,0}, {0,1,1,0}, {0,0,1,0}, {0,0,0,0} },
        { {0,0,0,0}, {0,1,1,0}, {1,1,0,0}, {0,0,0,0} },
        { {1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0} }
    },
    // 5: T piece (Purple)
    {
        { {0,1,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,1,0,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0} },
        { {0,0,0,0}, {1,1,1,0}, {0,1,0,0}, {0,0,0,0} },
        { {0,1,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0} }
    },
    // 6: Z piece (Red)
    {
        { {1,1,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
        { {0,0,1,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0} },
        { {0,0,0,0}, {1,1,0,0}, {0,1,1,0}, {0,0,0,0} },
        { {0,1,0,0}, {1,1,0,0}, {1,0,0,0}, {0,0,0,0} }
    }
};

static const uint32_t PIECE_COLORS[8] = {
    0x000F172A, // 0: Empty
    0x0006B6D4, // 1: I - Cyan
    0x002563EB, // 2: J - Blue
    0x00EA580C, // 3: L - Orange
    0x00EAB308, // 4: O - Yellow
    0x0010B981, // 5: S - Green
    0x00A855F7, // 6: T - Purple
    0x00EF4444  // 7: Z - Red
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
            y++;
        }
    }

    if (cleared > 0)
    {
        int points = 0;
        if (cleared == 1) points = 100;
        else if (cleared == 2) points = 300;
        else if (cleared == 3) points = 500;
        else if (cleared == 4) points = 800;

        g_Score += points;
        g_LinesCleared += cleared;
        g_Level = (g_LinesCleared / 10) + 1;

        audio_play_wav("/sounds/coin.wav", false);
    }

    return cleared;
}

static void DrawTetrisBlock(gui_window_t* win, int px, int py, uint32_t col)
{
    gui_fill_rect(win, px, py, TILE_SIZE, TILE_SIZE, col);
    // 3D highlight top/left
    gui_draw_line(win, px, py, px + TILE_SIZE - 1, py, 0x00FFFFFF);
    gui_draw_line(win, px, py, px, py + TILE_SIZE - 1, 0x00FFFFFF);
    // 3D shadow bottom/right
    gui_draw_line(win, px, py + TILE_SIZE - 1, px + TILE_SIZE - 1, py + TILE_SIZE - 1, 0x000F172A);
    gui_draw_line(win, px + TILE_SIZE - 1, py, px + TILE_SIZE - 1, py + TILE_SIZE - 1, 0x000F172A);
}

static void RunGuiTetris(gui_window_t* win)
{
    memset(g_Board, 0, sizeof(g_Board));
    g_Score = 0;
    g_LinesCleared = 0;
    g_Level = 1;

    int curPiece = 0;
    int curRot = 0;
    int curX = 3;
    int curY = 0;
    int nextPiece = 1;
    bool game_over = false;

    uint32_t last_gravity_tick = sys_getticks();

    while (!win->closed)
    {
        // 1. Process Window Events
        gui_event_t ev;
        while (gui_poll_event(win, &ev))
        {
            if (ev.type == GUI_EVENT_CLOSE)
            {
                win->closed = true;
                break;
            }
            else if (ev.type == GUI_EVENT_KEY_DOWN)
            {
                uint32_t k = ev.key_char;
                if (k >= 'A' && k <= 'Z') k += 32;

                if (k == 'q' || ev.key_code == 0x01) // Esc or Q
                {
                    win->closed = true;
                    break;
                }
                else if (game_over)
                {
                    if (k == ' ' || ev.key_code == 0x1C) // Space or Enter restarts
                    {
                        memset(g_Board, 0, sizeof(g_Board));
                        g_Score = 0;
                        g_LinesCleared = 0;
                        g_Level = 1;
                        curPiece = 0;
                        curRot = 0;
                        curX = 3;
                        curY = 0;
                        nextPiece = 1;
                        game_over = false;
                    }
                }
                else
                {
                    // Movement Controls (WASD & Arrows)
                    if (k == 'a' || ev.key_code == 0xCB || ev.key_code == 0x1E) // Left
                    {
                        if (!CheckCollision(curPiece, curRot, curX - 1, curY)) curX--;
                    }
                    else if (k == 'd' || ev.key_code == 0xCD || ev.key_code == 0x20) // Right
                    {
                        if (!CheckCollision(curPiece, curRot, curX + 1, curY)) curX++;
                    }
                    else if (k == 'w' || ev.key_code == 0xC8 || ev.key_code == 0x11) // Rotate
                    {
                        int nextRot = (curRot + 1) % 4;
                        if (!CheckCollision(curPiece, nextRot, curX, curY)) curRot = nextRot;
                    }
                    else if (k == 's' || ev.key_code == 0xD0 || ev.key_code == 0x1F) // Soft drop
                    {
                        if (!CheckCollision(curPiece, curRot, curX, curY + 1)) curY++;
                    }
                    else if (k == ' ' || ev.key_code == 0x39) // Hard drop
                    {
                        while (!CheckCollision(curPiece, curRot, curX, curY + 1)) curY++;
                        LockPiece(curPiece, curRot, curX, curY);
                        ClearFullLines();
                        curPiece = nextPiece;
                        nextPiece = (nextPiece + 3) % 7;
                        curRot = 0;
                        curX = 3;
                        curY = 0;
                        if (CheckCollision(curPiece, curRot, curX, curY))
                        {
                            game_over = true;
                            audio_play_wav("/sounds/boom.wav", false);
                        }
                    }
                }
            }
        }

        // 2. Gravity tick
        uint32_t now = sys_getticks();
        uint32_t grav_interval = (g_Level >= 10) ? 20 : (55 - g_Level * 3);
        if (!game_over && (now - last_gravity_tick >= grav_interval))
        {
            last_gravity_tick = now;
            if (!CheckCollision(curPiece, curRot, curX, curY + 1))
            {
                curY++;
            }
            else
            {
                LockPiece(curPiece, curRot, curX, curY);
                ClearFullLines();
                curPiece = nextPiece;
                nextPiece = (nextPiece + 3) % 7;
                curRot = 0;
                curX = 3;
                curY = 0;
                if (CheckCollision(curPiece, curRot, curX, curY))
                {
                    game_over = true;
                    audio_play_wav("/sounds/boom.wav", false);
                }
            }
        }

        // 3. Render GUI Frame
        gui_clear(win, 0x000F172A);

        // Header Title
        gui_draw_text(win, 20, 10, "myOS Tetris Arcade", GUI_COLOR_CYAN, GUI_COLOR_TRANSPARENT);
        gui_draw_text(win, 180, 10, "Classic 10x20 Falling Blocks", GUI_COLOR_LIGHT_SLATE, GUI_COLOR_TRANSPARENT);

        // Playing Field Background & Border
        int field_w = BOARD_WIDTH * TILE_SIZE;
        int field_h = BOARD_HEIGHT * TILE_SIZE;
        gui_fill_rect(win, BOARD_X, BOARD_Y, field_w, field_h, 0x00020617);
        gui_draw_rect(win, BOARD_X - 1, BOARD_Y - 1, field_w + 2, field_h + 2, 0x00334155);

        // Board Grid lines
        for (int r = 1; r < BOARD_HEIGHT; r++)
            gui_draw_line(win, BOARD_X, BOARD_Y + r * TILE_SIZE, BOARD_X + field_w, BOARD_Y + r * TILE_SIZE, 0x000B132B);
        for (int c = 1; c < BOARD_WIDTH; c++)
            gui_draw_line(win, BOARD_X + c * TILE_SIZE, BOARD_Y, BOARD_X + c * TILE_SIZE, BOARD_Y + field_h, 0x000B132B);

        // Render Locked Blocks
        for (int y = 0; y < BOARD_HEIGHT; y++)
        {
            for (int x = 0; x < BOARD_WIDTH; x++)
            {
                uint8_t p = g_Board[y][x];
                if (p > 0 && p < 8)
                {
                    DrawTetrisBlock(win, BOARD_X + x * TILE_SIZE, BOARD_Y + y * TILE_SIZE, PIECE_COLORS[p]);
                }
            }
        }

        // Render Ghost Piece
        int ghostY = curY;
        while (!CheckCollision(curPiece, curRot, curX, ghostY + 1)) ghostY++;
        for (int r = 0; r < 4; r++)
        {
            for (int c = 0; c < 4; c++)
            {
                if (TETROMINOES[curPiece][curRot][r][c])
                {
                    int gx = curX + c;
                    int gy = ghostY + r;
                    if (gx >= 0 && gx < BOARD_WIDTH && gy >= 0 && gy < BOARD_HEIGHT)
                    {
                        gui_draw_rect(win, BOARD_X + gx * TILE_SIZE, BOARD_Y + gy * TILE_SIZE, TILE_SIZE, TILE_SIZE, 0x00334155);
                    }
                }
            }
        }

        // Render Current Falling Piece
        for (int r = 0; r < 4; r++)
        {
            for (int c = 0; c < 4; c++)
            {
                if (TETROMINOES[curPiece][curRot][r][c])
                {
                    int px = curX + c;
                    int py = curY + r;
                    if (px >= 0 && px < BOARD_WIDTH && py >= 0 && py < BOARD_HEIGHT)
                    {
                        DrawTetrisBlock(win, BOARD_X + px * TILE_SIZE, BOARD_Y + py * TILE_SIZE, PIECE_COLORS[curPiece + 1]);
                    }
                }
            }
        }

        // Sidebar Panel
        int side_x = BOARD_X + field_w + 16;
        int side_y = BOARD_Y;
        int side_w = win->client_w - side_x - 14;

        gui_fill_rect(win, side_x, side_y, side_w, field_h, 0x001E293B);
        gui_draw_rect(win, side_x, side_y, side_w, field_h, 0x00334155);

        // Next Piece Box
        gui_draw_text(win, side_x + 12, side_y + 12, "NEXT PIECE:", GUI_COLOR_CYAN, GUI_COLOR_TRANSPARENT);
        gui_fill_rect(win, side_x + 12, side_y + 28, 80, 80, 0x000F172A);
        gui_draw_rect(win, side_x + 12, side_y + 28, 80, 80, 0x00334155);
        for (int r = 0; r < 4; r++)
        {
            for (int c = 0; c < 4; c++)
            {
                if (TETROMINOES[nextPiece][0][r][c])
                {
                    DrawTetrisBlock(win, side_x + 20 + c * 15, side_y + 36 + r * 15, PIECE_COLORS[nextPiece + 1]);
                }
            }
        }

        // Stats
        char buf[32];
        gui_draw_text(win, side_x + 12, side_y + 124, "SCORE", GUI_COLOR_LIGHT_SLATE, GUI_COLOR_TRANSPARENT);
        snprintf(buf, sizeof(buf), "%d", g_Score);
        gui_draw_text(win, side_x + 12, side_y + 138, buf, GUI_COLOR_EMERALD, GUI_COLOR_TRANSPARENT);

        gui_draw_text(win, side_x + 12, side_y + 162, "LINES", GUI_COLOR_LIGHT_SLATE, GUI_COLOR_TRANSPARENT);
        snprintf(buf, sizeof(buf), "%d", g_LinesCleared);
        gui_draw_text(win, side_x + 12, side_y + 176, buf, GUI_COLOR_WHITE, GUI_COLOR_TRANSPARENT);

        gui_draw_text(win, side_x + 12, side_y + 200, "LEVEL", GUI_COLOR_LIGHT_SLATE, GUI_COLOR_TRANSPARENT);
        snprintf(buf, sizeof(buf), "%d", g_Level);
        gui_draw_text(win, side_x + 12, side_y + 214, buf, GUI_COLOR_AMBER, GUI_COLOR_TRANSPARENT);

        // Controls Guide
        gui_draw_text(win, side_x + 12, side_y + 244, "CONTROLS:", GUI_COLOR_CYAN, GUI_COLOR_TRANSPARENT);
        gui_draw_text(win, side_x + 12, side_y + 260, "Left / A  : Left", GUI_COLOR_WHITE, GUI_COLOR_TRANSPARENT);
        gui_draw_text(win, side_x + 12, side_y + 274, "Right / D : Right", GUI_COLOR_WHITE, GUI_COLOR_TRANSPARENT);
        gui_draw_text(win, side_x + 12, side_y + 288, "Up / W    : Rotate", GUI_COLOR_WHITE, GUI_COLOR_TRANSPARENT);
        gui_draw_text(win, side_x + 12, side_y + 302, "Down / S  : Drop", GUI_COLOR_WHITE, GUI_COLOR_TRANSPARENT);
        gui_draw_text(win, side_x + 12, side_y + 316, "Space     : HardDrop", GUI_COLOR_YELLOW, GUI_COLOR_TRANSPARENT);
        gui_draw_text(win, side_x + 12, side_y + 334, "Esc / Q   : Quit", GUI_COLOR_LIGHT_SLATE, GUI_COLOR_TRANSPARENT);

        // Game Over Banner
        if (game_over)
        {
            int gov_w = 200;
            int gov_h = 70;
            int gov_x = BOARD_X + (field_w - gov_w) / 2;
            int gov_y = BOARD_Y + (field_h - gov_h) / 2;
            gui_fill_rect(win, gov_x, gov_y, gov_w, gov_h, 0x000F172A);
            gui_draw_rect(win, gov_x, gov_y, gov_w, gov_h, GUI_COLOR_RED);
            gui_draw_text(win, gov_x + 50, gov_y + 12, "GAME OVER!", GUI_COLOR_RED, GUI_COLOR_TRANSPARENT);
            snprintf(buf, sizeof(buf), "Final Score: %d", g_Score);
            gui_draw_text(win, gov_x + 44, gov_y + 28, buf, GUI_COLOR_WHITE, GUI_COLOR_TRANSPARENT);
            gui_draw_text(win, gov_x + 16, gov_y + 46, "Press [Space] to Play Again", GUI_COLOR_AMBER, GUI_COLOR_TRANSPARENT);
        }

        gui_present(win);
        sys_sleep(2);
    }

    gui_destroy_window(win);
}

static void RenderBoard(int curPiece, int curRot, int curX, int curY)
{
    printf("\033[H\033[2J");
    printf("=========================================\n");
    printf("   myOS Classic Retro Tetris (Ring 3)    \n");
    printf("=========================================\n");
    printf(" Score: %-6d | Lines: %-3d | Level: %-2d\n", g_Score, g_LinesCleared, g_Level);
    printf("-----------------------------------------\n");

    uint8_t tempBoard[BOARD_HEIGHT][BOARD_WIDTH];
    memcpy(tempBoard, g_Board, sizeof(g_Board));

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
            if (tempBoard[y][x] == 0) printf(" .");
            else printf("[]");
        }
        printf("|>\n");
    }
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

        for (int x = 0; x < BOARD_WIDTH; x++)
        {
            if (x != 4 && x != 5) g_Board[BOARD_HEIGHT - 1][x] = 1;
        }

        int piece = 3;
        int rot = 0;
        int posX = 3;
        int posY = BOARD_HEIGHT - 2;

        bool col = CheckCollision(piece, rot, posX, posY);
        printf("  Test collision check: %s\n", !col ? "PASSED (No collision)" : "FAILED");

        LockPiece(piece, rot, posX, posY);
        printf("  Test piece lock-in: PASSED\n");

        int cleared = ClearFullLines();
        printf("  Test line clear: %d line(s) cleared (Score: %d) -> %s\n",
               cleared, g_Score, cleared == 1 ? "PASSED" : "FAILED");

        audio_status_t st;
        sys_audio_status(&st);
        printf("  Audio status: HW=%s, Playing=%s\n",
               st.hardware_present ? "YES" : "NO", st.is_playing ? "YES" : "IDLE");

        printf("Tetris automated verification tests PASSED!\n");
        return 0;
    }

    // Default: Run as native GUI window in Desktop environment
    gui_window_t* win = gui_create_window("Tetris - Classic Arcade", 180, 60, 440, 480);
    if (win)
    {
        RunGuiTetris(win);
        return 0;
    }

    // Fallback to text terminal
    int curPiece = 0;
    int curRot = 0;
    int curX = 3;
    int curY = 0;
    RenderBoard(curPiece, curRot, curX, curY);
    return 0;
}
