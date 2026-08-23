/* BR-02: compact Leduc-style public-card parity gate. */
#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/solver/pe_best_response.h>
#include <poker_eval/solver/pe_traversal.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAILED: "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

/* Cards 0..5 are two copies of ranks 0..2.  The game deals private cards,
 * runs one check/fold action per player, deals a public card, and repeats. */
typedef struct { cfr_game_t cfr; cfr_storage_t *storage; pe_vector_game_t vector; } ld_adapter_t;

static uint64_t ld_key(int p0, int p1, int pub, int phase, int turn, int folded)
{ return (uint64_t)p0 | ((uint64_t)p1 << 3) | ((uint64_t)pub << 6) |
         ((uint64_t)phase << 9) | ((uint64_t)turn << 11) | ((uint64_t)folded << 12); }
static void ld_unpack(uint64_t k, int *p0, int *p1, int *pub, int *phase, int *turn, int *folded)
{ *p0=k&7; *p1=(k>>3)&7; *pub=(k>>6)&7; *phase=(k>>9)&3; *turn=(k>>11)&1; *folded=(k>>12)&3; }

static int ld_current(cfr_game_t *g, uint64_t k, void *u)
{ int a,b,c,phase,turn,f; (void)g;(void)u;ld_unpack(k,&a,&b,&c,&phase,&turn,&f); return (phase==0||phase==2)?turn:-1; }
static int ld_terminal(cfr_game_t *g, uint64_t k, void *u)
{ int a,b,c,phase,turn,f;(void)g;(void)u;if(!k)return 0;ld_unpack(k,&a,&b,&c,&phase,&turn,&f);return phase==3; }
static int ld_actions(cfr_game_t *g, uint64_t k, int *out, int max, void *u)
{ int a,b,c,phase,turn,f;(void)g;(void)u;ld_unpack(k,&a,&b,&c,&phase,&turn,&f);if((phase!=0&&phase!=2)||max<2)return 0;out[0]=0;out[1]=1;return 2; }
static uint64_t ld_apply(cfr_game_t *g, uint64_t k, int action, void *u)
{ int a,b,c,phase,turn,f;(void)g;(void)u;if(action<0||action>1)return 0;ld_unpack(k,&a,&b,&c,&phase,&turn,&f);if(phase!=0&&phase!=2)return 0;if(action==1)return ld_key(a,b,c,3,turn,f|(1<<turn));if(phase==0&&turn==1)return ld_key(a,b,c,1,0,0);if(phase==2&&turn==1)return ld_key(a,b,c,3,0,0);return ld_key(a,b,c,phase,1,0); }
static uint64_t ld_infoset(const void *state)
{ uint64_t k=(uint64_t)(uintptr_t)state;int a,b,p,phase,turn,f,own,board;(void)f;if(!k)return 0;ld_unpack(k,&a,&b,&p,&phase,&turn,&f);if(phase==1)return 0;if(phase==3)return (UINT64_C(1)<<60)|k;own=(turn==0?a:b)/2;board=(phase==2)?p/2:3;return (UINT64_C(1)<<60)|own|((uint64_t)board<<3)|((uint64_t)phase<<6)|((uint64_t)turn<<8); }

static int ld_chance(cfr_game_t *g,uint64_t k,void*u){(void)g;(void)u;return k==0||((k>>9)&3)==1;}
static int ld_chance_count(cfr_game_t*g,uint64_t k,void*u){(void)g;(void)u;return k==0?30:4;}
static uint64_t ld_chance_apply(cfr_game_t*g,uint64_t k,int o,void*u)
{ int a,b,p,phase,t,f,i,j,n,deck[6],x;(void)g;(void)u;if(k==0){if(o<0||o>=30)return 0;for(i=0;i<6;i++)deck[i]=i;i=o/5;a=deck[i];deck[i]=deck[5];j=o%5;b=deck[j];return ld_key(a,b,6,0,0,0);}ld_unpack(k,&a,&b,&p,&phase,&t,&f);if(phase!=1||o<0||o>=4)return 0;n=0;for(i=0;i<6;i++)if(i!=a&&i!=b)deck[n++]=i;x=deck[o];return ld_key(a,b,x,2,0,0); }
static double ld_utility(cfr_game_t*g,uint64_t k,int player,void*u)
{ int a,b,p,phase,t,f;int winner=-1;double v;(void)g;(void)u;ld_unpack(k,&a,&b,&p,&phase,&t,&f);if(f){winner=(f&1)?1:0;return player==winner?1.0:-1.0;}a/=2;b/=2;p/=2;if(a==p&&b!=p)winner=0;else if(b==p&&a!=p)winner=1;else if(a>b)winner=0;else if(b>a)winner=1;else return 0.0;v=player==winner?1.0:-1.0;return v; }

