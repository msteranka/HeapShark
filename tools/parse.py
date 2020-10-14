import json

json_file = '../src/data.json'

with open(json_file, 'r') as f:
    data = json.load(f)

thread_dict = {}
for x in data['objects']:
    if x['size'] == 47:
        thread_id = x['allocatingThread']
        if thread_id not in thread_dict:
            thread_dict[thread_id] = 0
        thread_dict[thread_id] += 1
        assert (x['numWrites'] == 47), 'numWrites'
        assert (x['bytesWritten'] == 47), 'bytesWritten'
        assert (x['writeCoverage'] == 1), 'writeCoverage'
        assert (x['mallocBacktrace']['0'] == '/home/msteranka/bullshit/blah/memory-profiler/test/multithreaded-test.cpp:6'), 'mallocBacktrace'
    # print('Address: ' + hex(x['address']))
    # print('\tSize: ' + str(x['size']))
    # invocation = x['mallocBacktrace']['0']
    # if invocation == "":
    #     print('\tInvocation: (NIL)')
    # else:
    #     print('\tInvocation: ' + invocation)

print(thread_dict)
