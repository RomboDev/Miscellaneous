#include <stddef.h>
#include <array>
#include "utils.hpp"

// Original idea from Martin Roberts ...
// http://extremelearning.com.au/isotropic-blue-noise-point-sets/

// Reference Java implementation from Tommy Ettinger ...
// https://github.com/tommyettinger/sarong/blob/master/src/test/java/sarong/PermutationEtc.java



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Permutation class                                                                                               //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MRperm
{
public:
    const static unsigned int SIZE {32}, HALF_SIZE {SIZE >> 1}, COUNT{100};

    MRperm() = default;
    MRperm(loooong state){ stateA = state; }
    ~MRperm() = default;

    /* Returns a slightly-biased pseudo-random int between 0 inclusive and bound exclusive. 
    ** The bias comes from not completely implementing Daniel Lemire's fastrange algorithm, 
    ** but it should only be relevant for huge bounds. The number generator itself passes 
    ** PractRand without anomalies, has a state size of 127 bits, and a period of 2 to the 127.
    ** @param bound upper exclusive bound
    ** @return an int between 0 (inclusive) and bound (exclusive) */
    int nextIntBounded (int bound) {
		const loooong s = (stateA += UINT64_C(0xC6BC279692B5C323));
		const loooong z = ((s < UINT64_C(0x800000006F17146D)) ? stateB : (stateB += UINT64_C(0x9479D2858AF899E6))) * (s ^ s >> 31);
		return (int)(bound * ((z ^ z >> 25) & UINT64_C(0xFFFFFFFF)) >> 32);
	}

    /* Fisher-Yates and/or Knuth shuffle, done in-place on an int array.
    ** @param elements will be modified in-place by a relatively fair shuffle */
	void shuffleInPlace(int elements [], int size) {
		for (int i = size; i > 1; i--) {
			swap(elements, i - 1, nextIntBounded(i));
		}
	}

    // Same as above but templated for std::array
    template<typename T, size_t N>
    void shuffleInPlace(std::array<T, N> &elements) {
        const int size = elements.size();
		for (int i = size; i > 1; i--) {
			swap(elements, i - 1, nextIntBounded(i));
        }
    }

private:
    loooong stateA = UINT64_C(12345678987654321);
    loooong stateB = UINT64_C(0x1337DEADBEEF);

    static void swap(int arr [], int pos1, int pos2) {
		const int tmp = arr[pos1];
		arr[pos1] = arr[pos2];
		arr[pos2] = tmp;
	}

    // Same as above but templated for std::array
    template<typename T, size_t N>
    static void swap(std::array<T, N> &arr, int pos1, int pos2) {
		const int tmp = arr[pos1];
		arr[pos1] = arr[pos2];
		arr[pos2] = tmp;
	}
};



