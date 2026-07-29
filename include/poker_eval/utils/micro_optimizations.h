/* micro_optimizations.h -- Micro-optimizations for poker-eval
 *
 * This file contains:
 * - Pre-calculated reciprocals for division optimization
 * - Branch prediction hints
 * - Alignment directives for tables
 *
 * Copyright (C) 2024
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program in a file in the toplevel directory called "GPLv3".
 * If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MICRO_OPTIMIZATIONS_H__
#define __MICRO_OPTIMIZATIONS_H__

#include <poker_eval/core/enumdefs.h>

/* Branch prediction hints */
#ifdef __GNUC__
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#else
#define likely(x)       (x)
#define unlikely(x)     (x)
#endif

/* Alignment directive for 64-byte cache line */
#ifdef __GNUC__
#define CACHE_ALIGN __attribute__((aligned(64)))
#else
#define CACHE_ALIGN
#endif

/* Pre-calculated reciprocals for common hishare values
 * These are used to avoid division in hot paths
 * Values are for 1/n where n is from 1 to ENUM_MAXPLAYERS
 */
static const double hishare_reciprocals[ENUM_MAXPLAYERS + 1] = {
    0.0,        /* 0 - unused */
    1.0,        /* 1/1 = 1.0 */
    0.5,        /* 1/2 = 0.5 */
    0.33333333333333333,  /* 1/3 */
    0.25,       /* 1/4 = 0.25 */
    0.2,        /* 1/5 = 0.2 */
    0.16666666666666666,  /* 1/6 */
    0.14285714285714285,  /* 1/7 */
    0.125,      /* 1/8 = 0.125 */
    0.11111111111111111,  /* 1/9 */
    0.1,        /* 1/10 = 0.1 */
    0.09090909090909091,  /* 1/11 */
    0.08333333333333333   /* 1/12 */
};

/* Pre-calculated half reciprocals for hi/lo split pots
 * These are used for 0.5/n calculations
 */
static const double hishare_half_reciprocals[ENUM_MAXPLAYERS + 1] = {
    0.0,        /* 0 - unused */
    0.5,        /* 0.5/1 = 0.5 */
    0.25,       /* 0.5/2 = 0.25 */
    0.16666666666666666,  /* 0.5/3 */
    0.125,      /* 0.5/4 = 0.125 */
    0.1,        /* 0.5/5 = 0.1 */
    0.08333333333333333,  /* 0.5/6 */
    0.07142857142857142,  /* 0.5/7 */
    0.0625,     /* 0.5/8 = 0.0625 */
    0.05555555555555555,  /* 0.5/9 */
    0.05,       /* 0.5/10 = 0.05 */
    0.04545454545454545,  /* 0.5/11 */
    0.04166666666666666   /* 0.5/12 */
};

/* Inline functions to get reciprocals with bounds checking */
static inline double get_hishare_reciprocal(int hishare) {
    if (likely(hishare > 0 && hishare <= ENUM_MAXPLAYERS)) {
        return hishare_reciprocals[hishare];
    }
    return 1.0 / hishare;  /* Fallback for out-of-bounds values */
}

static inline double get_hishare_half_reciprocal(int hishare) {
    if (likely(hishare > 0 && hishare <= ENUM_MAXPLAYERS)) {
        return hishare_half_reciprocals[hishare];
    }
    return 0.5 / hishare;  /* Fallback for out-of-bounds values */
}

#endif /* __MICRO_OPTIMIZATIONS_H__ */
