/*
 * test_extended_analytical_games.c - remaining ISSUE-09/#165 benchmarks.
 *
 * The games are deliberately small and dependency-free.  Each one is exposed
 * through the real cfr_game_t interface and is checked against an oracle that
 * walks the tree independently of CFR:
 *   - equal-prior jam-or-fold;
 *   - a finite quadrature of the continuous [0,1] game u(x,y)=x-y;
 *   - static and non-static two-street matrix games.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include "analytical_oracles.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", (m), __FILE__, __LINE__); return 1; } } while (0)
#define CLOSE(a,b,e,m) do { if (fabs((a)-(b)) > (e)) { fprintf(stderr, "FAIL: %s (got %.12g want %.12g)\n", (m), (double)(a), (double)(b)); return 1; } } while (0)

/* ---------------------------- Jam-or-Fold ------------------------- */
#define JF_ROOT 0
#define JF_P2   1
#define JF_TERM 2
static uint64_t jf_key(int hand, int phase, int action)
{ return (1ULL << 10) | (uint64_t)(hand & 3) | ((uint64_t)(phase & 3) << 2) | ((uint64_t)(action & 3) << 5); }
static void jf_dec(uint64_t k, int *hand, int *phase, int *action)
{ *hand = (int)(k & 3); *phase = (int)((k >> 2) & 3); *action = (int)((k >> 5) & 3); }
static int jf_current(cfr_game_t *g, uint64_t k, void *u)
{ int h,p,a; (void)g; (void)u; jf_dec(k,&h,&p,&a); return p == JF_ROOT ? 0 : (p == JF_P2 ? 1 : -1); }
static int jf_terminal(cfr_game_t *g, uint64_t k, void *u)
{ int h,p,a; (void)g; (void)u; jf_dec(k,&h,&p,&a); return p == JF_TERM; }
static double jf_get_utility(cfr_game_t *g, uint64_t k, int player, void *u)
{
    int h,p,a; (void)g; (void)u; jf_dec(k,&h,&p,&a);
    if (p != JF_TERM) return 0.0;
    double v = a == 2 ? -1.0 : (a == 0 ? 1.0 : (h ? 2.0 : -2.0));
    return player == 0 ? v : -v;
}
static int jf_actions(cfr_game_t *g, uint64_t k, int *out, int maxn, void *u)
{ int h,p,a; (void)g; (void)u; jf_dec(k,&h,&p,&a); if (p == JF_TERM || maxn < 2) return 0; out[0]=0; out[1]=1; return 2; }
static uint64_t jf_apply(cfr_game_t *g, uint64_t k, int a, void *u)
{ int h,p,last; (void)g; (void)u; jf_dec(k,&h,&p,&last); return p == JF_ROOT ? (a == 0 ? jf_key(h,JF_TERM,2) : jf_key(h,JF_P2,a)) : jf_key(h,JF_TERM,a); }
static uint64_t jf_infoset(const void *state)
{
    uint64_t k=(uint64_t)(uintptr_t)state; int h,p,a; jf_dec(k,&h,&p,&a);
    if (p == JF_TERM) return (1ULL<<60)|k;
    return (1ULL<<59) | (uint64_t)(p == JF_ROOT ? h : 3);
}
static int jf_chance(cfr_game_t *g,uint64_t k,void *u) { (void)g;(void)u;return k==0; }
static int jf_nchance(cfr_game_t *g,uint64_t k,void *u) { (void)g;(void)k;(void)u;return 2; }
static uint64_t jf_apply_chance(cfr_game_t *g,uint64_t k,int o,void *u) { (void)g;(void)u;return k==0?jf_key(o,JF_ROOT,0):k; }
static double jf_brute(cfr_game_t *g,cfr_storage_t *st,uint64_t k)
{
    if (g->is_chance(g,k,NULL)) return 0.5*(jf_brute(g,st,jf_apply_chance(g,k,0,NULL))+jf_brute(g,st,jf_apply_chance(g,k,1,NULL)));
    if (g->is_terminal(g,k,NULL)) return g->get_utility(g,k,0,NULL);
    int acts[2],n=g->get_actions(g,k,acts,2,NULL); double s[2];
    cfr_storage_get_avg_strategy(st,jf_infoset((const void *)(uintptr_t)k),n,s);
    return s[0]*jf_brute(g,st,g->apply_action(g,k,acts[0],NULL))+s[1]*jf_brute(g,st,g->apply_action(g,k,acts[1],NULL));
}

