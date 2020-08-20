#!/usr/bin/env python3
# Script to audit the assets
# Reads the asset (amount has all issuances)
# Reads the balances in every address for the asset.
# Compares the two numbers to checks that qty of all assets are accounted for#
#
# -- NOTE: 	This script requires Python, PIP, and bitcoin-rpc to be installed.
# 		To install bitcoin-rpc run the following command from the terminal:
#       pip install wheel
#				pip install python-bitcoinrpc
#
#     To use sendgid notification (optional):
#       pip3 install sendgrid
#       export SENDGRID_API_KEY="<your sendgrid API key>"
#       set notification_emails variable below

import os
import subprocess
import json
import logging


#Set this to your raven-cli program
cli = "raven-cli"

mode = "-main"
rpc_port = 8766

#mode = "-testnet"
#rpc_port = 18770
#mode =  "-regtest"
#rpc_port = 18444

#Set this information in your raven.conf file (in datadir, not testnet6)
rpc_user = 'rpcuser'
rpc_pass = 'rpcpass555'

#Set this e-mail address, and SENDGRID_API_KEY env variable for notifications
notification_emails='test@example.com'
send_alerts_on_success = True    #Set to True if you want email notification on success

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

def log_failure(err):
    logging.error(err)

def send_notification(notification_to_emails, notification_subject, notification_content):
    sendgrid_api_key = ''
    if "SENDGRID_API_KEY" in os.environ:  
      sendgrid_api_key = os.environ.get('SENDGRID_API_KEY')
      #print("Key="+sendgrid_api_key)
    else:
      print("Must set SENDGRID_API_KEY environment to use e-mail notification.")
      return

    import sendgrid
    from sendgrid.helpers.mail import Content, Email, Mail

    message = Mail(
    from_email='asset_audit_notifier@example.com',
    to_emails=notification_to_emails,
    subject=notification_subject,
    html_content=notification_content)
    
    try:
      sg = sendgrid.SendGridAPIClient(sendgrid_api_key)
      response = sg.send(message)
      #print(response.status_code)
      #print(response.body)
      #print(response.headers)
    except:
      print(e.message)      


def audit(filter):
    assets = listassets(filter)
    print("Auditing: " + filter)
    #print(assets)
    print("Asset count: " + str(len(assets)))
    count = 0  
    max_dist_asset_name = ""
    max_dist_address_count = 0
    audits_failed = 0
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
                #print(address + "," + str(qty))
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
            audits_failed += 1
            print("Audit FAILED for " + asset)
            msg = "Audit FAILED for " + asset + " Issued="+str(total_issued)+ " Total="+str(total_for_asset)
            log_failure(msg)
            send_notification(notification_emails, "Ravencoin Asset Audit Failed", msg)
            #exit(-1)

        if len(assets) == count:
            print("All " + str(len(assets)) + " assets audited.")
            print("Stats:")
            print("  Max Distribed Asset: " + max_dist_asset_name + " with " + str(max_dist_address_count) + " addresses.")
            if (send_alerts_on_success and audits_failed == 0):
              send_notification(notification_emails, "Ravencoin Asset Audit Success", "All " + str(len(assets)) + " assets audited.")

if mode == "-regtest":  #If regtest then mine our own blocks
    import os
    os.system(cli + " " + mode + " generate 400")

#### Uncomment these lines to test e-mail notification ###
#send_notification(notification_emails, "Test Subject", "Test Message")
#exit()
##########################################################

logging.basicConfig(filename='failures.txt', format="%(asctime)-15s %(message)s", level=logging.INFO)
audit("*")  #Set to "*" for all, or set a specific asset, or 'B*'
