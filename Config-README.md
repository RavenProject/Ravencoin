Optimizing Your Node for Maximum Performance and Efficiency
-------------------

#### *RPC and Network Setting need to be added to raven.conf file*

#### Config file found here:
- **Windows**: C:\Users\<YourUsername>\AppData\Roaming\Raven\raven.conf
- **Linux**: /home/<your-username>/.raven/raven.conf
- **Mac**: /Users/<your-username>/Library/Application Support/Raven/raven.conf
- **In Wallet**: Click "Wallet" in the menu at the top, then click "Options", then click "Open Configuration File"

## List of RPC and Network Settings:

### Standard:

- **rpcuser**: Username required for RPC authentication  
  - Example: `rpcuser=<your_rpc_username>`

- **rpcpassword**: Password required for RPC authentication  
  - Example: `rpcpassword=<your_secure_rpc_password>`

- **rpcallowip**: Allow RPC requests only from localhost  
  - Example: `rpcallowip=127.0.0.1`

- **rpcport**: RPC port (default is 8766)  
  - Example: `rpcport=8766`

- **port**: Port for incoming P2P connections (default is 8767)  
  - Example: `port=8767`

- **listen**: Enable listening for inbound P2P connections (1 = enabled)  
  - Example: `listen=1`

- **server**: Enable RPC server to accept commands (1 = enabled)  
  - Example: `server=1`

- **daemon**: Run as a background process (1 = enabled)  
  - Example: `daemon=1`

- **txindex**: Maintain a full transaction index (1 = enabled)  
  - Example: `txindex=1`

- **logtimestamps**: Include timestamps in the debug log (1 = enabled)  
  - Example: `logtimestamps=1`


### Performance:

- **dbcache**: Size of database cache in MB (higher uses more RAM)  
  - Example: `dbcache=20000`

- **coindbcache**: Size of the block database cache in MB (higher uses more RAM)  
  - Example: `coindbcache=20000`

- **maxmempool**: Maximum size of memory pool in MB (higher allows more unconfirmed transactions)  
  - Example: `maxmempool=10000`

- **mempoolexpiry**: Time (in hours) to keep unconfirmed transactions in memory pool before expiring  
  - Example: `mempoolexpiry=72`

- **maxconnections**: Maximum number of inbound and outbound P2P connections  
  - Example: `maxconnections=40`

- **maxuploadtarget**: Maximum upload target in MB per day (limits outbound data)  
  - Example: `maxuploadtarget=50000`

- **blockminsize**: Minimum size of blocks in bytes  
  - Example: `blockminsize=100000`

- **blockmaxsize**: Maximum size of blocks in bytes  
  - Example: `blockmaxsize=2000000`

- **bloomfiltersize**: Size of the bloom filter for transaction indexing  
  - Example: `bloomfiltersize=30`