static int run_jam_or_fold(void)
{
    cfr_game_t g; memset(&g,0,sizeof(g)); g.current_player=jf_current; g.is_terminal=jf_terminal;
    g.get_utility=jf_get_utility; g.get_actions=jf_actions; g.apply_action=jf_apply; g.get_infoset_key=jf_infoset;
    g.is_chance=jf_chance; g.get_chance_outcomes=jf_nchance; g.apply_chance=jf_apply_chance; g.num_players=2;
    cfr_config_t cfg; memset(&cfg,0,sizeof(cfg)); cfg.max_iterations=100000; cfg.enable_dcfr=1; cfg.dcfr_alpha=1.5; cfg.dcfr_gamma=2.0;
    cfr_storage_t *st=cfr_storage_create(); CHECK(st,"jam storage"); double proxy=0; cfr_solve(&g,st,&cfg,&proxy);
    double weak[2], strong[2], caller[2]; cfr_storage_get_avg_strategy(st,jf_infoset((const void *)(uintptr_t)jf_key(0,JF_ROOT,0)),2,weak);
    cfr_storage_get_avg_strategy(st,jf_infoset((const void *)(uintptr_t)jf_key(1,JF_ROOT,0)),2,strong);
    cfr_storage_get_avg_strategy(st,jf_infoset((const void *)(uintptr_t)jf_key(0,JF_P2,0)),2,caller);
    jam_or_fold_equilibrium_t eq; get_jam_or_fold_analytical_solution(&eq);
    double ev=jf_brute(&g,st,0);
    printf("    Jam-or-Fold weak jam=%.4f caller call=%.4f EV=%.5f\n",weak[1],caller[1],ev);
    CHECK(strong[1] > 0.98,"strong hand jams"); CLOSE(weak[1],eq.weak_jam_freq,0.04,"weak jam frequency");
    CLOSE(caller[1],eq.caller_call_freq,0.05,"caller frequency"); CLOSE(ev,eq.p1_ev,0.03,"jam-or-fold value");
    cfr_storage_destroy(st); return 0;
}

/* -------------------------- Continuous [0,1] ----------------------- */
#define CT_ROOT 0
#define CT_P2 1
#define CT_TERM 2
static uint64_t ct_key(int phase,int x,int y) { return (uint64_t)(phase&3)|((uint64_t)(x&15)<<4)|((uint64_t)(y&15)<<8); }
static void ct_dec(uint64_t k,int *p,int *x,int *y) { *p=(int)(k&3);*x=(int)((k>>4)&15);*y=(int)((k>>8)&15); }
static int ct_current(cfr_game_t*g,uint64_t k,void*u){int p,x,y;(void)g;(void)u;ct_dec(k,&p,&x,&y);return p==CT_ROOT?0:(p==CT_P2?1:-1);}
static int ct_terminal(cfr_game_t*g,uint64_t k,void*u){int p,x,y;(void)g;(void)u;ct_dec(k,&p,&x,&y);return p==CT_TERM;}
static double ct_util(cfr_game_t*g,uint64_t k,int player,void*u){int p,x,y;(void)g;(void)u;ct_dec(k,&p,&x,&y);if(p!=CT_TERM)return 0;double v=((double)x-(double)y)/10.0;return player==0?v:-v;}
static int ct_actions(cfr_game_t*g,uint64_t k,int*out,int maxn,void*u){int p,x,y;(void)g;(void)u;ct_dec(k,&p,&x,&y);if(p==CT_TERM||maxn<11)return 0;for(int i=0;i<11;i++)out[i]=i;return 11;}
static uint64_t ct_apply(cfr_game_t*g,uint64_t k,int a,void*u){int p,x,y;(void)g;(void)u;ct_dec(k,&p,&x,&y);return p==CT_ROOT?ct_key(CT_P2,a,0):ct_key(CT_TERM,x,a);}
static uint64_t ct_infoset(const void*state){uint64_t k=(uint64_t)(uintptr_t)state;int p,x,y;ct_dec(k,&p,&x,&y);return (1ULL<<57)|(uint64_t)(p==CT_ROOT?0:1);}
static double ct_brute(cfr_game_t*g,cfr_storage_t*st,uint64_t k){int p,x,y;ct_dec(k,&p,&x,&y);if(p==CT_TERM)return ct_util(g,k,0,NULL);int a[11],n=ct_actions(g,k,a,11,NULL);double s[11];cfr_storage_get_avg_strategy(st,ct_infoset((const void*)(uintptr_t)k),n,s);double v=0;for(int i=0;i<n;i++)v+=s[i]*ct_brute(g,st,ct_apply(g,k,a[i],NULL));return v;}

