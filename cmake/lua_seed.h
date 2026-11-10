/* Force-included into every Lua source: a fixed string-hash seed instead of one drawn from the clock
   and ASLR, so pairs() order is the same on every run. */
#define luai_makeseed(L) 0u
