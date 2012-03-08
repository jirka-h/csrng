/*
 * fips.c -- Performs FIPS 140-1/140-2 RNG tests
 *
 * Copyright (C) 2001 Philipp Rumpf
 * Copyright (C) 2012 Jirka Hladky
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define _GNU_SOURCE

#ifndef HAVE_CONFIG_H
#error Invalid or missing autoconf build environment
#endif

#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <stdio.h>

#include "csprng/fips.h"

/*
 * Names for the FIPS tests, and bitmask
 */
const char *fips_test_names[N_FIPS_TESTS] = {
  "FIPS 140-2(2001-10-10) Monobit",
  "FIPS 140-2(2001-10-10) Poker",
  "FIPS 140-2(2001-10-10) Runs",
  "FIPS 140-2(2001-10-10) Long run",
  "FIPS 140-2(2001-10-10) Continuous run"
};
const unsigned int fips_test_mask[N_FIPS_TESTS] = {
  FIPS_RNG_MONOBIT, FIPS_RNG_POKER, FIPS_RNG_RUNS,
  FIPS_RNG_LONGRUN, FIPS_RNG_CONTINUOUS_RUN
};


/* These are the startup tests suggested by the FIPS 140-1 spec section
*  4.11.1 (http://csrc.nist.gov/fips/fips1401.htm), and updated by FIPS
*  140-2 4.9, errata of 2001-10-10.  FIPS 140-2, errata of 2002-12-03 
*  removed all requirements for non-deterministic RNGs, and thus most of
*  the tests we need are not mentioned in FIPS 140-2 anymore.  We also
*  implement FIPS 140-1 4.11.2/FIPS 140-2 4.9 Continuous Run test.
* 
*  The Monobit, Poker, Runs, and Long Runs tests are implemented below.
*  This test must be run at periodic intervals to verify data is 
*  sufficiently random.  If the tests are failed the RNG module shall 
*  no longer submit data to the entropy pool, but the tests shall 
*  continue to run at the given interval.  If at a later time the RNG
*  passes all tests it shall be re-enabled for the next period.
*  
*  The reason for this is that it is not unlikely that at some time
*  during normal operation one of the tests will fail.  This does not
*  necessarily mean the RNG is not operating properly, it is just a
*  statistically rare event.  In that case we don't want to forever
*  disable the RNG, we will just leave it disabled for the period of
*  time until the tests are rerun and passed.
*
*  For the continuous run test, we need to check all bits of data, so
*  "periodic" above shall be read as "for every back-to-back block of 
*  20000 bits".  We verify 32 bits to accomodate the AMD TRNG, and
*  to reduce false positives with other TRNGs.
*/


/*
 * fips_test_store - store 8 bits of entropy in FIPS
 * 			 internal test data pool
 */
static void fips_test_store(fips_ctx_t *ctx, unsigned int rng_data)
{
  int j;

  ctx->poker[rng_data >> 4]++;
  ctx->poker[rng_data & 15]++;

  /* Note in the loop below rlength is always one less than the actual
     run length. This makes things easier. */
  for (j = 7; j >= 0; j--) {
    ctx->ones += ctx->current_bit = ((rng_data >> j) & 1);
    if (ctx->current_bit != ctx->last_bit) {
      /* If runlength is 1-6 count it in correct bucket. 0's go in
         runs[0-5] 1's go in runs[6-11] hence the 6*current_bit below */
      if (ctx->rlength < 5) {
        ctx->runs[ctx->rlength +
          (6 * ctx->current_bit)]++;
      } else {
        ctx->runs[5 + (6 * ctx->current_bit)]++;
      }

      /* Check if we just failed longrun test */
      if (ctx->rlength >= 25)
        ctx->longrun = 1;
      ctx->rlength = 0;
      /* flip the current run type */
      ctx->last_bit = ctx->current_bit;
    } else {
      ctx->rlength++;
    }
  }
}

