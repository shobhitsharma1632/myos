#include "string.h"

size_t strlen(const char* str)
{
    size_t len = 0;
    while (str && str[len]) len++;
    return len;
}

int strcmp(const char* s1, const char* s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char* strcpy(char* dst, const char* src)
{
    char* orig = dst;
    while ((*dst++ = *src++));
    return orig;
}

char* strncpy(char* dst, const char* src, size_t n)
{
    char* orig = dst;
    while (n > 0 && *src)
    {
        *dst++ = *src++;
        n--;
    }
    while (n > 0)
    {
        *dst++ = '\0';
        n--;
    }
    return orig;
}

const char* strchr(const char* str, char chr)
{
    if (str == NULL)
        return NULL;

    while (*str)
    {
        if (*str == chr)
            return str;
        str++;
    }

    return NULL;
}
