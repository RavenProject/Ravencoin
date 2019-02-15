#!/usr/bin/env python3
# Script to audit the assets
# Reads the asset (amount has all issuances)
# Reads the balances in every address for the asset.
# Compares the two numbers to checks that qty of all assets are accounted for

import subprocess
import json


#Set this to your raven-cli program
cli = "raven-cli"

mode = "-testnet"
rpc_port = 18766
#mode =  "-regtest"
#rpc_port = 18443

#Set this information in your raven.conf file (in datadir, not testnet3)
rpc_user = 'rpcuser'
rpc_pass = 'rpcpass555'


def listassets(filter):
    rpc_connection = get_rpc_connection()
    result = rpc_connection.listassets(filter, True)
    return(result)


def listaddressesbyasset(asset, bool, number, number2):
    rpc_connection = get_rpc_connection()
    result = rpc_connection.listaddressesbyasset(asset, bool, number, number2)
    return(result)


def rpc_call(params):
    process = subprocess.Popen([cli, mode, params], stdout=subprocess.PIPE)
    out, err = process.communicate()
    return(out)


def generate_blocks(n):
    rpc_connection = get_rpc_connection()
    hashes = rpc_connection.generate(n)
    return(hashes)


def get_rpc_connection():
    from bitcoinrpc.authproxy import AuthServiceProxy, JSONRPCException
    connection = "http://%s:%s@127.0.0.1:%s"%(rpc_user, rpc_pass, rpc_port)
    #print("Connection: " + connection)
    rpc_connection = AuthServiceProxy(connection)
    return(rpc_connection)


def audit(filter):
    assets = listassets(filter)
    print("Auditing: " + filter)
    #print(assets)
    print("Asset count: " + str(len(assets)))
    count = 0  
    max_dist_asset_name = ""
    max_dist_address_count = 0
    for asset, properties in assets.items():
        count=count+1
        total_issued = 0
        total_for_asset = 0

        print("Auditing asset (" + str(count) + "): " + asset)
        for key, value in properties.items():
            if (key == 'amount'):
                total_issued += value
                print("Total issued for " + asset + " is: " + str(value))

        loop = True
        loop_count = 0
        number_of_addresses = 0
        while loop:
            # This call returns a max of 50000 items at a time
            address_qtys = listaddressesbyasset(asset, False, 50000, loop_count * 50000)
            number_of_addresses += len(address_qtys)

            for address, qty in address_qtys.items():
                #print(address + " -> " + str(qty))
                total_for_asset += qty

            # If the number of address is less than 50000, end the loop
            if len(address_qtys) < 50000:
                loop = False

            loop_count += 1

        print("Total in addresses for asset " + asset + " is " + str(total_for_asset))

        # Calculate stats
        if number_of_addresses > max_dist_address_count:
            max_dist_asset_name = asset
            max_dist_address_count = number_of_addresses

        if (total_issued == total_for_asset):
            print("Audit PASSED for " + asset)
            print("")
        else:
            print("Audit FAILED for " + asset)
            exit()

        if len(assets) == count:
            print("All " + str(len(assets)) + " assets audited.")
            print("Stats:")
            print("  Max Distribed Asset: " + max_dist_asset_name + " with " + str(max_dist_address_count) + " addresses.")


if mode == "-regtest":  #If regtest then mine our own blocks
    import os
    os.system(cli + " " + mode + " generate 400")

audit("*")  #Set to "*" for all.
