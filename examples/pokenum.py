import subprocess

class Pokenum:
    def __init__(self, game_type='h', pockets=[], board=[], dead=[], mc=False, niter=1000, terse=False, hist=False):
        self.game_type = game_type
        self.pockets = pockets
        self.board = board
        self.dead = dead
        self.mc = mc
        self.niter = niter
        self.terse = terse
        self.hist = hist

    def evaluate(self):
        cmd = ['./pokenum']

        if self.mc:
            cmd.append('-mc')
            cmd.append(str(self.niter))

        if self.terse:
            cmd.append('-t')

        if self.hist:
            cmd.append('-O')

        if self.game_type in ['h', 'h8', 'o', 'o5', 'o6', 'o8', 'o85', '7s', '7s8', '7snsq', 'r', '5d', '5d8', '5dnsq', 'l', 'l27']:
            cmd.append('-' + self.game_type)
        else:
            raise ValueError('Invalid game type')

        for pocket in self.pockets:
            cmd.append(pocket)

        if self.board:
            cmd.append('--')
            for card in self.board:
                cmd.append(card)

        if self.dead:
            cmd.append('/')
            for card in self.dead:
                cmd.append(card)

        output = subprocess.check_output(cmd)
        return output.decode()


p = Pokenum(game_type='h', pockets=['Ac', '7c', '-', '5s', '4s', '-', 'Ks', 'Kd'])
output = p.evaluate()
print(output)