#Shows data from the transactions

import random
import os
import subprocess
import json


#Set this to your raven-cli program
cli = "raven-cli"

mode = "-testnet"
mode = ""
rpc_port = 18766
#Set this information in your raven.conf file (in datadir, not testnet3)
rpc_user = 'rpcuser'
rpc_pass = 'rpcpass555'

def get_rpc_connection():
    from bitcoinrpc.authproxy import AuthServiceProxy, JSONRPCException
    connection = "http://%s:%s@127.0.0.1:%s"%(rpc_user, rpc_pass, rpc_port)
    rpc_conn = AuthServiceProxy(connection)
    return(rpc_conn)

rpc_connection = get_rpc_connection()

def rpc_call(params):
    process = subprocess.Popen([cli, mode, params], stdout=subprocess.PIPE)
    out, err = process.communicate()
    process.stdout.close()
    process.stderr.close()
    return(out)

def get_blockinfo(num):
    hash = rpc_connection.getblockhash(num)
    blockinfo = rpc_connection.getblock(hash)
    return(blockinfo)

def get_block(hash):
    blockinfo = rpc_connection.getblock(hash)
    return(blockinfo)

def get_rawtx(tx):
    txinfo = rpc_connection.getrawtransaction(tx)
    return(txinfo)

def get_bci():
    bci = rpc_connection.getblockchaininfo()
    return(bci)

def decode_rawtx(txdata):
    #print("decoding: " + txdata)
    txjson = rpc_connection.decoderawtransaction(txdata)
    return(txjson)    

def decode_script(script):
    scriptinfo = rpc_connection.decodescript(script)
    return(scriptinfo)   

def ipfs_add(file):
    print("Adding to IPFS")
    import ipfsapi
    api = ipfsapi.connect('127.0.0.1', 5001)
    res = api.add(file)
    print(res)
    return(res['Hash'])

def ipfs_get(hash):    
    import ipfsapi
    api = ipfsapi.connect('127.0.0.1', 5001)
    res = api.get(hash)
    return()

def ipfs_pin_add(hash):
    import ipfsapi
    api = ipfsapi.connect('127.0.0.1', 5001)
    res = api.pin_add(hash)
    return(res)

def asset_handler(asset_script):
    # print("Type: " + asset_script.get('type'))
    # print("Asset: " + asset_script.get('asset_name'))
    # print(asset_script.get('amount'))
    # print(asset_script.get('units'))
    # print("Reissuable: " + str(asset_script.get('reissuable')))
    # print("Has IPFS: " + str(asset_script.get('hasIPFS')))
    if asset_script.get('hasIPFS') == True:
        print(asset_script.get('ipfs_hash'))
        ipfs_pin_add(asset_script.get('ipfs_hash'))

#Get the blockheight of the chain
blockheight = get_bci().get('blocks')

for i in range(23500,blockheight):
    dta = get_blockinfo(i)
    print("Block #" + str(i) + " - " + dta.get('hash'))
    #print(dta.get('difficulty'))
    #print(dta.get('time'))

    tx_in_block = get_block(dta.get('hash'))
    txs = tx_in_block.get('tx')
    #print(txs)
    for tx in txs:
        tx_info = get_rawtx(tx)
        #print("txinfo: " + tx_info)
        tx_detail = decode_rawtx(tx_info)
        for vout in tx_detail.get('vout'):
            #print("vout: " + str(vout.get('value')))
            #print(vout.get('scriptPubKey').get('asm'))
            if (vout.get('scriptPubKey').get('asm')[86:98] == "OP_RVN_ASSET"):
                #print("Found OP_RVN_ASSET")
                #print(vout.get('scriptPubKey').get('hex'))
                asset_script = decode_script(vout.get('scriptPubKey').get('hex'))
                asset_handler(asset_script)
                #print(asset_script)
        #print("txdecoded: " + tx_detail.get('vout'))


    #print("")

