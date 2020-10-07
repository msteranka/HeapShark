import json

json_file = '../src/data.json'

with open(json_file, 'r') as f:
    data = json.load(f)

for x in data['objects']:
    print('Address: ' + hex(x['address']))
    print('\tSize: ' + str(x['size']))
    invocation = x['mallocBacktrace']['0']
    if invocation == "":
        print('\tInvocation: (NIL)')
    else:
        print('\tInvocation: ' + invocation)

