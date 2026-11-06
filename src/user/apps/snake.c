#include "../libuser.h"
#include <libgui.h>

#define GRID_COLS 22
#define GRID_ROWS 18
#define TILE_SIZE 14
#define FIELD_X   14
#define FIELD_Y   36

typedef struct Segment {
    int x;
    int y;
    struct Segment* next;
} Segment;

static void RunInteractiveSnake(void)
{
    printf("\033[H\033[2J");
    printf("========================================\n");
    printf("   myOS Interactive Retro Snake Game    \n");
    printf("========================================\n");
    printf(" Controls: W (Up), A (Left), S (Down), D (Right), Q (Quit)\n\n");

    int head_x = 10, head_y = 10;
    int dir_x = 1, dir_y = 0;
    int food_x = 15, food_y = 8;
    int score = 0;

    Segment* head = (Segment*)malloc(sizeof(Segment));
    head->x = head_x;
    head->y = head_y;
    head->next = NULL;

    while (1)
    {
        char c = 0;
        if (sys_read(&c, 1) > 0 && c != 0)
        {
            if (c == 'q' || c == 'Q') break;
            else if ((c == 'w' || c == 'W') && dir_y != 1) { dir_x = 0; dir_y = -1; }
            else if ((c == 's' || c == 'S') && dir_y != -1) { dir_x = 0; dir_y = 1; }
            else if ((c == 'a' || c == 'A') && dir_x != 1) { dir_x = -1; dir_y = 0; }
            else if ((c == 'd' || c == 'D') && dir_x != -1) { dir_x = 1; dir_y = 0; }
        }

        head_x += dir_x;
        head_y += dir_y;

        if (head_x < 0 || head_x >= 25 || head_y < 0 || head_y >= 16)
        {
            printf("\n*** CRASH! Snake hit the wall! Game Over! Score: %d ***\n", score);
            audio_play_wav("/sounds/boom.wav", false);
            break;
        }

        bool ate_food = (head_x == food_x && head_y == food_y);
        Segment* new_head = (Segment*)malloc(sizeof(Segment));
        new_head->x = head_x;
        new_head->y = head_y;
        new_head->next = head;
        head = new_head;

        if (ate_food)
        {
            score += 10;
            printf("Ate food at (%d,%d)! Score: %d\n", food_x, food_y, score);
            audio_play_wav("/sounds/coin.wav", false);
            food_x = (food_x + 7) % 24 + 1;
            food_y = (food_y + 5) % 14 + 1;
        }
        else
        {
            Segment* prev = head;
            while (prev->next && prev->next->next) prev = prev->next;
            if (prev->next)
            {
                free(prev->next);
                prev->next = NULL;
            }
        }

        sys_sleep(150);
    }

    while (head)
    {
        Segment* next = head->next;
        free(head);
        head = next;
    }
}

