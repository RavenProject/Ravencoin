#!/usr/bin/env python3

# Install pip3 (if not there)
#   sudo apt-get install python3-pip

# Install zmq with 
#   pip3 install pyzmq

# Install bitcoinrpc with 
#   pip3 install python-bitcoinrpc

# Install ipfsapi with 
#   pip3 install ipfsapi

import sys
import argparse
import zmq
import struct
import binascii
import codecs
import random
import os
import subprocess
import json
import signal  #Used for timeout

JSON_ONLY_CHECK = False
FILESIZE_THRESHOLD = 100000000

#Set this to your raven-cli program
cli = "raven-cli"

#mode = "-testnet"
mode = ""
rpc_port = 8766
#Set this information in your raven.conf file (in datadir, not testnet3)
rpc_user = 'rpcuser'
rpc_pass = 'rpcpass555'

def print_debug(str):
	if args.debug:
		print(str)

class timeout:
    def __init__(self, seconds=1, error_message='Timeout'):
        self.seconds = seconds
        self.error_message = error_message
    def handle_timeout(self, signum, frame):
        raise TimeoutError(self.error_message)
    def __enter__(self):
        signal.signal(signal.SIGALRM, self.handle_timeout)
        signal.alarm(self.seconds)
    def __exit__(self, type, value, traceback):
        signal.alarm(0)

def get_rpc_connection():
    from bitcoinrpc.authproxy import AuthServiceProxy, JSONRPCException
    connection = "http://%s:%s@127.0.0.1:%s"%(rpc_user, rpc_pass, rpc_port)
    rpc_conn = AuthServiceProxy(connection)
    return(rpc_conn)

rpc_connection = get_rpc_connection()

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

# def decode_rawtx(txdata):
#     print("decoding: " + txdata)
#     txjson = rpc_connection.decoderawtransaction(txdata)
#     return(txjson)    

def decode_rawtx(txdata):
    #print("decoding: " + txdata)
    txjson =  rpc_connection.decoderawtransaction(txdata)

    return(txjson)

# def decode_script(script):
#     scriptinfo = get_rpc_connection.decodescript(script)
#     return(scriptinfo)

def decode_rawtx_cli(txdata):
    txjson_str = rpc_call('decoderawtransaction', txdata)
    return(json.loads(txjson_str))

def decode_script(script):
    scriptinfo_str = rpc_call('decodescript', script)
    scriptinfo_str = scriptinfo_str.decode('ascii')    #Needed for Python version 3.5 compat.  3.6 works fine without it.
    return(json.loads(scriptinfo_str))


def rpc_call(command, params):
	# return(subprocess.check_output, [cli, mode, command, params])
    print_debug('cli: ' + cli)
    print_debug('command: ' + command)
    print_debug('params: ' + params)

    if len(params) > 131070:
        print_debug("Params too long for command line")
        print_debug('Len: ' + str(len(params)))
        return("")

    process = subprocess.Popen([cli, command, params], stdout=subprocess.PIPE)
    out, err = process.communicate()
    process.stdout.close()
    if process.stderr:
    	process.stderr.close()
    #print(out)
    return(out)

def is_json_only(txt):
	if args.debug:
		print("Validating JSON")
	txt = txt.strip()
	if txt[1:] != '{':
		print("Not proper JSON - No leading {")
		return False
	if txt[-1] != '}':
		print("Not proper JSON - No trailing }")
		return False
	try:
		json.loads(txt)
	except ValueError as e:
		print('Invalid json: %s' % e)
		return False 
	return(True)



