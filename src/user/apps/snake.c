#include "../libuser.h"

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

        if (head_x < 0 || head_x > 25 || head_y < 0 || head_y > 15)
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

int main(int argc, char* argv[])
{
    if (argc >= 2 && (strcmp(argv[1], "-i") == 0 || strcmp(argv[1], "--interactive") == 0))
    {
        RunInteractiveSnake();
        return 0;
    }

    printf("\n=== myOS Snake Game (Ring 3 User Mode) ===\n");
    printf("Simulating Snake with dynamic heap-allocated linked lists...\n");

    srand(sys_getticks());

    // Initialize snake head and body with malloc
    Segment* head = (Segment*)malloc(sizeof(Segment));
    head->x = 10;
    head->y = 10;
    head->next = NULL;

    Segment* tail = head;
    for (int i = 1; i <= 3; i++)
    {
        Segment* seg = (Segment*)malloc(sizeof(Segment));
        seg->x = 10 - i;
        seg->y = 10;
        seg->next = NULL;
        tail->next = seg;
        tail = seg;
    }

    int food_x = 14;
    int food_y = 10;
    int score = 0;
    int steps = 0;

    printf("Initial snake length: 4 segments (head at (%d,%d))\n", head->x, head->y);
    printf("Food located at (%d,%d)\n", food_x, food_y);

    // Simulate 5 movement steps towards food
    for (int s = 1; s <= 5; s++)
    {
        steps++;
        int new_x = head->x + 1; // Moving right
        int new_y = head->y;

        // Check food collision
        bool ate_food = (new_x == food_x && new_y == food_y);

        // Prepend new head
        Segment* new_head = (Segment*)malloc(sizeof(Segment));
        new_head->x = new_x;
        new_head->y = new_y;
        new_head->next = head;
        head = new_head;

        if (ate_food)
        {
            score += 10;
            printf("Step %d: Snake ate food at (%d,%d)! Length: 5, Score: %d\n", s, new_x, new_y, score);
            // Play Sound Blaster audio chime on food consumption
            audio_play_wav("/sounds/coin.wav", false);
            food_x = 20;
            food_y = 12;
        }
        else
        {
            // Remove tail segment and free memory
            Segment* prev = head;
            while (prev->next && prev->next->next)
            {
                prev = prev->next;
            }
            if (prev->next)
            {
                free(prev->next);
                prev->next = NULL;
            }
            printf("Step %d: Snake slithered to (%d,%d)\n", s, new_x, new_y);
        }
    }

    printf("Simulation complete! Final Score: %d\n", score);

    // Cleanup all allocated segments
    while (head)
    {
        Segment* next = head->next;
        free(head);
        head = next;
    }

    printf("Cleanly deallocated all snake segments via free(). Exiting.\n");
    return score;
}
