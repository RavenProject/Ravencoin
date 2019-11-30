from ravenrpc import Ravencoin # pip install ravenrpc
import sys
import os
import time

def valid_base58(s):
    for c in ('0', 'O', 'l', '1'):
        if c in s:
            print(f'Cannot use charachter "{c}" in prefix!!')
            exit()

def create_rvn_inst():
    try:
        rvn = Ravencoin(sys.argv[1], sys.argv[2])
    except IndexError:
        print('You must provide a valid rpc username and rpc password!')
        exit()
    return rvn

def get_args():
    try:
        prefix = sys.argv[3]
    except IndexError:
        print('Missing options!\n'
            'Usage: `python gen.py <rpcuser> <rpcpass> <prefix> <ignore case (default True)>`')
        exit()
    ignore_case = sys.argv[4] if len(sys.argv) > 4 else True
    if prefix[0] != 'R':
        print('Please start your prefix with an "R"\n'
            'Usage: `python gen.py <rpcuser> <rpcpass> <prefix> <ignore case (default True)>`')
        exit()
    return prefix, ignore_case, create_rvn_inst()

def validate_rvn(rvn):
    if rvn.getinfo() == '':
        print('Run ravend')
        exit()

def display_stats(count, start, addr, prefix):
    if not count % 30 == 1:
        return
    os.system('cls' if os.name == 'nt' else 'clear')
    print(f'\u001b[33;1mTried {count} addresses\n\u001b[0m'
          f'\u001b[32;1mSeconds Spent: {time.time() - start}\n\u001b[0m'
          f'\u001b[36;1mAve. Addresses/sec: {count/(time.time() - start)}\n'
          f'Lastest Address: {addr}\n'
          f'Prefix: {prefix}\n'
          'To stop, press Ctrl-C\u001b[0m', flush=True, end='')
    

def main():
    prefix, ignore_case, rvn = get_args()
    validate_rvn(rvn)
    valid_base58(prefix)
    start = time.time()
    count = 0
    address = rvn.getnewaddress()['result']
    try:
        if ignore_case:
            while not address.lower().startswith(prefix.lower()):
                address = rvn.getnewaddress()['result']
                count += 1
                display_stats(count, start, address, prefix)
        else:
            while not address.startswith(prefix):
                address = rvn.getnewaddress()['result']
                count += 1
                display_stats(count, start, address, prefix)
    except KeyboardInterrupt:
        exit()
    print(f'Found an address that starts with {prefix}! It is saved to your wallet. Use it freely!\n{address}')

if __name__ == '__main__':
    main()