def asset_handler(asset_script):
	global FILESIZE_THRESHOLD
	global args
	asset_file = asset_to_file(asset_script.get('asset_name'))
	if args.debug:
		print("Type: " + asset_script.get('type'))
		print("Asset: " + asset_script.get('asset_name'))
		print("Asset File: " + asset_file)
		print(asset_script.get('amount'))
		print(asset_script.get('units'))
		print("Reissuable: " + str(asset_script.get('reissuable')))
		print("Has IPFS: " + str(asset_script.get('hasIPFS')))
	if asset_script.get('hasIPFS') == True:
		ipfs_hash = asset_script.get('ipfs_hash')
		print_debug(ipfs_hash)
		size = FILESIZE_THRESHOLD + 1
		with timeout(seconds=15):
			try:
				size = check_ipfs_file_size(ipfs_hash)
			except:
				print("Couldn't get size - skipping: " + asset_script.get('asset_name'))
				size = FILESIZE_THRESHOLD + 1
		#size = check_ipfs_file_size(asset_script.get('ipfs_hash'))
		#size=1
		full_path_with_asset = asset_file + "=" + ipfs_hash
		if not args.folder == None:
			full_path_with_asset = add_sep(args.folder) + full_path_with_asset
		if (size <= FILESIZE_THRESHOLD):
			with timeout(seconds=20):
				try:
					if not os.path.isfile(full_path_with_asset):
						if JSON_ONLY_CHECK:
							a_str = ipfs_cat(ipfs_hash)
							if not is_json_only(a_str):
								return(None)

						atuple = ipfs_get(ipfs_hash)
						ipfs_pin_add(ipfs_hash)
						os.rename(ipfs_hash, full_path_with_asset)
						if args.debug:
							print('Saved file as: ' + full_path_with_asset)
					else:
						if args.debug:
							print("Found: " + full_path_with_asset)
				except:
					print("Unable to fetch IPFS file for asset: " + asset_script.get('asset_name'))
		else:
			print_debug("Failed to get " + ipfs_hash + ' via ipfs get <hash>  Trying http...')
			result = get_ipfs_file_wget(full_path_with_asset, ipfs_hash)
			if not result == 1:
				print("Unable to get file for asset " + asset_file)
			output_missing(full_path_with_asset + '.MISSING')
			#print("Too large at %d bytes" % size)


def output_missing(file):
	outf = open(file, 'w')
	outf.write("MISSING")
	outf.close()	

def get_ipfs_file_wget(filename, hash):
    try:
        import urllib.request as urllib2
    except ImportError:
        import urllib2


    print("Downloading: " + hash + " as " + filename)
    try:
        filedata = urllib2.urlopen('https://ipfs.io/ipfs/' + hash, timeout=20)  
        datatowrite = filedata.read()

        datatowrite.strip()
        if (datatowrite[0] != '{'):
            print("Not a valid metadata file")
            return


        with open(filename, 'wb') as f:  
            f.write(datatowrite)
        print("Saving metadata file")
    except urllib2.URLError as e:
        print(type(e))
        return 0
    except:
        print("Uncaught error while downloading")    #not catch
        return 0

    return 1



#Converts Asset to valid filename
def asset_to_file(asset):
	file = asset
	file = file.replace('/', r'%2F')
	file = file.replace('*', r'%2A')
	file = file.replace('&', r'%26')
	file = file.replace('?', r'%3F')
	file = file.replace(':', r'%3A')
	file = file.replace('=', r'%3D')
	return(file)

#Converts valid filename back to asset name
def file_to_asset(file):
	asset = file
	asset = asset.replace(r'%2F', '/')
	asset = asset.replace(r'%2A', '*')
	asset = asset.replace(r'%26', '&')
	asset = asset.replace(r'%3F', '?')
	asset = asset.replace(r'%3A', ':')
	asset = asset.replace(r'%3D', '=')
	return(asset)	

def check_ipfs_file_size(hash):
    #print("Checking size in IPFS")
    import ipfsapi
    api = ipfsapi.connect('127.0.0.1', 5001)
    res = api.object_stat(hash)
    #print(res)
    return(res['CumulativeSize'])

def ipfs_add(file):
    print("Adding to IPFS")
    import ipfsapi
    api = ipfsapi.connect('127.0.0.1', 5001)
    res = api.add(file)
    if args.debug:
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

def ipfs_repo_stat():
    import ipfsapi
    api = ipfsapi.connect('127.0.0.1', 5001)
    res = api.repo_stat()
    if args.debug:
    	print(res)
    return(res)

def ipfs_pin_ls():
    import ipfsapi
    api = ipfsapi.connect('127.0.0.1', 5001)
    res = api.pin_ls()
    print(res)
    return(res)

def block_conf_filename():
	return('saveblock.conf')

#Add OS specific folder separator
def add_sep(dir):
	if (dir[-1] != os.sep):
		dir = dir + os.sep
	return(dir)

def load_block():
	print_debug('reading block')
	FIRST_ASSET_BLOCK = 435456

	#If we passed in an argument for the first block
	if args.block != None and args.block >= FIRST_ASSET_BLOCK:
		return(args.block)

	#Read from the config file for last blocks processed
	if os.path.isfile(block_conf_filename()):  
		outf = open(block_conf_filename(), 'r')
		saved_block = int(outf.read())
		outf.close()
		if saved_block > FIRST_ASSET_BLOCK:
				return(saved_block)

	#Return first block that could contain assets
	return(FIRST_ASSET_BLOCK)

def save_block(block_num):
	outf = open(block_conf_filename(), 'w')
	outf.write(str(block_num))
	outf.close()