static int run_continuous(void)
{
    cfr_game_t g;memset(&g,0,sizeof(g));g.current_player=ct_current;g.is_terminal=ct_terminal;g.get_utility=ct_util;g.get_actions=ct_actions;g.apply_action=ct_apply;g.get_infoset_key=ct_infoset;g.initial_state=(void*)(uintptr_t)ct_key(CT_ROOT,0,0);g.num_players=2;
    cfr_config_t cfg;memset(&cfg,0,sizeof(cfg));cfg.max_iterations=30000;cfg.enable_dcfr=1;cfg.dcfr_alpha=1.5;cfg.dcfr_gamma=2.0;cfr_storage_t*st=cfr_storage_create();CHECK(st,"continuous storage");double proxy=0;cfr_solve(&g,st,&cfg,&proxy);
    double p1[11],p2[11];cfr_storage_get_avg_strategy(st,ct_infoset((const void*)(uintptr_t)ct_key(CT_ROOT,0,0)),11,p1);cfr_storage_get_avg_strategy(st,ct_infoset((const void*)(uintptr_t)ct_key(CT_P2,0,0)),11,p2);double ev=ct_brute(&g,st,(uint64_t)(uintptr_t)g.initial_state);continuous_equilibrium_t eq;get_continuous_analytical_solution(&eq);
    printf("    Continuous [0,1] grid P1 endpoint=%.4f P2 endpoint=%.4f EV=%.6f\n",p1[10],p2[10],ev);
    CHECK(p1[10]>0.9&&p2[10]>0.9,"continuous endpoint equilibrium");CLOSE(ev,eq.value,0.03,"continuous value");cfr_storage_destroy(st);return 0;
}