static void RunGuiSnake(gui_window_t* win)
{
    int head_x = 5, head_y = 9;
    int dir_x = 1, dir_y = 0;
    int next_dir_x = 1, next_dir_y = 0;
    int food_x = 15, food_y = 9;
    int score = 0;
    int high_score = 0;
    int snake_len = 3;
    bool game_over = false;

    Segment* head = (Segment*)malloc(sizeof(Segment));
    head->x = head_x;
    head->y = head_y;
    head->next = NULL;

    // Initial body segments
    Segment* tail = head;
    for (int i = 1; i <= 2; i++)
    {
        Segment* s = (Segment*)malloc(sizeof(Segment));
        s->x = head_x - i;
        s->y = head_y;
        s->next = NULL;
        tail->next = s;
        tail = s;
    }

    uint32_t last_step_tick = sys_getticks();

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
                        // Clean existing
                        while (head) { Segment* n = head->next; free(head); head = n; }
                        head_x = 5; head_y = 9;
                        dir_x = 1; dir_y = 0;
                        next_dir_x = 1; next_dir_y = 0;
                        food_x = 15; food_y = 9;
                        score = 0;
                        snake_len = 3;
                        game_over = false;

                        head = (Segment*)malloc(sizeof(Segment));
                        head->x = head_x; head->y = head_y; head->next = NULL;
                        tail = head;
                        for (int i = 1; i <= 2; i++)
                        {
                            Segment* s = (Segment*)malloc(sizeof(Segment));
                            s->x = head_x - i; s->y = head_y; s->next = NULL;
                            tail->next = s; tail = s;
                        }
                    }
                }
                else
                {
                    // Movement Controls (WASD & Arrows)
                    if ((k == 'w' || ev.key_code == 0xC8 || ev.key_code == 0x11) && dir_y != 1)
                    {
                        next_dir_x = 0; next_dir_y = -1;
                    }
                    else if ((k == 's' || ev.key_code == 0xD0 || ev.key_code == 0x1F) && dir_y != -1)
                    {
                        next_dir_x = 0; next_dir_y = 1;
                    }
                    else if ((k == 'a' || ev.key_code == 0xCB || ev.key_code == 0x1E) && dir_x != 1)
                    {
                        next_dir_x = -1; next_dir_y = 0;
                    }
                    else if ((k == 'd' || ev.key_code == 0xCD || ev.key_code == 0x20) && dir_x != -1)
                    {
                        next_dir_x = 1; next_dir_y = 0;
                    }
                }
            }
        }

        // 2. Fixed-rate game step (~9 Hz speed)
        uint32_t now = sys_getticks();
        if (!game_over && (now - last_step_tick >= 11))
        {
            last_step_tick = now;
            dir_x = next_dir_x;
            dir_y = next_dir_y;

            head_x += dir_x;
            head_y += dir_y;

            // Wall collision check
            if (head_x < 0 || head_x >= GRID_COLS || head_y < 0 || head_y >= GRID_ROWS)
            {
                game_over = true;
                audio_play_wav("/sounds/boom.wav", false);
                if (score > high_score) high_score = score;
            }
            else
            {
                // Self-collision check
                Segment* check = head;
                while (check)
                {
                    if (check->x == head_x && check->y == head_y)
                    {
                        game_over = true;
                        audio_play_wav("/sounds/boom.wav", false);
                        if (score > high_score) high_score = score;
                        break;
                    }
                    check = check->next;
                }
            }

            if (!game_over)
            {
                bool ate = (head_x == food_x && head_y == food_y);
                Segment* new_h = (Segment*)malloc(sizeof(Segment));
                new_h->x = head_x;
                new_h->y = head_y;
                new_h->next = head;
                head = new_h;

                if (ate)
                {
                    score += 10;
                    snake_len++;
                    if (score > high_score) high_score = score;
                    audio_play_wav("/sounds/coin.wav", false);

                    // Place new food at random non-snake cell
                    bool on_snake;
                    int attempts = 0;
                    do {
                        food_x = (head_x * 7 + score + attempts * 3) % GRID_COLS;
                        food_y = (head_y * 11 + attempts * 5) % GRID_ROWS;
                        on_snake = false;
                        Segment* cur = head;
                        while (cur)
                        {
                            if (cur->x == food_x && cur->y == food_y)
                            {
                                on_snake = true;
                                break;
                            }
                            cur = cur->next;
                        }
                        attempts++;
                    } while (on_snake && attempts < 50);
                }
                else
                {
                    // Remove tail
                    Segment* cur = head;
                    while (cur->next && cur->next->next) cur = cur->next;
                    if (cur->next)
                    {
                        free(cur->next);
                        cur->next = NULL;
                    }
                }
            }
        }

        // 3. Render GUI Frame
        gui_clear(win, 0x000F172A); // Dark navy background

        // Header Title
        gui_draw_text(win, 14, 10, "myOS Snake Arcade", GUI_COLOR_CYAN, GUI_COLOR_TRANSPARENT);
        gui_draw_text(win, 180, 10, "Ring 3 Multi-Core Game", GUI_COLOR_LIGHT_SLATE, GUI_COLOR_TRANSPARENT);

        // Playing Field Background & Border
        int field_w = GRID_COLS * TILE_SIZE;
        int field_h = GRID_ROWS * TILE_SIZE;
        gui_fill_rect(win, FIELD_X, FIELD_Y, field_w, field_h, 0x00020617);
        gui_draw_rect(win, FIELD_X - 1, FIELD_Y - 1, field_w + 2, field_h + 2, 0x00334155);

        // Grid lines (subtle)
        for (int r = 1; r < GRID_ROWS; r++)
        {
            gui_draw_line(win, FIELD_X, FIELD_Y + r * TILE_SIZE, FIELD_X + field_w, FIELD_Y + r * TILE_SIZE, 0x000B132B);
        }
        for (int c = 1; c < GRID_COLS; c++)
        {
            gui_draw_line(win, FIELD_X + c * TILE_SIZE, FIELD_Y, FIELD_X + c * TILE_SIZE, FIELD_Y + field_h, 0x000B132B);
        }

        // Draw Food (Apple)
        int fx = FIELD_X + food_x * TILE_SIZE;
        int fy = FIELD_Y + food_y * TILE_SIZE;
        gui_fill_rect(win, fx + 2, fy + 2, TILE_SIZE - 4, TILE_SIZE - 4, 0x00EF4444); // Crimson red
        gui_draw_rect(win, fx + 1, fy + 1, TILE_SIZE - 2, TILE_SIZE - 2, 0x00B91C1C);
        gui_put_pixel(win, fx + TILE_SIZE / 2, fy + 1, 0x0022C55E); // Green stem

        // Draw Snake Body
        Segment* cur = head ? head->next : NULL;
        int seg_idx = 0;
        while (cur)
        {
            int sx = FIELD_X + cur->x * TILE_SIZE;
            int sy = FIELD_Y + cur->y * TILE_SIZE;
            uint32_t body_col = (seg_idx % 2 == 0) ? 0x0010B981 : 0x00059669;
            gui_fill_rect(win, sx + 1, sy + 1, TILE_SIZE - 2, TILE_SIZE - 2, body_col);
            gui_draw_rect(win, sx + 1, sy + 1, TILE_SIZE - 2, TILE_SIZE - 2, 0x00047857);
            cur = cur->next;
            seg_idx++;
        }

        // Draw Snake Head
        if (head)
        {
            int hx = FIELD_X + head->x * TILE_SIZE;
            int hy = FIELD_Y + head->y * TILE_SIZE;
            gui_fill_rect(win, hx + 1, hy + 1, TILE_SIZE - 2, TILE_SIZE - 2, 0x0034D399); // Light emerald
            gui_draw_rect(win, hx, hy, TILE_SIZE, TILE_SIZE, 0x00059669);

            // Eyes
            gui_put_pixel(win, hx + 4, hy + 4, GUI_COLOR_WHITE);
            gui_put_pixel(win, hx + 9, hy + 4, GUI_COLOR_WHITE);
            gui_put_pixel(win, hx + 4, hy + 5, GUI_COLOR_BLACK);
            gui_put_pixel(win, hx + 9, hy + 5, GUI_COLOR_BLACK);
        }

        // Sidebar Info Panel
        int side_x = FIELD_X + field_w + 14;
        int side_y = FIELD_Y;
        int side_w = win->client_w - side_x - 12;

        gui_fill_rect(win, side_x, side_y, side_w, field_h, 0x001E293B);
        gui_draw_rect(win, side_x, side_y, side_w, field_h, 0x00334155);

        gui_draw_text(win, side_x + 10, side_y + 12, "SCORE", GUI_COLOR_LIGHT_SLATE, GUI_COLOR_TRANSPARENT);
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", score);
        gui_draw_text(win, side_x + 10, side_y + 26, buf, GUI_COLOR_EMERALD, GUI_COLOR_TRANSPARENT);

        gui_draw_text(win, side_x + 10, side_y + 48, "HIGH SCORE", GUI_COLOR_LIGHT_SLATE, GUI_COLOR_TRANSPARENT);
        snprintf(buf, sizeof(buf), "%d", high_score);
        gui_draw_text(win, side_x + 10, side_y + 62, buf, GUI_COLOR_AMBER, GUI_COLOR_TRANSPARENT);

        gui_draw_text(win, side_x + 10, side_y + 84, "LENGTH", GUI_COLOR_LIGHT_SLATE, GUI_COLOR_TRANSPARENT);
        snprintf(buf, sizeof(buf), "%d", snake_len);
        gui_draw_text(win, side_x + 10, side_y + 98, buf, GUI_COLOR_WHITE, GUI_COLOR_TRANSPARENT);

        gui_draw_text(win, side_x + 10, side_y + 130, "CONTROLS:", GUI_COLOR_CYAN, GUI_COLOR_TRANSPARENT);
        gui_draw_text(win, side_x + 10, side_y + 146, "W / Up", GUI_COLOR_WHITE, GUI_COLOR_TRANSPARENT);
        gui_draw_text(win, side_x + 10, side_y + 160, "S / Down", GUI_COLOR_WHITE, GUI_COLOR_TRANSPARENT);
        gui_draw_text(win, side_x + 10, side_y + 174, "A / Left", GUI_COLOR_WHITE, GUI_COLOR_TRANSPARENT);
        gui_draw_text(win, side_x + 10, side_y + 188, "D / Right", GUI_COLOR_WHITE, GUI_COLOR_TRANSPARENT);
        gui_draw_text(win, side_x + 10, side_y + 210, "Space: Restart", GUI_COLOR_LIGHT_SLATE, GUI_COLOR_TRANSPARENT);
        gui_draw_text(win, side_x + 10, side_y + 224, "Esc: Quit", GUI_COLOR_LIGHT_SLATE, GUI_COLOR_TRANSPARENT);

        // Game Over Banner
        if (game_over)
        {
            int gov_w = 220;
            int gov_h = 70;
            int gov_x = FIELD_X + (field_w - gov_w) / 2;
            int gov_y = FIELD_Y + (field_h - gov_h) / 2;
            gui_fill_rect(win, gov_x, gov_y, gov_w, gov_h, 0x000F172A);
            gui_draw_rect(win, gov_x, gov_y, gov_w, gov_h, GUI_COLOR_RED);
            gui_draw_text(win, gov_x + 60, gov_y + 12, "GAME OVER!", GUI_COLOR_RED, GUI_COLOR_TRANSPARENT);
            snprintf(buf, sizeof(buf), "Final Score: %d", score);
            gui_draw_text(win, gov_x + 54, gov_y + 28, buf, GUI_COLOR_WHITE, GUI_COLOR_TRANSPARENT);
            gui_draw_text(win, gov_x + 22, gov_y + 46, "Press [Space] to Play Again", GUI_COLOR_AMBER, GUI_COLOR_TRANSPARENT);
        }

        gui_present(win);
        sys_sleep(2);
    }

    while (head)
    {
        Segment* next = head->next;
        free(head);
        head = next;
    }

    gui_destroy_window(win);
}

