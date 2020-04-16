//#include "stddef.h"
//#include "stdio.h"

// Original idea from Martin Roberts ...
// http://extremelearning.com.au/isotropic-blue-noise-point-sets/

// Reference Java implementation from Tommy Ettinger ...
// https://github.com/tommyettinger/sarong/blob/master/src/test/java/sarong/PermutationEtc.java


// THIS IS ACTUALLY SLOWER (!) THAN THE C/C++ COUTERPARTS //


#ifndef ISPC_UINT_IS_DEFINED
typedef unsigned int64 uint64;
#endif
#define UINT64_C(c) c ## UL

#define SIZE 32
#define HALF_SIZE 16
uniform const unsigned int LIMIT = 10000 * 100 * 100;

varying uint64 stateA = UINT64_C(12345678987654321) +programIndex;
varying uint64 stateB = UINT64_C(0x1337DEADBEEF)    ;//+programIndex;

static inline int nextIntBounded (int bound) {
    const uint64 s = (stateA += UINT64_C(0xC6BC279692B5C323));
    const uint64 z = ((s < UINT64_C(0x800000006F17146D)) ? stateB : (stateB += UINT64_C(0x9479D2858AF899E6))) * (s ^ s >> 31);
    const int bounded = (int)(bound * ((z ^ z >> 25) & UINT64_C(0xFFFFFFFF)) >> 32);
    return bounded;
}

static inline void swap(int arr [], int pos1, int pos2) {
    const int tmp = arr[pos1];
    arr[pos1] = arr[pos2];
    arr[pos2] = tmp;
}

static inline void shuffleInPlace(int elements []) {
    for (int i = SIZE; i > 1; i--) {
        swap(elements, i - 1, nextIntBounded(i));
    }
}


//#define USESTDARRAYS
//#define JUSTDEBUGARRAYCOMPARISON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
export void perm (uniform int allItems[], uniform int count) 
{

    int delta       [SIZE];
    int targets     [SIZE];

    bool getback_tobigloop      = false;
    bool bigloop_should_break   = false;
    
    // Start permutating //
    foreach (index = 0 ... count)
    {   
         // store contiguos items into allItems
        int * items = &allItems[index*SIZE];

        //BIG_LOOP:
        for (int repeat = 0; repeat < LIMIT; repeat++) 
        {   
            //reset 'goto' flags
            getback_tobigloop = false;
            bigloop_should_break = false;


            for (int i = 0; i < HALF_SIZE; i++) {
				delta[i] = i + 1;
				delta[i + HALF_SIZE] = ~i;
			}
            shuffleInPlace(delta);

            for (int i = 0; i < SIZE; i++) {
                targets[i] = i + 1; 
            }
            targets[(items[0] = nextIntBounded(SIZE) + 1) - 1] = 0;

            for (int i = 1; i < SIZE; i++) {
                int d = 0;
                for (int j = 0; j < SIZE; j++) {
                    d = delta[j];
                    if(d == 0) continue;
                    int t = items[i-1] + d;
                    if(t > 0 && t <= SIZE && targets[t-1] != 0){
                        items[i] = t;
                        targets[t-1] = 0;
                        delta[j] = 0;
                        break;
                    }
                    else d = 0;
                }
                if(d == 0) {
                    getback_tobigloop = true;
                    break;
                }
            }

            //
            if(!getback_tobigloop) 
            {
                //
                int d = items[0] - items[SIZE - 1];
                for (int j = 0; j < SIZE; j++) {
                    if(d == delta[j]) 
                    {   
                    /*
                        // distinct array check
                        // items[index] vs allItems[0to(index-1)]
                        for (int i = 0; i < index; i++) {

                            bool theyarethesame = true;
                            // retrive starting array address from allItems
                            int * itemx = &allItems[(i)*SIZE];
                            for (int x=0; x<SIZE; x++) {
                                // check item by item
                                if(items[x] != itemx[x]) 
                                {
                                    theyarethesame = false;
                                    break;
                                }
                            }
                            if(theyarethesame){
                                getback_tobigloop = true;
                                break;
                            }                
                        }
                    */
                    /*
                        if(!getback_tobigloop) {
                            // print out ...
                            print("\n%i)", index+1);
                            print(" %i",items[0]);
                            for (int i = 1; i < SIZE; i++) {
                                print(", %i",items[i]);
                            } 
                        }else 
                            break;
                    */
                        // we filled array perm ..
                        // get back to a new array
                        bigloop_should_break = true;
                        break;
                    }
                }
            }
            //here from the break
            if(bigloop_should_break)
                break;
        }
        //END_BIG_LOOP: 
        items = NULL;
    }
}