/* --------------------- Static/non-static multi-street -------------- */
#define MS_TERM 4
#define MS_NONSTATIC (1ULL<<55)
static uint64_t ms_key(int mode,int phase,int a1,int b1,int a2,int b2){return (mode?MS_NONSTATIC:0)|(uint64_t)(phase&7)|((uint64_t)(a1&1)<<3)|((uint64_t)(b1&1)<<4)|((uint64_t)(a2&1)<<5)|((uint64_t)(b2&1)<<6);}
static void ms_dec(uint64_t k,int*m,int*p,int*a1,int*b1,int*a2,int*b2){*m=(k&MS_NONSTATIC)!=0;*p=(int)(k&7);*a1=(int)((k>>3)&1);*b1=(int)((k>>4)&1);*a2=(int)((k>>5)&1);*b2=(int)((k>>6)&1);}
static int ms_current(cfr_game_t*g,uint64_t k,void*u){int m,p,a,b,c,d;(void)g;(void)u;ms_dec(k,&m,&p,&a,&b,&c,&d);return p==0||p==2?0:(p==1||p==3?1:-1);}
static int ms_terminal(cfr_game_t*g,uint64_t k,void*u){int m,p,a,b,c,d;(void)g;(void)u;ms_dec(k,&m,&p,&a,&b,&c,&d);return p==MS_TERM;}
static double ms_util(cfr_game_t*g,uint64_t k,int player,void*u){int m,p,a,b,c,d;(void)g;(void)u;ms_dec(k,&m,&p,&a,&b,&c,&d);if(p!=MS_TERM)return 0;static const double A[2][2]={{2,-1},{-1,1}};double v=A[a][b]+A[c][d];return player==0?v:-v;}
static int ms_actions(cfr_game_t*g,uint64_t k,int*out,int maxn,void*u){int m,p,a,b,c,d;(void)g;(void)u;ms_dec(k,&m,&p,&a,&b,&c,&d);if(p==MS_TERM||maxn<2)return 0;out[0]=0;out[1]=1;return 2;}
static uint64_t ms_apply(cfr_game_t*g,uint64_t k,int act,void*u){int m,p,a,b,c,d;(void)g;(void)u;ms_dec(k,&m,&p,&a,&b,&c,&d);if(p==0)a=act;else if(p==1)b=act;else if(p==2)c=act;else if(p==3)d=act;return ms_key(m,p+1,a,b,c,d);}
static uint64_t ms_infoset(const void*state){uint64_t k=(uint64_t)(uintptr_t)state;int m,p,a,b,c,d;ms_dec(k,&m,&p,&a,&b,&c,&d);if(p==MS_TERM)return (1ULL<<60)|k;uint64_t key=(uint64_t)p;if(m&&p==2)key|=(uint64_t)a<<8;if(m&&p==3)key|=(uint64_t)b<<8;return (1ULL<<56)|key;}
static double ms_brute(cfr_game_t*g,cfr_storage_t*st,uint64_t k){int m,p,a,b,c,d;ms_dec(k,&m,&p,&a,&b,&c,&d);if(p==MS_TERM)return ms_util(g,k,0,NULL);int acts[2];ms_actions(g,k,acts,2,NULL);double s[2];cfr_storage_get_avg_strategy(st,ms_infoset((const void*)(uintptr_t)k),2,s);return s[0]*ms_brute(g,st,ms_apply(g,k,0,NULL))+s[1]*ms_brute(g,st,ms_apply(g,k,1,NULL));}

static int run_multistreet(int mode)
{
    cfr_game_t g;memset(&g,0,sizeof(g));g.current_player=ms_current;g.is_terminal=ms_terminal;g.get_utility=ms_util;g.get_actions=ms_actions;g.apply_action=ms_apply;g.get_infoset_key=ms_infoset;g.initial_state=(void*)(uintptr_t)ms_key(mode,0,0,0,0,0);g.num_players=2;
    cfr_config_t cfg;memset(&cfg,0,sizeof(cfg));cfg.max_iterations=50000;cfg.enable_dcfr=1;cfg.dcfr_alpha=1.5;cfg.dcfr_gamma=2.0;cfr_storage_t*st=cfr_storage_create();CHECK(st,"multi-street storage");double proxy=0;cfr_solve(&g,st,&cfg,&proxy);double ev=ms_brute(&g,st,(uint64_t)(uintptr_t)g.initial_state);double expected=get_two_street_analytical_value();printf("    %s two-street EV=%.6f infosets=%zu\n",mode?"Non-static":"Static",ev,cfr_storage_count_infosets(st));CLOSE(ev,expected,0.04,"two-street matrix value");CHECK(cfr_storage_count_infosets(st)>=(mode?6:4),"multi-street tree visited");cfr_storage_destroy(st);return 0;
}

int main(void)
{
    printf("Running remaining analytical benchmarks (ISSUE-09 #165)...\n");
    int failures=0; failures+=run_jam_or_fold(); failures+=run_continuous(); failures+=run_multistreet(0); failures+=run_multistreet(1);
    if(!failures){printf("All extended analytical tests PASSED\n");return 0;}printf("%d extended analytical test(s) FAILED\n",failures);return 1;
}
