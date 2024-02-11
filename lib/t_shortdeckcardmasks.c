#include "poker_defs.h"

/* 
 * Table ShortDeck_cardMasksTable
 */

/*
ShortDeck_cardMasks[].  Maps card indices (0..35) to CardMasks.  
The output mask has only one bit set, the bit corresponding to the card
identified by the index.
 */

#include "deck_short.h" 
ShortDeck_CardMask ShortDeck_cardMasksTable[36] = { 
      { 0x0010000000000000LL }  ,
      { 0x0020000000000000LL }  ,
      { 0x0040000000000000LL }  ,
      { 0x0080000000000000LL }  ,
      { 0x0100000000000000LL }  ,
      { 0x0200000000000000LL }  ,
      { 0x0400000000000000LL }  ,
      { 0x0800000000000000LL }  ,
      { 0x1000000000000000LL }  ,
      { 0x0000001000000000LL }  ,
      { 0x0000002000000000LL }  ,
      { 0x0000004000000000LL }  ,
      { 0x0000008000000000LL }  ,
      { 0x0000010000000000LL }  ,
      { 0x0000020000000000LL }  ,
      { 0x0000040000000000LL }  ,
      { 0x0000080000000000LL }  ,
      { 0x0000100000000000LL }  ,
      { 0x0000000000100000LL }  ,
      { 0x0000000000200000LL }  ,
      { 0x0000000000400000LL }  ,
      { 0x0000000000800000LL }  ,
      { 0x0000000001000000LL }  ,
      { 0x0000000002000000LL }  ,
      { 0x0000000004000000LL }  ,
      { 0x0000000008000000LL }  ,
      { 0x0000000010000000LL }  ,
      { 0x0000000000000010LL }  ,
      { 0x0000000000000020LL }  ,
      { 0x0000000000000040LL }  ,
      { 0x0000000000000080LL }  ,
      { 0x0000000000000100LL }  ,
      { 0x0000000000000200LL }  ,
      { 0x0000000000000400LL }  ,
      { 0x0000000000000800LL }  ,
      { 0x0000000000001000LL }  
};
