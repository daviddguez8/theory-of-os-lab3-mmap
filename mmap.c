#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <math.h>

static size_t page_size;

// align_down - rounds a value down to an alignment
// @x: the value
// @a: the alignment (must be power of 2)
//
// Returns an aligned value.
#define align_down(x, a) ((x) & ~((typeof(x))(a) - 1))

#define AS_LIMIT	(1 << 25) // Maximum limit on virtual memory bytes
#define MAX_SQRTS	(1 << 27) // Maximum limit on sqrt table entries
static double *sqrts;

// Use this helper function as an oracle for square root values.
//args:
//the address of the array
//where in the square root to start 
static void
calculate_sqrts(double *sqrt_pos, int start, int nr)
{
  int i;

  for (i = 0; i < nr; i++) {
    //printf("Calculating sqrt of: %d, at: %p\n", start, sqrt_pos);
    sqrt_pos[i] = sqrt((double)(start + i));
  }
}

static void *mapped_page = NULL;

static void
handle_sigsegv(int sig, siginfo_t *si, void *ctx)
{
  // Your code here.
  // replace these three lines with your implementation
  //if there is a mapped page we want to unmap it
  if (mapped_page != NULL) {
    if (munmap(mapped_page, page_size) == -1) {
      fprintf(stderr, "Couldn't munmap() region for sqrt table; %s\n",
	      strerror(errno));
      exit(EXIT_FAILURE);
    }
    mapped_page = NULL;
  }
  
  uintptr_t fault_addr = (uintptr_t)si->si_addr;
  //printf("Got SIGSEGV at 0x%lx\n", fault_addr);

  //align downt the faulting address to the page size
  uintptr_t aligned = align_down(fault_addr, page_size);
  //printf("Alignment: %lx\n", aligned); 

  //map the aligned address and make sure no error occurred. Also need to use prot_write and prot_read in order to read/write from the array
  mapped_page = mmap((void*)aligned, page_size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mapped_page == MAP_FAILED) {
    fprintf(stderr, "Couldn't mmap() region for sqrt table; %s\n",
	    strerror(errno));
    exit(EXIT_FAILURE);
  }
  //if sqrts[0] is the sqrt of 0, then mapped-page-sqrts /sizeof(double) can get us the number that should start at the mapped address
  uintptr_t nextsqrt = (uintptr_t)mapped_page-(uintptr_t)sqrts;
  //printf("mapped page: %p\n", mapped_page);
  //printf("nextsqrt/8: %ld\n", nextsqrt/sizeof(double));
  calculate_sqrts((double*)aligned, nextsqrt/sizeof(double), (int)page_size/sizeof(double));
  
}

static void
setup_sqrt_region(void)
{
  struct rlimit lim = {AS_LIMIT, AS_LIMIT};
  struct sigaction act;
  // Only mapping to find a safe location for the table.
  sqrts = mmap(NULL, MAX_SQRTS * sizeof(double) + AS_LIMIT, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (sqrts == MAP_FAILED) {
    fprintf(stderr, "Couldn't mmap() region for sqrt table; %s\n",
	    strerror(errno));
    exit(EXIT_FAILURE);
  }
  
  // Now release the virtual memory to remain under the rlimit.
  if (munmap(sqrts, MAX_SQRTS * sizeof(double) + AS_LIMIT) == -1) {
    fprintf(stderr, "Couldn't munmap() region for sqrt table; %s\n",
            strerror(errno));
    exit(EXIT_FAILURE);
  }  

  

  // Set a soft rlimit on virtual address-space bytes.
  if (setrlimit(RLIMIT_AS, &lim) == -1) {
    fprintf(stderr, "Couldn't set rlimit on RLIMIT_AS; %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  // Register a signal handler to capture SIGSEGV.
  act.sa_sigaction = handle_sigsegv;
  act.sa_flags = SA_SIGINFO;
  sigemptyset(&act.sa_mask);
  if (sigaction(SIGSEGV, &act, NULL) == -1) {
    fprintf(stderr, "Couldn't set up SIGSEGV handler;, %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
}

static void
test_sqrt_region(void)
{
  int i, pos = rand() % (MAX_SQRTS - 1);
  double correct_sqrt;

  printf("Validating square root table contents...\n");
  srand(0xDEADBEEF);

  for (i = 0; i < 500000; i++) {
    //printf("%d\n", i);
    if (i % 2 == 0) {
      pos = rand() % (MAX_SQRTS - 1);
    }
    else {
      pos += 1;
    }
    calculate_sqrts(&correct_sqrt, pos, 1);
    if (sqrts[pos] != correct_sqrt) {
      fprintf(stderr, "Square root is incorrect. Expected %f, got %f.\n",
              correct_sqrt, sqrts[pos]);
      printf("Square root is incorrect. Expected %f, got %f.\n",
              correct_sqrt, sqrts[pos]);

      exit(EXIT_FAILURE);
    }
  }

  printf("All tests passed!\n");
}

int
main(int argc, char *argv[])
{
  page_size = sysconf(_SC_PAGESIZE);
  printf("page_size is %ld\n", page_size);
  setup_sqrt_region();
  test_sqrt_region();
  return 0;
}
