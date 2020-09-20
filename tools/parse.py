import json

json_file = '../src/data.json'

with open(json_file, 'r') as f:
    data = json.load(f)

for x in data['objects']:
    print('Address: ' + hex(x['address']))
    print('Size: ' + str(x['size']))
    invocation = x['mallocBacktrace']['0']
    if invocation == "":
        print('Invocation: (NIL)\n')
    else:
        print('Invocation: ' + invocation + '\n')

