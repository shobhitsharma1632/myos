#include "../libuser.h"

typedef struct Segment {
    int x;
    int y;
    struct Segment* next;
} Segment;

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

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
