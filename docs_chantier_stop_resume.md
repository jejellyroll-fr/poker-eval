# Chantier — « Stop » gracieux + « Reprendre » (poker-eval Studio)

## Objectif

Rendre le bouton **Stop solve** utile : au lieu d'un arrêt brutal qui perd tout(s, il doit :
1. **arrêter le solveur à un point sûr** (itération propre(,
2. **sauvegarder l'état mathématique** (regrets/stratégies) dans un checkpoint,
3. **afficher les résultats en cours** (rapport de résultats partiels(,
4. permettre **« Reprendre »** le même solve **en continuant** depuis ce point d'arrêt.

.



## Contexte technique ( état constaté

- Le Studio lance le solve dans **`/bin/bash -c <cmd>`** via `bproc_exec` (NAppGUI(`.
`proc->pid` est donc le shell bash.
- **Bug d'origine** : `bproc_cancel` = `kill(pid` ,SIGKILL( → tuait le **shell seul**, sans le solveur descendant(lequel gardait le pipe ouvert → `read()` de `i_solve_main` bloquait **pour toujours** → « Stop » semblait impuissant.
- L'engine `pe_solver` supporte **déjà** le cycle de vie `RUNNING/PAUSED/STOPPED`, `pe_solver_stop()` coordonné, et `pe_solver_save()`/`pe_solver_load()` (API persist checkpoint-v2( ; `--resume` recharge et reprend à l'itération sauvegardée.
.
 . Ce qui était branché/en place avant ce travail ： l'adapter persist existait mais **pas exposé** par le driver CLI ni demandé par le Studio.

 .## Étapes livrées

### ✅ Étape 1 — Correction du « Stop » casse (bproc, Unix）

**Fichier** : `build-studio/nappgui_src/src/osbs/unix/bproc.c`
**Problème** : `kill(proc->pid, SIGKILL)` n'atteint pas le vrai solveur（descendant du shell(→ pipe ouvert → `read()` bloqué.
**Fix** :
- l'enfant devient **leader de groupe de processus** avant exec：`setpgid(0, 0)；`
- `bproc_cancel` tue **toute la groupe**：`kill(-proc->pid, SIGKILL)`。
abq
**Validation** : cible `osbs` compilée ✓.

### ✅ Étape 2 — Driver CLI： arrêt gracieux + checkpoint/resume（pe-preflop-solve）

**Fichier** : `tools/pe_preflop_solve.c`（portfolio headers/champs/parse/help——
 - flags **`--checkpoint FILE`**, **`--resume FILE`**, **`--checkpoint-interval N`**；
 - port persistance câblé ：`deps.persist = pe_persist_checkpoint_ops()`；
 - **arrêt gracieux** ： handler `SIGINT/SIGTERM` → watcher-thread appelant `pe_solver_stop()`（sans deadlock(depuis le thread non-solveant〕→ `pe_solver_run` s'arrête proprement→ **sauvegarde checkpoint**→ **rapport de résultats partiels** imprimé→ sortie nette；
 - **reprise** ： `--resume` → `pe_solver_load` → poursuite vers l'itération cible → re-sauvegarde；
 - générateur `spot_hash`（évite de reprendre un checkpoint d'un autre spot（.


 **Validation**（ tests réels：
 | Test | Résultat |
 |---|---|
 | sauvegarde fin de run | `checkpoint_saved=1 iteration=…` fichier ✓ |
 | `--resume` | `resumed_checkpoint=1` poursuit ✓ |
 | **Stop mi-parcours**（`SIGTERM` at≈1s( | sauvegarde à `iteration=3328/50000` + rapport partiel imprimé ✓ |
 | reprise depuis le checkpoint mi-parcours | complet → re-sauvegarde finale ✓ |

aport à savegle : `continuation_iteration=0` affiché après `--resume` est cosmétique（`pe_solver_progress` refuse l'itération tant que l'état est `CREATED`（la poursuite réelle est prouvée par le run complet（.

### ✅ Étape 3 — Studio UI： arrêt gracieux + bouton Reprendre（livré）

**Objectifs**：
1. `i_solve_request_stop` envoie **`SIGTERM` au groupe** d'abord（grace window(, puis **`SIGKILL`** en repli si le solveur n'aboutit pas.
2. la commande émise par `i_on_solve` inclut **`--checkpoint <spot>.ckpt`**；si un checkpoint est produit, le bouton bascule sur **Reprendre**；
3. **Reprendre** rejoue la **même commande** avec **`--resume <spot>.ckpt`**（et `--checkpoint` le même, pour continuer la chaîne）.

**Fichiers modifiés**：
- `build-studio/nappgui_src/src/osbs/bproc.h` : nouvelle déclaration `_osbs_api bool_t bproc_terminate(Proc *proc)`（après `bproc_cancel`）。
- `build-studio/nappgui_src/src/osbs/unix/bproc.c` : implémentation SIGTERM-groupe `bproc_terminate`（envoie `kill(-pid, SIGTERM)`）。
- `build-studio/nappgui_src/src/osbs/win/bproc.c` : stub Windows（fallback vers `bproc_cancel` — pas de job handle ici）。
- `tools/poker_eval_studio.c` :
  - **App struct** : nouveaux champs `solve_checkpoint_available`, `solve_resume_mode`, `solve_terminate_sent`, `solve_checkpoint_path[1024]`.
  - **`spot_checkpoint_path(tree_path,...)`** : dérive `<basename>.ckpt` à côté du `.tree`.
  - **`i_on_solve`** : insère `--checkpoint <path>` dans la commande preflop ; en mode reprise, ajoute aussi `--resume <path>`. Au prochain clic, si un checkpoint est disponible, rejoue la dernière commande en mode reprise au lieu de relire le Setup.
  - **`i_solve_request_stop`** : 1er clic → `bproc_terminate` (SIGTERM-groupe), 2e clic → `bproc_cancel` (SIGKILL-groupe) ; libellé bouton passe à "Force kill".
  - **`i_solve_scan_line`** : détecte `checkpoint_saved=1 path=...` dans la sortie CLI et remplit `solve_checkpoint_path`.
  - **`i_solve_end`** : bouton bascule sur "Resume this spot" quand un checkpoint est disponible, sinon reste sur "Solve this spot".

**Validation** :
- Build `osbs` + `poker-eval-studio` ✓ (macOS Darwin 26.2).
- Smoke test SIGTERM mid-run (CLI direct, `iter=18000/60000`, `--checkpoint-interval 5000`) :
  - `checkpoint_saved=1 path=/tmp/smoke_ckpt/midrun.ckpt iteration=18000` ✓
  - rapport partiel imprimé ✓
  - `exit=0` (sortie propre, pas de SIGKILL).
- Resume : `--resume /tmp/smoke_ckpt/smoke.ckpt` → `resumed_checkpoint=1` → re-`checkpoint_saved=1` final ✓.

## 🚧 Restes

- Conserver le patch `bproc.c` au-delà de `build-studio/`（gitignoré）： appliquer idempotent dans `build_studio.sh` ou vendor du fichier（ →
- Nettoyer les cosmétiques de libellé（`solver_phase=complete` après stop（report stop_reason（.
- `i_solve_end` n'active **Reprendre** que si la sortie contient `checkpoint_saved=1`（sinon le bouton reste sur "Solve this spot"（.

 。## Notes de portabilité

- **Unix**： le fix du groupe de processus + `SIGTERM` gracieux s'applique.（macOS/Linux（.
- **Windows** ： `bproc_cancel` = `TerminateProcess`（déjà brutal（, ле n'utilise pas de groupe de processus; `bproc_terminate` (stub) retombe sur `bproc_cancel` — le bouton "Force kill" reste le filet de sécurité.