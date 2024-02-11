cdef extern from "poker_defs.h":

    # Compiler specific defines
    #...

    # Integer types
    ctypedef unsigned char uint8
    ctypedef unsigned short uint16 
    ctypedef unsigned int uint32
    ctypedef unsigned long long uint64

    # Card mask
    ctypedef struct StdDeck_CardMask:
        uint64 cards_bits[StdDeck_N_CARDS/64]

    # Card ranks    
    cdef enum StdDeck_Rank:
        StdDeck_Rank_2,
        StdDeck_Rank_3,
        StdDeck_Rank_4,
        StdDeck_Rank_5,
        StdDeck_Rank_6,
        StdDeck_Rank_7,
        StdDeck_Rank_8,
        StdDeck_Rank_9,
        StdDeck_Rank_TEN,
        StdDeck_Rank_JACK,
        StdDeck_Rank_QUEEN,
        StdDeck_Rank_KING,
        StdDeck_Rank_ACE

    # Card suits
    cdef enum StdDeck_Suit:
        StdDeck_Suit_CLUBS,
        StdDeck_Suit_DIAMONDS,
        StdDeck_Suit_HEARTS,
        StdDeck_Suit_SPADES

    # Hand values
    ctypedef struct HandVal:
        uint8 _v
        
    ctypedef struct LowHandVal: 
        uint8 _v

    # Deck functions
    Deck_CardMask_RESET(...)   
    Deck_CardMask_SET(...)
    Deck_CardMask_OR(...)
    
    # Hand eval functions
    HandVal Hand_EVAL_N(...)
    LowHandVal Hand_EVAL_LOW(...)
    
    int parseArgs(int argc, char **argv, 
                  enum_game_t *game, enum_sample_t *enumType, int *niter,
                  StdDeck_CardMask pockets[], StdDeck_CardMask *board,
                  StdDeck_CardMask *dead, int *npockets, int *nboard,
                  int *orderflag, int *terse)    

cdef extern from "enumdefs.h": 
    # declarations


                  
    int enumExhaustive(enum_game_t game, StdDeck_CardMask pockets[],
                       StdDeck_CardMask board, StdDeck_CardMask dead,
                       int npockets, int nboard, int orderflag, 
                       enum_result_t *result)
                       
    int enumSample(enum_game_t game, StdDeck_CardMask pockets[],
                   StdDeck_CardMask board, StdDeck_CardMask dead,
                   int npockets, int nboard, int niter, int orderflag,
                   enum_result_t *result)
                   
def py_parseArgs(list args):
    cdef:
        enum_game_t game
        enum_sample_t enumType
        int niter, npockets, nboard
        int orderflag, terse
        StdDeck_CardMask pockets[ENUM_MAXPLAYERS]
        StdDeck_CardMask board
        StdDeck_CardMask dead
        
    cdef int rv = parseArgs(len(args), &args[0], 
                            &game, &enumType, &niter,
                            pockets, &board, &dead, &npockets, &nboard,
                            &orderflag, &terse)
    cdef dict parsed_args    

    if rv == 0:

        # Parse args dict to return
        parsed_args = {
            'game': game,
            'enumType': enumType,
            'niter': niter,
            'pockets': pockets,
            'board': board, 
            'dead': dead,
            'npockets': npockets,
            'nboard': nboard,
            'orderflag': orderflag,
            'terse': terse
        }
        
        return parsed_args
    else:
        return error
        
def py_enumExhaustive(dict params):
    cdef int rv = enumExhaustive(params['game'], params['pockets'],
                                 params['board'], params['dead'],
                                 params['npockets'], params['nboard'],
                                 params['orderflag'], params['result'])
                                 
    if rv == 0:
       return params['result']
    else:
       return error
       
def py_enumSample(dict params):
    # Call C enumSample function
    cdef int rv = enumSample(params['game'], params['pockets'], 
                             params['board'], params['dead'],
                             params['npockets'], params['nboard'],
                             params['niter'], params['orderflag'],
                             params['result'])
                             
    if rv == 0:
        return params['result']
    else:
        return error