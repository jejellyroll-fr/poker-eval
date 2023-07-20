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

        output = subprocess.check_output(cmd).decode()
        return output



p = Pokenum(game_type='h', pockets=['Ac', '7c', '-', '5s', '4s', '-', 'Ks', 'Kd'])
output = p.evaluate()
print(output)

p = Pokenum(game_type='h', pockets=['Ac', '7c','-',  '5s', '4s','-',  'Ks', 'Kd'])
output = p.evaluate()
print(output)

p = Pokenum(game_type='h', pockets=['Ac', '7c','-',  '5s', '4s', '-', 'Ks', 'Kd'], board=['7h', '2c', '3h'])
output = p.evaluate()
print(output)

p = Pokenum(game_type='o', pockets=['As', 'Kh',  'Qs', 'Jh'], board=['8h', '8d', '7h', '6d'])
output = p.evaluate()
print(output)

p = Pokenum(game_type='o', pockets=['As', 'Kh', 'Qs', 'Jh', '-', '8h', '8d', '7h', '6d'])
output = p.evaluate()
print(output)

p = Pokenum(game_type='o', pockets=['As', 'Kh', 'Qs', 'Jh', '-', '8h', '8d', '7h', '6d'], board=['8s', 'Ts', 'Jc'])
output = p.evaluate()
print(output)

p = Pokenum(game_type='o85', pockets=['As', 'Kh', 'Qs', 'Jh', 'Ts','-', '8h', '8d', '7h', '6d', '9c'], mc=True, niter=1000000)
output = p.evaluate()
print(output)

p = Pokenum(game_type='o5', pockets=['As', 'Kh', 'Qs', 'Jh', 'Ts', '-', '8h', '8d', '7h', '6d', '9c'], mc=True, niter=1000000)
output = p.evaluate()
print(output)

p = Pokenum(game_type='o6', pockets=['As', 'Kh', 'Qs', 'Jh', 'Ts', '9d', '-', '8h', '8d', '7h', '6d', '9c', '6c'], mc=True, niter=1000000)
output = p.evaluate()
print(output)

p = Pokenum(game_type='7s', pockets=['As', 'Ah', 'Ts', 'Th', '8h', '8d', '-', 'Qc', 'Jc', 'Td', '3c', '2d', 'Kc'])
output = p.evaluate()
print(output)


p = Pokenum(game_type='7s', pockets=['As', 'Ah', 'Ts', 'Th', '8h', '8d', '-',  'Qc', 'Jc', 'Td', '3c', '2d', 'Kc'], dead=['5c', '6c', '2s', 'Jh'])
output = p.evaluate()
print(output)




p = Pokenum(game_type='l27', pockets=['5h', '4h', '3h', '2h', '/', '5s', '-', '9s', '8h', '6d', '4c','/', 'Kd'], mc=True, niter=10000)
output = p.evaluate()
print(output)


p = Pokenum(game_type='l27', pockets=['5h', '4h', '3h', '2h', '-', '9s', '8h', '6d', '4c'], dead=['Kd','5s'],  mc=True, niter=10000)
output = p.evaluate()
print(output)

p = Pokenum(game_type='l27', pockets=['5h', '4h', '3h', '-','9s', '8h', '6d'], dead=['5s', 'Qd', 'Ks', 'Kh'], mc=True, niter=10000)
output = p.evaluate()
print(output)
