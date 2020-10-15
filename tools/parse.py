import json

json_file = '../src/data.json'

with open(json_file, 'r') as f:
    data = json.load(f)

thread_dict = {}
for x in data['objects']:
    if x['allocatingThread'] == 0:
        continue
    thread_id = x['allocatingThread']
    if thread_id not in thread_dict:
        thread_dict[thread_id] = 0
    thread_dict[thread_id] += 1
    # print('Address: ' + hex(x['address']))
    # print('\tSize: ' + str(x['size']))
    # invocation = x['mallocBacktrace']['0']
    # if invocation == "":
    #     print('\tInvocation: (NIL)')
    # else:
    #     print('\tInvocation: ' + invocation)

print(thread_dict)
