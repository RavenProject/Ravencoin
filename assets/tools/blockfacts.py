#Shows data from the first 1000 blocks

import random
import os
import subprocess
import json
from rpc_auth import rpc_connection_url


#Set this to your raven-cli program
cli = "raven-cli"

#mode = "-testnet"
mode = ""
rpc_port = 8766
#Set this information in your raven.conf file (in datadir, not testnet3)
rpc_user = os.environ.get('RAVEN_RPC_USER', '')
rpc_pass = os.environ.get('RAVEN_RPC_PASSWORD', '')


def rpc_call(params):
    process = subprocess.Popen([cli, mode, params], stdout=subprocess.PIPE)
    out, err = process.communicate()
    return(out)

def get_blockinfo(num):
    rpc_connection = get_rpc_connection()
    hash = rpc_connection.getblockhash(num)
    blockinfo = rpc_connection.getblock(hash)
    return(blockinfo)

def get_rpc_connection():
    if not rpc_user or not rpc_pass:
        raise RuntimeError("Set RAVEN_RPC_USER and RAVEN_RPC_PASSWORD before running this tool")
    from bitcoinrpc.authproxy import AuthServiceProxy, JSONRPCException
    connection = rpc_connection_url(rpc_user, rpc_pass, rpc_port)
    #print("Connection: " + connection)
    rpc_connection = AuthServiceProxy(connection)
    return(rpc_connection)

for i in range(1,1000):
    dta = get_blockinfo(i)
    print("Block #" + str(i))
    print(dta.get('hash'))
    print(dta.get('difficulty'))
    print(dta.get('time'))
    print("")