#define LETSUSELAMBDAS
//#define USESTDARRAYS
//#define JUSTDEBUGARRAYCOMPARISON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[]) 
{
    MRperm p; // use below ctr for diff seqs
    //MRperm p (aux::Timer::nanoTime());


#ifdef USESTDARRAYS
    std::array<int, MRperm::COUNT*MRperm::SIZE>  allItems;
    std::array<int, MRperm::SIZE>                delta;
    std::array<int, MRperm::SIZE>                targets;
#else
    int allItems    [MRperm::COUNT*MRperm::SIZE];
    int delta       [MRperm::SIZE];
    int targets     [MRperm::SIZE];
#endif

    //
    const int LIMIT = 10000 * MRperm::COUNT * MRperm::COUNT;
    float store_elapsed_single_perm = 0.f; 
    float max_elapsed_single_perm = 0.f;


#ifndef LETSUSELAMBDAS
    // Start permutating //
	aux::Timer timer_whole(true);
    for (int g = 0; g < MRperm::COUNT; g++) 
    {
	    aux::Timer timer_perm(true);

#ifdef USESTDARRAYS
        std::array<int, MRperm::SIZE> items;
#else
        // store contiguos items into allItems
        int * items = &allItems[g*MRperm::SIZE];
#endif

        BIG_LOOP:
        for (int repeat = 0; repeat < LIMIT; repeat++) 
        {

            for (int i = 0; i < MRperm::HALF_SIZE; i++) {
				delta[i] = i + 1;
				delta[i + MRperm::HALF_SIZE] = ~i;
			}
#ifdef USESTDARRAYS
            p.shuffleInPlace(delta);
#else
            p.shuffleInPlace(delta, MRperm::SIZE);
#endif
            for (int i = 0; i < MRperm::SIZE; i++) {
                targets[i] = i + 1; 
            }
            targets[(items[0] = p.nextIntBounded(MRperm::SIZE) + 1) - 1] = 0;

            for (int i = 1; i < MRperm::SIZE; i++) {
                int d = 0;
                for (int j = 0; j < MRperm::SIZE; j++) {
                    d = delta[j];
                    if(d == 0) continue;
                    int t = items[i-1] + d;
                    if(t > 0 && t <= MRperm::SIZE && targets[t-1] != 0){
                        items[i] = t;
                        targets[t-1] = 0;
                        delta[j] = 0;
                        break;
                    }
                    else d = 0;
                }
                if(d == 0) goto BIG_LOOP;
            }

            // record time for this very permutation
            auto elapsed_p = timer_perm.Elapsed(); //end time recording
            float elapsed_p_secs = (elapsed_p.count()*0.001);
            std::cout << aux::Modifier(aux::FG_GREEN) << "\nSequences with size "<< MRperm::SIZE 
                                        << " took: " << elapsed_p_secs << " seconds" << std::endl;
            store_elapsed_single_perm += elapsed_p_secs; //store for average time and below for max
            if(elapsed_p_secs > max_elapsed_single_perm) max_elapsed_single_perm = elapsed_p_secs;

            //
            int d = items[0] - items[MRperm::SIZE - 1];
            for (int j = 0; j < MRperm::SIZE; j++) {
                if(d == delta[j]) 
                {   
                    // distinct array check
                    // items[g] vs allItems[0to(g-1)]
#ifdef JUSTDEBUGARRAYCOMPARISON
                    std::cout << "\n" << g << std::endl;
                    std::cout << aux::Modifier(aux::FG_YELLOW) << "checking this.." << std::endl;
                    for (int k=0; k<MRperm::SIZE; k++) {
                        std::cout << items[k] << ", ";
                    }
                    std::cout << aux::Modifier(aux::FG_GREEN) << "\nagainst.." << std::endl;
#endif

#ifdef USESTDARRAYS
                    // copy current items to allItems
                    std::copy(items.begin(), items.end(), &allItems[g*MRperm::SIZE]);
#endif
                    for (int i = 0; i < g; i++) {

#ifdef USESTDARRAYS
                        // retrive distinct array from allItems
                        std::array<int, MRperm::SIZE> itemx;
                        auto begin = allItems.data()+(i)*MRperm::SIZE;
                        std::copy(begin, begin+MRperm::SIZE, itemx.begin());

                        if(items==itemx)    //std::array don't decay to ptrs
                        {                   //so we can check for equality
                            std::cout << "OPS! Got a duplicated array !" << std::endl;
                            goto BIG_LOOP;
                        }
    #ifdef JUSTDEBUGARRAYCOMPARISON
                        for (int x=0; x<MRperm::SIZE; x++)
                            std::cout << itemx[x] << ", ";
    #endif
#else
                        auto theyarethesame = true;
                        // retrive distinct starting array address from allItems
                        int * itemx = &allItems[(i)*MRperm::SIZE];
                        for (int x=0; x<MRperm::SIZE; x++) {
    #ifdef JUSTDEBUGARRAYCOMPARISON
                            std::cout << itemx[x] << ", ";
                        }
    #else
                            // check item by item
                            if(items[x] != itemx[x]) 
                            {
                                theyarethesame = false;
                                break;
                            }
                        }
                        if(theyarethesame){
                            std::cout << "OPS! Got a duplicated array !?" << std::endl;
                            goto BIG_LOOP;
                        }
    #endif

#endif

#ifdef JUSTDEBUGARRAYCOMPARISON
                        std::cout << std::endl;

                    } // end distinct array check
#else                   
                    }
                    std::cout << g+1 << ") ";
                    std::cout << aux::Modifier(aux::FG_YELLOW) << items[0];
                    for (int i = 1; i < MRperm::SIZE; i++) {
                        std::cout << ", " << items[i];
                    }
                    std::cout << std::endl;
#endif
                    goto END_BIG_LOOP;
                    //break;
                }
            }
        }
        END_BIG_LOOP: 
#ifdef USESTDARRAYS
        {}; 
