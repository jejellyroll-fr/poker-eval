# DIST-04 — Étude CFR-D et découpage des WorkUnits

## Conclusion

Le dépôt possède déjà une implémentation exploitable de CFR-D dans
`src/engine/solvers/cfr/cfr_resolve.c`. Elle est adaptée à une résolution locale
d’un sous-arbre, mais elle ne constitue pas encore un protocole distribué : il
manque l’adaptateur entre `pe_work_unit_t` et `cfr_game_t`, ainsi que la fusion
déterministe des résultats d’un worker dans le blueprint.

## Ce que fournit déjà `cfr_resolve.c`

| Élément | Rôle | Réutilisable par DIST-02/03 |
|---|---|---|
| `pe_cfr_subgame_t` | Racine du sous-jeu, joueur résolu et frontière | Oui, le `root_state_key` correspond au `public_state` d’un WorkUnit |
| `pe_cfr_boundary_t` | Reach et CFV blueprint de chaque infoset frontière | Oui, à transporter ou recalculer côté worker |
| `pe_cfr_blueprint_cfv()` | Calcule les CFV du blueprint sur la frontière | Oui, avant une résolution locale |
| `pe_cfr_seed_resolve_storage()` | Copie le blueprint et verrouille le trunk | Oui, mode fallback simple |
| `pe_cfr_resolve_subgame()` | Gadget `follow/terminate` et contrôle des marges | Oui en heads-up |

Le résultat expose déjà `worst_margin`, `mean_margin`, les fréquences de suivi,
le nombre d’itérations et le nombre d’infosets entraînés. Ces champs sont les
minimums nécessaires pour la télémétrie d’un worker.

## Limites importantes

1. Le gadget CFR-D est défini pour deux joueurs. Pour un jeu multiway,
   `pe_cfr_resolve_subgame()` exige `lock_trunk`; sans cette option il renvoie
   `PE_CFR_RESOLVE_UNSUPPORTED`. Le mode trunk-lock résout donc localement,
   mais ne fournit pas la garantie CFR-D générale.
2. La frontière est limitée à `PE_CFR_RESOLVE_MAX_BOUNDARY` (256) et doit être
   exprimée en clés d’infosets et valeurs CFV. Un WorkUnit qui ne transporte
   qu’un snapshot de regrets ne suffit pas à reconstruire automatiquement
   cette frontière pour tous les adaptateurs de jeu.
3. La storage est clonée par worker. La fusion n’est sûre que si les WorkUnits
   ont une propriété d’ownership déterministe des infosets, ou si un reducer
   applique des deltas avec une règle explicitement définie. Une simple somme
   de snapshots est incorrecte pour des regrets CFR déjà cumulés.
4. Les boards d’un WorkUnit peuvent partager des infosets après canonicalisation.
   Le coordinateur doit donc regrouper les boards qui ont un propriétaire
   commun, ou refuser les unités qui se recouvrent avant dispatch.

## Contrat recommandé pour la suite

Le worker doit suivre cette séquence :

1. décoder et valider le WorkUnit ;
2. sélectionner son backend local via `pe_runtime_recommended_backend()` ;
3. reconstruire le sous-jeu depuis `public_state` et les boards ;
4. obtenir la frontière du blueprint et appeler `pe_cfr_resolve_subgame()` en
   heads-up, ou le trunk-lock fallback en multiway ;
5. renvoyer un résultat versionné contenant les marges, les compteurs et un
   delta de storage possédant une clé d’ownership non ambiguë ;
6. fusionner les deltas dans l’ordre `(public_state, worker_id, iteration_begin)`
   avant de publier le nouveau blueprint.

Le framing binaire DIST-03 transporte déjà les messages `CAPABILITIES`, `UNIT`
et `RESULT`; la prochaine implémentation doit définir le payload `RESULT` et
les règles d’ownership avant d’ouvrir des sockets TCP.

## Décision

DIST-04 reste une étude clôturée avec un chemin d'intégration clair. Aucun
changement n'est proposé dans `cfr_resolve.c` tant que l'adaptateur de jeu et
le reducer distribué ne sont pas spécifiés : modifier le gadget maintenant
augmenterait le risque sans rapprocher le DoD de DIST-03.

## Implémentation DIST-02/03

Le contrat décrit ci-dessus est maintenant câblé par
`cfr_work_adapter.h/c` : un builder reconstruit un `cfr_game_t` depuis le
WorkUnit, `pe_cfr_work_execute()` exécute le vrai `cfr_solve()` et exporte un
snapshot CFR endian-indépendant. Le reducer applique ensuite ces snapshots dans
l'ordre déterministe `(public_state, iteration_begin, iteration_end, worker)`.
Un builder Hold'em river fixe est fourni pour les WorkUnits dont `ranges`
contient `(h0, h1)` et dont `boards` contient le board en masques uint64
big-endian.

Le protocole RESULT transporte aussi `elapsed_ns` et `units_per_s`, mesurés par
le worker. Les helpers TCP (`listen`, `accept`, `connect`) et la session worker
permettent d’exercer le cycle réseau complet ; le test d’intégration river
compare la stratégie et l’exploitabilité du chemin WorkUnit à un solve local.

Le builder Hold’em accepte en outre un `pe_compute_ops_t` optionnel : lorsqu’un
worker dispose d’un backend CUDA/OpenCL réellement initialisé, les évaluations
terminales du `cfr_solve()` passent par son port `terminal_eval_batch`. Le test
river exécute alors aussi la parité CUDA ; sur une machine CPU seule, cette
branche est un skip de capacité explicite. HIP et Metal ne sont pas inclus dans
le périmètre de cette implémentation : aucun backend correspondant n’existe
encore dans l’ABI/runtime v3 du dépôt.
