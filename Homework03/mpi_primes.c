#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <mpi.h>

int end_now = 0;

void sig_handler(int signo)
{
    if (signo == SIGUSR1) {
        end_now = 1;
    }
}

//Determines primality using the worst possible method.
bool isPrime(unsigned int n)
{
  unsigned int a = 0;
  unsigned int root = sqrt(n);

  if(n % 2 == 0)
  {
    return 0;
  }

  //Iterate over 3..root of n, checking the modulo of each for a zero remainder,
  //indicating the number is not prime. If no numbers with a zero remainder are
  //found, n is prime.
  for(a=3; a <= root && !end_now; a+=2)
  {
    if(n % a == 0)
    {
      return 0;
    }
  }
  return 1;
}

int main(int argc, char **argv)
{
    int count, id;
    unsigned int a = 0;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &count);
    MPI_Comm_rank(MPI_COMM_WORLD, &id);

    signal(SIGUSR1, sig_handler);

    //Send and recieve buffers for MPI_Reduce.
    int* maxrecvbuf = calloc(sizeof(int), count);
    int* maxsendbuf = calloc(sizeof(int), 1);

    int* sumrecvbuf = calloc(sizeof(int), count);
    int* sumsendbuf = calloc(sizeof(int), 1);

    //Primes each processor has found.
    unsigned int pcount = 0;

    //Accounts for the primality of 2 on the first process.
    if(id == 0)
    {
      pcount += 1;
    }

    //What number we're calculating primes under.
    unsigned int magnitude = 10;

    // Each process will intelligently distribute numbers to check for primality to each of the cores.
    //
    // Ex.
    // count = 2
    // id 0: 3, 7, 11
    // id 1: 5, 9, 13
    for(a = 1 + (id+1) * 2; a <= 4294967291 && end_now != 1; a+=(2 * (count)))
    {
      //Evaluates true if this is the first number over the current magnitude of
      //primes we're working on. If true, will MPI_Reduce and calculate the sum
      //of all pcounts in each process and print that out for the magnitude.
      if(a > magnitude && (a - (2 * count)) < magnitude)
      {
        sumsendbuf[0] = pcount;

        MPI_Reduce(sumsendbuf, sumrecvbuf, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);

        if(id == 0 && !end_now)
        {
          printf("%u %u\n", magnitude, sumrecvbuf[0]);
        }

        magnitude *= 10;
      }

      //If a prime is found, increase the number of primes found.
      if(isPrime(a))
      {
        pcount += 1;
      }
    }

    //The highest number checked for primality by this process.
    maxsendbuf[0] = a;
    //The number of primes found by this process.
    sumsendbuf[0] = pcount;

    //Send both of those values off to be summed and maxed respectively.
    MPI_Reduce(sumsendbuf, sumrecvbuf, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(maxsendbuf, maxrecvbuf, 1, MPI_UNSIGNED, MPI_MAX, 0, MPI_COMM_WORLD);

    if(id == 0)
    {
      printf("%u %u\n", maxrecvbuf[0], sumrecvbuf[0]);
    }

    free(maxrecvbuf);
    free(maxsendbuf);
    free(sumrecvbuf);
    free(sumsendbuf);

    MPI_Finalize();

    return 0;
}
