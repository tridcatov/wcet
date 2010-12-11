
struct pair_of_ints { int min; int max; };
struct bool_maybe { bool value; bool defined; };

extern pair_of_ints l1_i;
extern bool_maybe l2_flag;

int foo();

#include <iostream>
#include <assert.h>
using namespace std;

int main()
{
    l1_i.min = 1;
    l1_i.max = 4;    
    foo();
    
    assert(l2_flag.defined);
    assert(l2_flag.value == false);	
    
    l1_i.min = -4;
    l1_i.max = -2;    
    foo();
    
    assert(l2_flag.defined);
    assert(l2_flag.value == true);	
    
    l1_i.min = -4;
    l1_i.max = 4;    
    foo();
    
    assert(l2_flag.defined == false);

    
    
    
    
    

    cout << "Tests passed\n";    
//    cout << "Defined = " << l2_flag.defined << " value = " << l2_flag.value << "\n";
    return 0;
}