/* rand.h - the generator of Candytron's GenThree (sGetRnd of _types.cpp) - header-only C99 (stb style)

   A linear congruential generator, 32 bit state, two steps per number. The original keeps the state in a global; here it is an object the caller owns, so that the
   order in which the script's natives consume it (bitmaps, meshes) is the caller's to keep. Everything is static inline: nothing to define.
*/
#ifndef GT3_RAND_H_INCLUDED
#define GT3_RAND_H_INCLUDED

#include <stdint.h>

typedef struct gt3_rng {
  uint32_t state;
} gt3_rng;

/* sRandomSeed starts as 0x74382381 */
static inline void gt3_rng_init(gt3_rng *r) { r->state = 0x74382381u; }

/* sGetRnd(): two steps of s = s * 0x343fd + 0x269ec3; the result takes 16 bits of the second state (>> 10) and 16 bits of the first (<< 6) */
static inline uint32_t gt3_rng_next(gt3_rng *r)
{
  const uint32_t s1 = r->state * 0x343fdu + 0x269ec3u;
  const uint32_t s2 = s1 * 0x343fdu + 0x269ec3u;
  r->state = s2;
  return (uint32_t)(((int32_t)s2 >> 10) & 0xffff) | ((s1 << 6) & 0xffff0000u);
}

/* sGetRnd(max) */
static inline uint32_t gt3_rng_below(gt3_rng *r, uint32_t max) { return gt3_rng_next(r) % max; }

/* sSetRndSeed(seed): int arithmetic as in the original (the products wrap) */
static inline void gt3_rng_seed(gt3_rng *r, int32_t seed)
{
  const uint32_t s = (uint32_t)seed;
  const uint32_t mix = s + s * 17u + s * 121u + (uint32_t)((int32_t)(s * 121u) / 17);
  r->state = mix;
  gt3_rng_next(r);
  r->state ^= mix;
  gt3_rng_next(r);
  r->state ^= mix;
  gt3_rng_next(r);
  r->state ^= mix;
  gt3_rng_next(r);
}

#endif /* GT3_RAND_H_INCLUDED */
