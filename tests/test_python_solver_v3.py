import pypokereval


class OneStepGame:
    def is_terminal(self, state):
        return state[0] == "terminal"

    def acting_player(self, state):
        return 0

    def action_count(self, state):
        return 2

    def infoset_key(self, state):
        return 7

    def apply_action(self, state, action):
        return ("terminal", action)

    def terminal_values(self, state):
        value = 1.0 if state[1] == 0 else -1.0
        return [[value], [-value]]


solver = pypokereval.solver_v3_create(
    ("root",), OneStepGame(), max_iterations=1, expected_infosets=1
)
pypokereval.solver_v3_run(solver)
progress = pypokereval.solver_v3_progress(solver)
assert progress["complete"]
assert progress["iteration"] == 1
assert pypokereval.solver_v3_strategy(solver, 0) == [0.5, 0.5]
