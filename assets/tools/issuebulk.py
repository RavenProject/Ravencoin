#!/usr/bin/env python3
# Script to issue assets on the Ravencoin platform
# Reads from a csv file
# Template Google Spreadsheet at:  
#   https://docs.google.com/spreadsheets/d/1Ym88-ggbw8yiMgVxOtVYDsCXJGNGZqlpOfgdbVK8iYU
# In Google Sheets: File->Download As->.csv
# Prerequisite: ravend daemon to be running
# In order to use metadata, you must install be running IPFS
# Steps:
#   1. Get IPFS - https://ipfs.io/
#   2. Run the ipfs daemon
#   3. pip install ipfsapi
#
# If you're signing contract_url
# pip install python-bitcoinrpc


import random
import os
import subprocess
import csv
import json
import hashlib

from safe_url import fetch_public_https


#Set this to your raven-cli program
cli = "raven-cli"

mode =  "-testnet"
rpc_port = 18766
#mode =  "-regtest"
#rpc_port = 18443
csv_file = "Raven Assets - Sheet1.csv"
#Set this information in your raven.conf file (in datadir, not testnet3)
rpc_user = os.environ.get('RAVEN_RPC_USER', '')
rpc_pass = os.environ.get('RAVEN_RPC_PASSWORD', '')


def NormalizeMetaData(dict):
    #Remove all empty items    
    for key in list(dict.keys()):
        if dict[key] == '':
            del dict[key] 

    del dict['asset']
    del dict['qty']
    del dict['units']
    del dict['reissuable']

    #Convert from text to bool
    if dict.get('forsale') == 'TRUE': 
        dict['forsale'] = True

      
    return(dict)

def issue_asset(asset, qty, units, reissuable = False, address='', ipfs_hash = ''):
    cmd = [cli, mode, "issue", asset, str(qty), address, "", str(units), str(bool(reissuable)).lower()]

    if len(ipfs_hash) > 0:
        cmd.extend(["true", ipfs_hash])

    print("Issuing asset: " + asset)
    subprocess.run(cmd, check=True)

def get_contract_hash(url):
    print ("Downloading: " + url)
    rawdata = fetch_public_https(url)
    #print("Len: " + len(rawdata))
    hexdigest = hashlib.sha256(rawdata).hexdigest()
    print(hexdigest)
    return(hexdigest)

def rpc_call(params):
    process = subprocess.Popen([cli, mode, params], stdout=subprocess.PIPE)
    out, err = process.communicate()
    return(out)

def get_address():
    rpc_connection = get_rpc_connection()
    new_address = rpc_connection.getnewaddress()
    print("New address: " + new_address)
    return(new_address)

def sign_hash(address, hash):
    rpc_connection = get_rpc_connection()
    signature = rpc_connection.signmessage(address, hash)
    return(signature)

def generate_blocks(n):
    rpc_connection = get_rpc_connection()
    hashes = rpc_connection.generate(n)
    return(hashes)

def get_rpc_connection():
    if not rpc_user or not rpc_pass:
        raise RuntimeError("Set RAVEN_RPC_USER and RAVEN_RPC_PASSWORD before running this tool")
    from bitcoinrpc.authproxy import AuthServiceProxy, JSONRPCException
    connection = "http://%s:%s@127.0.0.1:%s"%(rpc_user, rpc_pass, rpc_port)
    print("Connecting to local Ravencoin RPC")
    rpc_connection = AuthServiceProxy(connection)
    return(rpc_connection)

def add_to_ipfs(file):
    print("Adding to IPFS")
    import ipfsapi
    api = ipfsapi.connect('127.0.0.1', 5001)
    res = api.add(file)
    print(res)
    return(res['Hash'])



if mode == "-regtest":  #If regtest then mine our own blocks
    subprocess.run([cli, mode, "generate", "400"], check=True)

with open(csv_file, "r") as csvfile:
    #print(rpc_call('getbestblockhash'))
    reader = csv.DictReader(csvfile)
    for issue in reader:
        if issue.get('reissuable') == 'TRUE': 
            issue['reissuable'] = True
        else: 
            issue['reissuable'] = False
        print(issue['asset'], issue['qty'], issue['units'])
        asset = issue['asset']
        meta = issue.copy();
        NormalizeMetaData(meta)
        print(issue['asset'])

        #If a contract_url is present, then sign it and add the proof to the meta data
        if meta.get('contract_url'):
            meta['contract_hash'] = get_contract_hash(meta['contract_url'])
            meta['contract_address'] = get_address();
            meta['contract_signature'] = sign_hash(meta['contract_address'] , meta['contract_hash'])
            print(meta['contract_signature'])            



        #If there is metadata, then add to IPFS
        ipfs_hash = ''
        if (len(meta)):
            file = issue['asset'] + '.json'
            with open(file, 'w') as fp:
                json.dump(meta, fp, sort_keys=True, indent=2)
            ipfs_hash = add_to_ipfs(file)
            print(issue['asset'] + " IPFS Hash:" + ipfs_hash)
        else:
            print(issue['asset'] + " No metadata")

        issue_asset(issue['asset'], issue['qty'], issue['units'], issue['reissuable'], meta.get('contract_address', ''), ipfs_hash)


    


