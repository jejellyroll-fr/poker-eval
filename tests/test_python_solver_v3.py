import pypokereval


class OneStepGame:
    def __init__(self):
        self.reaches = []

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

    def terminal_values(self, state, reach):
        assert len(reach) == 2
        assert all(len(player_reach) == 1 for player_reach in reach)
        self.reaches.append(reach)
        value = 1.0 if state[1] == 0 else -1.0
        return [[value], [-value]]


class LegacyOneArgGame(OneStepGame):
    def terminal_values(self, state):
        value = 1.0 if state[1] == 0 else -1.0
        return [[value], [-value]]


game = OneStepGame()
solver = pypokereval.solver_v3_create(
    ("root",), game, max_iterations=1, expected_infosets=1
)
try:
    pypokereval.solver_v3_strategy(solver, 0)
except RuntimeError as exc:
    assert "status" in str(exc)
else:
    raise AssertionError("strategy queries before solve must raise RuntimeError")
try:
    pypokereval.solver_v3_metrics(solver)
except RuntimeError as exc:
    assert "status" in str(exc)
else:
    raise AssertionError("metrics queries before solve must raise RuntimeError")
try:
    pypokereval.solver_v3_progress(solver)
except RuntimeError as exc:
    assert "status" in str(exc)
else:
    raise AssertionError("progress queries before solve must raise RuntimeError")
pypokereval.solver_v3_run(solver)
progress = pypokereval.solver_v3_progress(solver)
assert progress["complete"]
assert progress["iteration"] == 1
assert pypokereval.solver_v3_strategy(solver, 0) == [0.5, 0.5]
assert game.reaches

try:
    pypokereval.solver_v3_create(("root",), game, combos=65537)
except ValueError:
    pass
else:
    raise AssertionError("solver_v3_create must reject combo counts above uint16_t")

legacy_solver = pypokereval.solver_v3_create(
    ("root",), LegacyOneArgGame(), max_iterations=1, expected_infosets=1
)
pypokereval.solver_v3_run(legacy_solver)


class CompatibleComboGame(OneStepGame):
    def combo_compatible(self, state, player, player_combo, opponent,
                         opponent_combo):
        return player != opponent and player_combo != opponent_combo

    def terminal_values(self, state, reach):
        value = 1.0 if state[1] == 0 else -1.0
        return [[value, value], [-value, -value]]


compatible_game = CompatibleComboGame()
compatible_solver = pypokereval.solver_v3_create(
    ("root",), compatible_game, players=2, combos=2, max_iterations=1,
    expected_infosets=1
)
pypokereval.solver_v3_run(compatible_solver)


class IncompleteChanceGame(OneStepGame):
    def is_chance(self, state):
        return False

    def chance_outcome_count(self, state):
        return 1


try:
    pypokereval.solver_v3_create(("root",), IncompleteChanceGame())
except TypeError:
    pass
else:
    raise AssertionError("solver_v3_create must reject incomplete chance callbacks")


class ChanceGame(OneStepGame):
    def is_terminal(self, state):
        return state[0] == "terminal"

    def is_chance(self, state):
        return state[0] == "chance"

    def chance_outcome_count(self, state):
        return 2

    def chance_outcome_weight(self, state, outcome):
        return 0.75 if outcome == 0 else 0.25

    def apply_chance(self, state, outcome):
        return ("terminal", outcome)


chance_solver = pypokereval.solver_v3_create(
    ("chance",), ChanceGame(), max_iterations=1, expected_infosets=1
)
pypokereval.solver_v3_run(chance_solver)
