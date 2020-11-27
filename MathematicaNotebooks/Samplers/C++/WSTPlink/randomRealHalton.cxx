
#include "wstp.h"
extern int WSMain(int, char **);

#include <stddef.h>
#include <math.h>

__uint32_t wangHash(__uint32_t x)
{
    x = (x ^ 61) ^ (x >> 16);
    x *= 9;
    x ^= x >> 4;
    x *= 0x27d4eb2dU;
    x ^= x >> 15;

    return x;
}

float halton3(__uint32_t index)
{
    float result = 0.0f;
    float scale = 1.0f;
    while (index != 0)
    {
        scale /= 3;
		__uint32_t xx = index%3;
        result += xx * scale;
        index /= 3;
    }

    return result;
}

double randomRealHalton(int p)
{
    __uint32_t hp = wangHash(p);
    return (double)halton3(hp);
}


/// ////////////////////////////
int main(int argc, char* argv[])
{
	return WSMain(argc, argv);
}