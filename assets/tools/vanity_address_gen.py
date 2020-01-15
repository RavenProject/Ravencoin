from ravenrpc import Ravencoin # pip install ravenrpc
import sys
import os
import time

# Requires ravenrpc==0.2.4
#
# usage:
# ```
# python vanity_address_gen.py ( rpcuser ) ( rpcpass ) ( prefix ) ( ignore_case=True )
# ```



def valid_base58(s):
    for c in ('0', 'O', 'l', '1'):
        if c in s:
            print(f'Cannot use charachter "{c}" in prefix!!')
            exit()

def success_odds(prefix, ignore_case):
    odds = 1
    for letter in prefix[1:]:
        if letter in ('o', 'l') or not ignore_case:
            odds *= 58
        else:
            odds *= 29
    return odds

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

def display_stats(count, start, addr, prefix, odds):
    addr_sec = count/(time.time() - start)
    os.system('cls' if os.name == 'nt' else 'clear')
    print(f'Tried {count} addresses\n'
          f'Seconds Spent: {time.time() - start}\n'
          f'Ave. Addresses/sec: {addr_sec}\n'
          f'Lastest Address: {addr}\n'
          f'Prefix: {prefix}\n'
          f'You have a 1 in {odds} chance per address!\n'
          f'At this rate, you will find your address in ~{odds/addr_sec/60} minutes!\n'
          'To stop, press Ctrl-C', flush=True, end='')


def main():
    prefix, ignore_case, rvn = get_args()
    validate_rvn(rvn)
    valid_base58(prefix)
    start = time.time()
    odds = success_odds(prefix, ignore_case)
    count = 0
    address = rvn.getnewaddress()['result']
    try:
        while not address.lower().startswith(prefix.lower()) if ignore_case else address.startswith(prefix):
            address = rvn.getnewaddress()['result']
            count += 1
            if count % 100 == 1:
                display_stats(count, start, address, prefix, odds)
    except KeyboardInterrupt:
        exit()
    print(f'\nFound an address that starts with {prefix}!\n It is saved to your wallet. Use it freely!\n{address}')

if __name__ == '__main__':
    main()