def scan_asset_blocks():
	#Get the blockheight of the chain
	blockheight = get_bci().get('blocks')
	start_block = load_block()
	print_debug("Starting at block: " + str(start_block))

	for i in range(start_block,blockheight):
	    dta = get_blockinfo(i)
	    print('Block #' + str(i) + " - " + dta.get('hash'))
	    tx_in_block = get_block(dta.get('hash'))
	    txs = tx_in_block.get('tx')
	    print_debug(txs)
	    for tx in txs:
	        tx_info = get_rawtx(tx)
	        print_debug("txinfo: " + tx_info)
	        tx_detail = decode_rawtx(tx_info)
	        for vout in tx_detail.get('vout'):
	            if (vout.get('scriptPubKey').get('asm')[86:98] == "OP_RVN_ASSET"):
	                print_debug("Found OP_RVN_ASSET")
	                print_debug(vout.get('scriptPubKey').get('hex'))
	                asset_script = decode_script(vout.get('scriptPubKey').get('hex'))
	                asset_handler(asset_script)
	                save_block(i)
	                print_debug(asset_script)


def monitor_zmq():
	#  Socket to talk to server
	context = zmq.Context()
	socket = context.socket(zmq.SUB)

	print("Getting Ravencoin msgs")
	socket.connect("tcp://localhost:28766")

	#socket.setsockopt_string(zmq.SUBSCRIBE, u'hashtx')
	#socket.setsockopt_string(zmq.SUBSCRIBE, u'hashblock')
	#socket.setsockopt_string(zmq.SUBSCRIBE, u'rawblock')
	socket.setsockopt_string(zmq.SUBSCRIBE, u'rawtx')

	while True:
		msg = socket.recv_multipart()
		topic = msg[0]
		body = msg[1]
		sequence = "Unknown"
		if len(msg[-1]) == 4:
			msgSequence = struct.unpack('<I', msg[-1])[-1]
			sequence = str(msgSequence)
		if topic == b"hashblock":
			print('- HASH BLOCK ('+sequence+') -')
			print(binascii.hexlify(body))
		elif topic == b"hashtx":
			print('- HASH TX  ('+sequence+') -')
			print(binascii.hexlify(body))
		elif topic == b"rawblock":
			print('- RAW BLOCK HEADER ('+sequence+') -')
			print(binascii.hexlify(body[:80]))
		elif topic == b"rawtx":
			print('ZMQ - RAW TX - Sequence: ' + sequence)
			if args.debug:
				print('- RAW TX ('+sequence+') -')
			tx_info = binascii.hexlify(body).decode("utf-8")
			#print('tx_info is ' + tx_info)
			if args.debug:
				print("txinfo: " + tx_info)
			tx_detail = decode_rawtx_cli(tx_info)
			for vout in tx_detail.get('vout'):
			#print("vout: " + str(vout.get('value')))
			#print(vout.get('scriptPubKey').get('asm'))
				if (vout.get('scriptPubKey').get('asm')[86:98] == "OP_RVN_ASSET"):
					#print("Found OP_RVN_ASSET")
					#print(vout.get('scriptPubKey').get('hex'))
					asset_script = decode_script(vout.get('scriptPubKey').get('hex'))
					asset_handler(asset_script)

#print(file_to_asset(asset_to_file('?*_/')))
#exit(0)

def main(argv):
	global args
	parser = argparse.ArgumentParser()
	parser.add_argument('-n', '--noblockscan', action='store_true', help='Do not scan though blocks.')
	parser.add_argument('-z', '--nozmqwatch', action='store_true', help='Do not watch zero message queue.')
	parser.add_argument('-s', '--safemode', action='store_true', help='Only store JSON files of limited size.')
	parser.add_argument('-b', '--block', type=int, help='Start at this block number.')
	parser.add_argument('-f', '--folder', type=str, help='Store files in a different folder.')
	parser.add_argument('-d', '--debug', action='store_true', help='Print debug info.')
	args = parser.parse_args()
	if args.debug:
		print(args)


	try:
		ipfs_repo_stat()  #Make sure IPFS is running
	except:
		print("pip3 install ipfs")
		print("OR")
		print("ipfs not running.  Run: ipfs daemon")
		exit(-1)


	if args.safemode:
		FILESIZE_THRESHOLD = 16000
		JSON_ONLY_CHECK = True


	#check_ipfs_file_size('QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E')
	if not args.noblockscan:
		scan_asset_blocks()
	if not args.nozmqwatch:
		monitor_zmq()

if __name__ == "__main__":
   main(sys.argv[1:])