int main(int argc, char* argv[])
{
    if (argc >= 2 && (strcmp(argv[1], "-i") == 0 || strcmp(argv[1], "--interactive") == 0))
    {
        RunInteractiveSnake();
        return 0;
    }

    // Automated test mode
    if (argc >= 2 && (strcmp(argv[1], "--test") == 0 || strcmp(argv[1], "-t") == 0))
    {
        printf("\n=== myOS Snake Game (Ring 3 User Mode) ===\n");
        printf("Simulating Snake with dynamic heap-allocated linked lists...\n");

        srand(sys_getticks());

        Segment* head = (Segment*)malloc(sizeof(Segment));
        head->x = 10; head->y = 10; head->next = NULL;

        Segment* tail = head;
        for (int i = 1; i <= 3; i++)
        {
            Segment* seg = (Segment*)malloc(sizeof(Segment));
            seg->x = 10 - i; seg->y = 10; seg->next = NULL;
            tail->next = seg; tail = seg;
        }

        int food_x = 14, food_y = 10, score = 0;
        for (int s = 1; s <= 5; s++)
        {
            int new_x = head->x + 1;
            int new_y = head->y;
            bool ate_food = (new_x == food_x && new_y == food_y);
            Segment* new_head = (Segment*)malloc(sizeof(Segment));
            new_head->x = new_x; new_head->y = new_y; new_head->next = head;
            head = new_head;

            if (ate_food)
            {
                score += 10;
                audio_play_wav("/sounds/coin.wav", false);
                food_x = 20; food_y = 12;
            }
            else
            {
                Segment* prev = head;
                while (prev->next && prev->next->next) prev = prev->next;
                if (prev->next) { free(prev->next); prev->next = NULL; }
            }
        }

        while (head) { Segment* next = head->next; free(head); head = next; }
        printf("Simulation complete! Final Score: %d\n", score);
        return score;
    }

    // Default: Run as native GUI window in Desktop environment
    gui_window_t* win = gui_create_window("Snake - Retro Arcade", 150, 80, 460, 360);
    if (win)
    {
        RunGuiSnake(win);
        return 0;
    }

    // Fallback to interactive console
    RunInteractiveSnake();
    return 0;
}