#else
        items = nullptr;
#endif
    }


#else // let's use lambdas !


//////////////////////////////////////////////////////////

    // Start permutating //
	aux::Timer timer_whole(true);
    for (int g = 0; g < MRperm::COUNT; g++) 
    {
	    aux::Timer timer_perm(true);

        // store contiguos items into allItems
        int * items = &allItems[g*MRperm::SIZE];
        for (int repeat = 0; repeat < LIMIT; repeat++) 
        {

            auto doitagain = 
            [&items, &delta, &targets, &p] 
            {
                for (int i = 0; i < MRperm::HALF_SIZE; i++) {
                    delta[i] = i + 1;
                    delta[i + MRperm::HALF_SIZE] = ~i;
                }

                p.shuffleInPlace(delta, MRperm::SIZE);

                for (int i = 0; i < MRperm::SIZE; i++) {
                    targets[i] = i + 1; 
                }
                targets[(items[0] = p.nextIntBounded(MRperm::SIZE) + 1) - 1] = 0;

                for (int i = 1; i < MRperm::SIZE; i++) {
                    int d = 0;
                    for (int j = 0; j < MRperm::SIZE; j++) {
                        d = delta[j];
                        if(d == 0) continue;
                        int t = items[i-1] + d;
                        if(t > 0 && t <= MRperm::SIZE && targets[t-1] != 0){
                            items[i] = t;
                            targets[t-1] = 0;
                            delta[j] = 0;
                            break;
                        }
                        else d = 0;
                    }
                    if(d == 0)  return true;
                }
                return false;
            }();
            if(doitagain) continue;


            // check sq
            auto sequenceisok = 
            [&allItems, &items, &delta, g] 
            {
                int d = items[0] - items[MRperm::SIZE - 1];
                for (int j = 0; j < MRperm::SIZE; j++) 
                {
                    if(d == delta[j]) 
                    {   
                        // distinct array check
                        // items[g] vs allItems[0to(g-1)]
                        for (int i = 0; i < g; i++) {

                            auto theyarethesame = true;
                            // retrive starting array address from allItems
                            int * itemx = &allItems[(i)*MRperm::SIZE];
                            for (int x=0; x<MRperm::SIZE; x++) {
                                // check item by item
                                if(items[x] != itemx[x]) 
                                {
                                    theyarethesame = false;
                                    break;
                                }
                            }
                            if(theyarethesame){
                                std::cout << "OPS! Got a duplicated array !?" << std::endl;
                                
                                //sequence is bogus..
                                //let's try again 
                                return false;
                            }
                    
                        }
                        std::cout << g+1 << ") ";
                        std::cout << aux::Modifier(aux::FG_YELLOW) << items[0];
                        for (int i = 1; i < MRperm::SIZE; i++) {
                            std::cout << ", " << items[i];
                        }
                        std::cout << std::endl;
                    
                        //sequence is ok ..
                        //let's do one another
                        return true;
                    }
                }
            }();
            if(sequenceisok){

                // record time for this very permutation
                auto elapsed_p = timer_perm.Elapsed(); //end time recording
                float elapsed_p_secs = (elapsed_p.count()*0.001);
                std::cout << aux::Modifier(aux::FG_GREEN) << "\nSequences with size "<< MRperm::SIZE 
                << " took: " << elapsed_p_secs << " seconds" << std::endl;
                store_elapsed_single_perm += elapsed_p_secs; //store for average time and below for max
                if(elapsed_p_secs > max_elapsed_single_perm) max_elapsed_single_perm = elapsed_p_secs;

                
                break;      // break big loop and get new ptr
            }
            else continue;  // engage another loop
        }     
        items = nullptr;
    }
#endif


    // Time recording output for the console
	auto elapsed = timer_whole.Elapsed(); //end time recording
    std::cout << aux::Modifier(aux::FG_GREEN) << "\nGenerating " << MRperm::COUNT << " sequences with size "<< MRperm::SIZE 
                                << " took: " << (elapsed.count()*0.001) << " seconds" << std::endl;
    std::cout << "Average time for single permutation is: "
                                << (store_elapsed_single_perm/MRperm::COUNT) << " seconds" 
                                << " (max time was " << max_elapsed_single_perm << " seconds)" << std::endl;
    std::cout << aux::Modifier(aux::FG_DEFAULT) << std::endl;

    return 0;
}