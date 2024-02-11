import subprocess

cmd = ['./pokenum', '-mc', '1000000', '-o85', 'As', 'Kh', 'Qs', 'Jh', 'Ts', '--', '8h', '8d', '7h', '6d', '9c']
result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

if result.returncode == 0:
    output = result.stdout.decode()
    print(output)
else:
    error = result.stderr.decode()
    print(error)