static uint64_t ld_state(const ld_adapter_t*a,const void*s){return s==a?0:(uint64_t)(uintptr_t)s;}
static int ld_v_terminal(const void*s,void*u){ld_adapter_t*a=u;return ld_terminal(&a->cfr,ld_state(a,s),u);}
static int ld_v_player(const void*s,void*u){ld_adapter_t*a=u;return ld_current(&a->cfr,ld_state(a,s),u);}
static uint16_t ld_v_actions(const void*s,void*u){ld_adapter_t*a=u;int x[2];return (uint16_t)ld_actions(&a->cfr,ld_state(a,s),x,2,u);}
static uint64_t ld_v_infoset(const void*s,void*u){ld_adapter_t*a=u;return ld_infoset((const void*)(uintptr_t)ld_state(a,s));}
static int ld_v_strategy(const void*s,uint64_t info,uint16_t action,pe_value_vec_t*out,void*u){ld_adapter_t*a=u;double x[2];(void)s;if(!out||out->n!=1||action>1)return -1;cfr_storage_get_avg_strategy(a->storage,info,2,x);out->v[0]=x[action];return 0;}
static const void *ld_v_apply(const void*s,uint16_t action,void*u){ld_adapter_t*a=u;uint64_t x=ld_apply(&a->cfr,ld_state(a,s),action,u);return x?(const void*)(uintptr_t)x:NULL;}
static int ld_v_values(const void*s,const pe_reach_vec_t*r,pe_value_vec_t*out,uint8_t n,void*u){ld_adapter_t*a=u;uint64_t k=ld_state(a,s);(void)r;if(!out||n!=2)return -1;out[0].v[0]=ld_utility(&a->cfr,k,0,u);out[1].v[0]=ld_utility(&a->cfr,k,1,u);return 0;}
static int ld_v_chance(const void*s,void*u){ld_adapter_t*a=u;return ld_chance(&a->cfr,ld_state(a,s),u);}
static uint16_t ld_v_chance_count(const void*s,void*u){ld_adapter_t*a=u;return (uint16_t)ld_chance_count(&a->cfr,ld_state(a,s),u);}
static const void *ld_v_chance_apply(const void*s,int o,void*u){ld_adapter_t*a=u;uint64_t x=ld_chance_apply(&a->cfr,ld_state(a,s),o,u);return x?(const void*)(uintptr_t)x:NULL;}

static void ld_init(ld_adapter_t*a)
{ memset(a,0,sizeof(*a));a->cfr.current_player=ld_current;a->cfr.is_terminal=ld_terminal;a->cfr.get_actions=ld_actions;a->cfr.apply_action=ld_apply;a->cfr.get_infoset_key=ld_infoset;a->cfr.get_utility=ld_utility;a->cfr.is_chance=ld_chance;a->cfr.get_chance_outcomes=ld_chance_count;a->cfr.apply_chance=ld_chance_apply;a->cfr.initial_state=(void*)0;a->cfr.state_size=sizeof(uint64_t);a->cfr.num_players=2;a->storage=cfr_storage_create();a->vector.root=a;a->vector.user=a;a->vector.player_count=2;a->vector.combo_count=1;a->vector.is_chance=ld_v_chance;a->vector.chance_outcome_count=ld_v_chance_count;a->vector.apply_chance=ld_v_chance_apply;a->vector.is_terminal=ld_v_terminal;a->vector.acting_player=ld_v_player;a->vector.action_count=ld_v_actions;a->vector.infoset_key=ld_v_infoset;a->vector.strategy=ld_v_strategy;a->vector.apply_action=ld_v_apply;a->vector.terminal_values=ld_v_values; }

int main(void)
{ ld_adapter_t a;pe_best_response_vector_config_t cfg;pe_best_response_vector_result_t r;double scalar;ld_init(&a);CHECK(a.storage,"Leduc storage allocation");if(!a.storage)return 1;cfg=pe_best_response_vector_config_default();CHECK(pe_best_response_vector(&a.vector,0,&cfg,&r)==PE_SOLVER_OK,"Leduc vector BR failed");scalar=cfr_best_response_value_infoset(&a.cfr,a.storage,0,NULL);CHECK(r.converged,"Leduc vector BR did not converge");CHECK(r.infosets>0,"Leduc BR found no infosets");CHECK(fabs(r.value-scalar)<=1e-9,"Leduc vector/scalar mismatch %.17g vs %.17g",r.value,scalar);cfr_storage_destroy(a.storage);if(failures)return 1;puts("test_best_response_ii_leduc: parity passed");return 0; }