int fips_run_rng_test (fips_ctx_t *ctx, const void *buf)
{
  int i, j;
  int rng_test = 0;
  const unsigned char *rngdatabuf;
  struct timespec cpu_s, cpu_e;

  if ( ctx->fips_statistics.track_CPU_time) clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_s);

  if (!ctx) return -1;
  if (!buf) return -1;
  rngdatabuf = (const unsigned char *)buf;

  for (i=0; i<FIPS_RNG_BUFFER_SIZE; i += 4) {
    int new32 = rngdatabuf[i] | 
      ( rngdatabuf[i+1] << 8 ) | 
      ( rngdatabuf[i+2] << 16 ) | 
      ( rngdatabuf[i+3] << 24 );
    if (new32 == ctx->last32) { 
      rng_test |= FIPS_RNG_CONTINUOUS_RUN;
      ++ctx -> fips_statistics.fips_failures[4];
    }

    ctx->last32 = new32;
    fips_test_store(ctx, rngdatabuf[i]);
    fips_test_store(ctx, rngdatabuf[i+1]);
    fips_test_store(ctx, rngdatabuf[i+2]);
    fips_test_store(ctx, rngdatabuf[i+3]);
  }

  /* add in the last (possibly incomplete) run */
  if (ctx->rlength < 5)
    ctx->runs[ctx->rlength + (6 * ctx->current_bit)]++;
  else {
    ctx->runs[5 + (6 * ctx->current_bit)]++;
    if (ctx->rlength >= 25) {
      rng_test |= FIPS_RNG_LONGRUN;
    }
  }

  if (ctx->longrun) {
    rng_test |= FIPS_RNG_LONGRUN;
    ctx->longrun = 0;
  }

  /* Ones test */
  if ((ctx->ones >= 10275) || (ctx->ones <= 9725)) {
    rng_test |= FIPS_RNG_MONOBIT;
    ++ ctx -> fips_statistics.fips_failures[0];
  }
  /* Poker calcs */
  for (i = 0, j = 0; i < 16; i++)
    j += ctx->poker[i] * ctx->poker[i];
  /* 16/5000*1563176-5000 = 2.1632  */
  /* 16/5000*1576928-5000 = 46.1696 */
  if ((j > 1576928) || (j < 1563176)) {
    rng_test |= FIPS_RNG_POKER;
    ++ctx -> fips_statistics.fips_failures[1];
  }

  if ((ctx->runs[0] < 2315) || (ctx->runs[0] > 2685) ||
      (ctx->runs[1] < 1114) || (ctx->runs[1] > 1386) ||
      (ctx->runs[2] < 527) || (ctx->runs[2] > 723) ||
      (ctx->runs[3] < 240) || (ctx->runs[3] > 384) ||
      (ctx->runs[4] < 103) || (ctx->runs[4] > 209) ||
      (ctx->runs[5] < 103) || (ctx->runs[5] > 209) ||
      (ctx->runs[6] < 2315) || (ctx->runs[6] > 2685) ||
      (ctx->runs[7] < 1114) || (ctx->runs[7] > 1386) ||
      (ctx->runs[8] < 527) || (ctx->runs[8] > 723) ||
      (ctx->runs[9] < 240) || (ctx->runs[9] > 384) ||
      (ctx->runs[10] < 103) || (ctx->runs[10] > 209) ||
      (ctx->runs[11] < 103) || (ctx->runs[11] > 209)) {
    rng_test |= FIPS_RNG_RUNS;
    ++ctx -> fips_statistics.fips_failures[2];
  }

  /* finally, clear out FIPS variables for start of next run */
  memset (ctx->poker, 0, sizeof (ctx->poker));
  memset (ctx->runs, 0, sizeof (ctx->runs));
  ctx->ones = 0;
  ctx->rlength = -1;
  ctx->current_bit = 0;


  if (rng_test) {
    ctx -> fips_statistics.bad_fips_blocks++;
    if (rng_test & FIPS_RNG_LONGRUN) ++ctx -> fips_statistics.fips_failures[3];
    //for (j = 0; j < N_FIPS_TESTS; j++) if (fips_result & fips_test_mask[j]) fips_statistics->fips_failures[j]++;
  } else {
    ctx -> fips_statistics.good_fips_blocks++;
  }

  if ( ctx->fips_statistics.track_CPU_time ) { 
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_e);   
    add_timing_difference_to_counter ( &ctx->fips_statistics.cpu_time, &cpu_s, &cpu_e );
  }

  return rng_test;
}

void fips_statistics_init(fips_statistics_type *fips_statistics, int track_CPU_time) {
  int i;

  fips_statistics->bad_fips_blocks = 0;	     
  fips_statistics->good_fips_blocks = 0;	     
  for (i=0; i<N_FIPS_TESTS; ++i) fips_statistics->fips_failures[i] = 0;
  fips_statistics->cpu_time.tv_sec = 0;
  fips_statistics->cpu_time.tv_nsec = 0;          
  if ( track_CPU_time ) {
    fips_statistics->track_CPU_time = 1;
  } else {
    fips_statistics->track_CPU_time = 0;
  }
}

void add_timing_difference_to_counter( struct timespec *counter, const struct timespec *start, const struct timespec *end ) {
  counter->tv_sec  = ( counter->tv_sec  - start->tv_sec  ) + end->tv_sec ;
  counter->tv_nsec = ( counter->tv_nsec - start->tv_nsec ) + end->tv_nsec;
  if ( counter->tv_nsec < 0 ) {
    counter->tv_nsec += 1000000000;
    counter->tv_sec  -= 1;
  }
}  

void dump_fips_statistics ( fips_statistics_type *fips_statistics) {
  int i;
  fprintf(stderr,"Number of blocks passing FIPS 140-2 randomness tests: %" PRIu64 "\n", fips_statistics->good_fips_blocks);
  fprintf(stderr,"Number of blocks failing FIPS 140-2 randomness tests: %"  PRIu64 "\n", fips_statistics->bad_fips_blocks);
  for ( i = 0; i < N_FIPS_TESTS; ++i) fprintf(stderr,"Number of blocks failing %s test: \t%"   PRIu64 "\n",
      fips_test_names[i], fips_statistics->fips_failures[i]);
  if ( fips_statistics->track_CPU_time ) {
    fprintf (stderr,"CPU time of FIPS 140-2 randomness tests: %ld.%09ld s\n",
        fips_statistics->cpu_time.tv_sec, fips_statistics->cpu_time.tv_nsec);
  }
}

void fips_init(fips_ctx_t *ctx, unsigned int last32, int track_CPU_time)
{
  if (ctx) {
    fips_statistics_init(&ctx->fips_statistics, track_CPU_time);
    memset (ctx->poker, 0, sizeof (ctx->poker));
    memset (ctx->runs, 0, sizeof (ctx->runs));
    ctx->longrun = 0;
    ctx->ones = 0;
    ctx->rlength = -1;
    ctx->current_bit = 0;
    ctx->last_bit = 0;
    ctx->last32 = last32;
  }
}




