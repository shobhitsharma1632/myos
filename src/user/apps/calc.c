#include "../libuser.h"

int main(int argc, char* argv[])
{
    printf("\n[User Calc] Command-line Calculator (PID %d)\n", sys_getpid());

    if (argc < 4)
    {
        printf("Usage: calc <num1> <op> <num2>\n");
        printf("Supported operators: +, -, *, /, %%\n");
        return 1;
    }

    int a = atoi(argv[1]);
    char op = argv[2][0];
    int b = atoi(argv[3]);
    int result = 0;

    switch (op)
    {
        case '+':
            result = a + b;
            break;
        case '-':
            result = a - b;
            break;
        case '*':
        case 'x':
            result = a * b;
            break;
        case '/':
            if (b == 0)
            {
                printf("Error: Division by zero!\n");
                return -1;
            }
            result = a / b;
            break;
        case '%':
            if (b == 0)
            {
                printf("Error: Modulo by zero!\n");
                return -1;
            }
            result = a % b;
            break;
        default:
            printf("Error: Unknown operator '%c'\n", op);
            return 2;
    }

    printf("[User Calc] %d %c %d = %d\n\n", a, op, b, result);
    return result;
}
