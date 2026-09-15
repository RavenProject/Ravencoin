## Tools for Asset Issuance

### Bulk Issuance
Issue assets from a .csv file.
* Make a copy of https://docs.google.com/spreadsheets/d/1Ym88-ggbw8yiMgVxOtVYDsCXJGNGZqlpOfgdbVK8iYU
* Edit your own data
* Download as .csv and put in this folder.
* Set `RAVEN_RPC_USER` and `RAVEN_RPC_PASSWORD` in the environment before running the tool.
* ```python issuebulk.py```

### Signed Promises
Check for assets that have signed documents.
* Set `RAVEN_RPC_USER` and `RAVEN_RPC_PASSWORD` in the environment before running the tool.
* ```python signed_promises.py```

### Block Facts
Loops through blocks and prints out block information.
* Set `RAVEN_RPC_USER` and `RAVEN_RPC_PASSWORD` in the environment before running the tool.
* ```python blockfacts.py```

### Transaction Facts
Loops through blocks and transactions and prints out tx information.
* Uncomment out print lines to print out more facts
* Set `RAVEN_RPC_USER` and `RAVEN_RPC_PASSWORD` in the environment before running the tool.
* ```python txfacts.py```

### IPFS Pinner
Loops through blocks and transactions and pins asset issuance meta-data and then monitors ravend transactions for new ipfs metadata via zmq.
* Requires ipfs daemon to be running ```ipfs daemon```
* Set `RAVEN_RPC_USER` and `RAVEN_RPC_PASSWORD` in the environment before running the tool.

* Install pip3 (if not there) ```sudo apt-get install python3-pip```

* Install zmq with ```pip3 install pyzmq```

* Install bitcoinrpc with ```pip3 install python-bitcoinrpc```

* Install ipfsapi with ```pip3 install ipfsapi```

* Run ravend or raven-qt with parameter to use zmq: ```-zmqpubrawtx=tcp://127.0.0.1:28766```

Optional Arguments
```  
  -h, --help            show this help message and exit
  -n, --noblockscan     Do not scan though blocks.
  -z, --nozmqwatch      Do not watch zero message queue.
  -s, --safemode        Only store JSON files of limited size.
  -b BLOCK, --block BLOCK
                        Start at this block number.
  -f FOLDER, --folder FOLDER
                        Store files in a different folder.
  -d, --debug           Print debug info.
 ```
Run: ```python ipfs_pinner.py```

