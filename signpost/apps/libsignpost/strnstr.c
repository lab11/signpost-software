#include "strnstr.h"
#include <string.h>

//https://stackoverflow.com/questions/23999797/implementing-strnstr

char *strnstr(const char *big, const char *little, size_t len) {
        int i;
        size_t little_len;

        if (0 == (little_len = strnlen(little, len)))
                return (char *)big;

        for (i=0; i<=(int)(len-little_len); i++)
        {
                if ((big[0] == little[0]) &&
                        (0 == strncmp(big, little, little_len)))
                        return (char *)big;

                big++;
        }
        return NULL;
}
