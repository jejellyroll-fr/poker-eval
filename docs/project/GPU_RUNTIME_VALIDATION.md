# Validation runtime des backends GPU

`pe_runtime_probe()` crée chaque backend GPU disponible et exécute un vecteur
de parité court contre `cpu_ref` avant de publier ses capabilities. Quand la
parité stratégie, update et terminal réussit, le probe ouvre les deux verrous
processus (`PE_CAP_GPU_TERMINAL_EVAL` et `PE_CAP_GPU_REGRET_UPDATE`). Un solve
configuré sur `cuda` ou `opencl` peut donc atteindre le backend sans appel
spécial dans les tests.

Le résultat est conservé dans `pe_runtime_backend_info_t` : `validated` indique
que le vecteur de parité a réussi, tandis que `runtime_available` indique que
le backend est effectivement proposé au routeur.

Pour diagnostiquer un problème GPU sans exposer le backend au solveur, définir
`PE_GPU_SKIP_PARITY=1`. Le probe continue la validation, mais laisse les portes
fermées et explique ce choix dans `reason`. Cette variable est volontairement
un opt-out de disponibilité, pas un contournement de la validation.
