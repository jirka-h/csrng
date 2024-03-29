/* vim: set expandtab cindent fdm=marker ts=2 sw=2: */

/* {{{ Copyright notice

Simple entropy harvester based upon the havege RNG

Copyright (C) 2011-2013 Jirka Hladky <hladky DOT jiri AT gmail DOT com>
Copyright (C) 2009 Gary Wuertz gary@issiweb.com
Copyright (C) 2006 - André Seznec - Olivier Rochecouste

This file is part of CSRNG http://code.google.com/p/csrng/

CSRNG is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

CSRNG is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with CSRNG.  If not, see <http://www.gnu.org/licenses/>.
}}} */

/* ------------------------------------------------------------------------
 * On average, one iteration accesses two 8-word blocks in the havege_walk
 * table, and generates 16 words in the RESULT array.
 *
 * The data read in the Walk table are updated and permuted after each use.
 * The result of the hardware clock counter read is used for this update.
 *
 * 21 conditional tests are present. The conditional tests are grouped in
 * two nested  groups of 10 conditional tests and 1 test that controls the
 * permutation.
 *
 * In average, there should be 4 tests executed and, in average, 2 of them
 * should be mispredicted.
 * ------------------------------------------------------------------------
 */

  PTtest = PT >> 20;

  if (PTtest & 1) {
    PTtest ^= 3; PTtest = PTtest >> 1;
    if (PTtest & 1) {
      PTtest ^= 3; PTtest = PTtest >> 1;
      if (PTtest & 1) {
        PTtest ^= 3; PTtest = PTtest >> 1;
        if (PTtest & 1) {
          PTtest ^= 3; PTtest = PTtest >> 1;
          if (PTtest & 1) {
            PTtest ^= 3; PTtest = PTtest >> 1;
            if (PTtest & 1) {
              PTtest ^= 3; PTtest = PTtest >> 1;
              if (PTtest & 1) {
                PTtest ^= 3; PTtest = PTtest >> 1;
                if (PTtest & 1) {
                  PTtest ^= 3; PTtest = PTtest >> 1;
                  if (PTtest & 1) {
                    PTtest ^= 3; PTtest = PTtest >> 1;
                    if (PTtest & 1) {
                      PTtest ^= 3; PTtest = PTtest >> 1;
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  };

  PTtest = PTtest >> 1;
  pt = (PT >> 18) & 7;

  PT = PT & ANDPT;

  HARDCLOCK(havege_hardtick);

  Pt0 = &havege_pwalk[PT];
  Pt1 = &havege_pwalk[PT2];
  Pt2 = &havege_pwalk[PT  ^ 1];
  Pt3 = &havege_pwalk[PT2 ^ 4];

  RESULT[i++] ^= *Pt0;
  RESULT[i++] ^= *Pt1;
  RESULT[i++] ^= *Pt2;
  RESULT[i++] ^= *Pt3;

  inter = ROR32(*Pt0,1) ^ havege_hardtick;
  *Pt0  = ROR32(*Pt1,2) ^ havege_hardtick;
  *Pt1  = inter;
  *Pt2  = ROR32(*Pt2, 3) ^ havege_hardtick;
  *Pt3  = ROR32(*Pt3, 4) ^ havege_hardtick;

  Pt0 = &havege_pwalk[PT  ^ 2];
  Pt1 = &havege_pwalk[PT2 ^ 2];
  Pt2 = &havege_pwalk[PT  ^ 3];
  Pt3 = &havege_pwalk[PT2 ^ 6];

  RESULT[i++] ^= *Pt0;
  RESULT[i++] ^= *Pt1;
  RESULT[i++] ^= *Pt2;
  RESULT[i++] ^= *Pt3;

  if (PTtest & 1) {
    volatile DATA_TYPE *Ptinter;
    Ptinter = Pt0;
    Pt2 = Pt0;
    Pt0 = Ptinter;
  }

  PTtest = (PT2 >> 18);
  inter  = ROR32(*Pt0, 5) ^ havege_hardtick;
  *Pt0   = ROR32(*Pt1, 6) ^ havege_hardtick;
  *Pt1   = inter;

  HARDCLOCK(havege_hardtick);

  *Pt2 = ROR32(*Pt2, 7) ^ havege_hardtick;
  *Pt3 = ROR32(*Pt3, 8) ^ havege_hardtick;

  Pt0 = &havege_pwalk[PT  ^ 4];
  Pt1 = &havege_pwalk[PT2 ^ 1];

  PT2 = (RESULT[(i - 8) ^ pt2] ^ havege_pwalk[PT2 ^ pt2 ^ 7]);
  PT2 = ((PT2 & ANDPT) & (0xfffffff7)) ^ ((PT ^ 8) & 0x8);

  /* avoid PT and PT2 to point on the same cache block */
  pt2 = ((PT2 >> 28) & 7);

  if (PTtest & 1) {
    PTtest ^= 3; PTtest = PTtest >> 1;
    if (PTtest & 1) {
      PTtest ^= 3; PTtest = PTtest >> 1;
      if (PTtest & 1) {
        PTtest ^= 3; PTtest = PTtest >> 1;
        if (PTtest & 1) {
          PTtest ^= 3; PTtest = PTtest >> 1;
          if (PTtest & 1) {
            PTtest ^= 3; PTtest = PTtest >> 1;
            if (PTtest & 1) {
              PTtest ^= 3; PTtest = PTtest >> 1;
              if (PTtest & 1) {
                PTtest ^= 3; PTtest = PTtest >> 1;
                if (PTtest & 1) {
                  PTtest ^= 3; PTtest = PTtest >> 1;
                  if (PTtest & 1) {
                    PTtest ^= 3; PTtest = PTtest >> 1;
                    if (PTtest & 1) {
                      PTtest ^= 3; PTtest = PTtest >> 1;
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  };

  Pt2 = &havege_pwalk[PT  ^ 5];
  Pt3 = &havege_pwalk[PT2 ^ 5];

  RESULT[i++] ^= *Pt0;
  RESULT[i++] ^= *Pt1;
  RESULT[i++] ^= *Pt2;
  RESULT[i++] ^= *Pt3;

  inter = ROR32(*Pt0 , 9) ^ havege_hardtick;
  *Pt0  = ROR32(*Pt1 , 10) ^ havege_hardtick;
  *Pt1  = inter;
  *Pt2  = ROR32(*Pt2, 11) ^ havege_hardtick;
  *Pt3  = ROR32(*Pt3, 12) ^ havege_hardtick;

  Pt0 = &havege_pwalk[PT  ^ 6];
  Pt1 = &havege_pwalk[PT2 ^ 3];
  Pt2 = &havege_pwalk[PT  ^ 7];
  Pt3 = &havege_pwalk[PT2 ^ 7];

  RESULT[i++] ^= *Pt0;
  RESULT[i++] ^= *Pt1;
  RESULT[i++] ^= *Pt2;
  RESULT[i++] ^= *Pt3;

  inter = ROR32(*Pt0, 13) ^ havege_hardtick;
  *Pt0  = ROR32(*Pt1, 14) ^ havege_hardtick;
  *Pt1  = inter;
  *Pt2  = ROR32(*Pt2, 15) ^ havege_hardtick;
  *Pt3  = ROR32(*Pt3, 16) ^ havege_hardtick;

  /* avoid PT and PT2 to point on the same cache block */
  PT = (((RESULT[(i - 8) ^ pt] ^ havege_pwalk[PT ^ pt ^ 7])) &
        (0xffffffef)) ^ ((PT2 ^ 0x10) & 0x10);
