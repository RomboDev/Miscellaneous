#include "ispc_perm.ispc.h"
#include "utils.hpp"

const unsigned int SIZE = 32;
const unsigned int COUNT = 100;

int main(int argc, char *argv[]) 
{
   int allItems    [COUNT*SIZE];

   aux::Timer timer_whole(true);
   ispc::perm (allItems, COUNT);
   auto elapsed = timer_whole.Elapsed(); //end time recording


   // let's see what we got
   for(int i=0;i<COUNT;i++)
   {
      std::cout << "\n" << i << ") ";
      int * items = &allItems[i*SIZE];
      for(int x=0;x<SIZE;x++) {
         std::cout << items[x] << ", ";
      }
   }
   std::cout << "\n" << std::endl;
   std::cout << aux::Modifier(aux::FG_GREEN) << "\nGenerating " << COUNT << " sequences with size "<< SIZE 
                                << " took: " << (elapsed.count()*0.001) << " seconds" << std::endl;

   return 0;
}

