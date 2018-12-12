# HOWTO: Atomic Swaps

__Or, trading apples for bananas.__


## The Problem

_Andy has some apples.  Barb has some bananas.  Andy agrees to give Barb two apples in exchange for one banana.  In order to make sure both parties hold up their end of the deal they need to create a single transaction that sends assets both directions._

## The Inputs

We'll need to find 3 UTXOs to use as inputs.  Andy's apples, Barb's bananas and some Raven to pay the network fee (Andy will pay).

Andy uses `listunspent` to find a suitable `txid` and `vout`:

```
﻿{
  "txid": "631cf1566165803f0b89fbfb169d8f0c89129ec3f8536a48e4c4f0f3c4081cff",
  "vout": 0,
  "address": "mh8mmbCPqnxCNqJq547NG99pauTHqvvYjA",
  "scriptPubKey": "21027e1fde02d2cbfac3629aeaf669abd156d0c4dfbf52f6a5e4dd6664e81a621045ac",
  "amount": 4.88281250,
  "confirmations": 3088,
  "spendable": true,
  "solvable": true,
  "safe": true
}
```

Then each party uses `listmyassets` with `verbose=true` to find asset UTXOs:

Andy:
`listmyassets APPLES true`:

```
{
  "APPLES": {
    "balance": 1000,
    "outpoints": [
      {
        "txid": "744c61abe6d237939c3567bc44b912ff4c375984229908c964be44f36dec79e3",
        "vout": 3,
        "amount": 1000
      }
    ]
  }
}
```

Barb:
`listmyassets BANANAS true`:

```
{
  "BANANAS": {
    "balance": 1000,
    "outpoints": [
      {
        "txid": "8468ef5193b1f5d6a7bd501f5f8ef5aec7c3d86fa87cec5a0b6f6d86fba78a4f",
        "vout": 3,
        "amount": 1000
      }
    ]
  }
}
```

Extracting the txids and vouts gives us our raw inputs:
`'[{"txid":"631cf1566165803f0b89fbfb169d8f0c89129ec3f8536a48e4c4f0f3c4081cff","vout":0}, \
   {"txid":"744c61abe6d237939c3567bc44b912ff4c375984229908c964be44f36dec79e3","vout":3}, \
   {"txid":"8468ef5193b1f5d6a7bd501f5f8ef5aec7c3d86fa87cec5a0b6f6d86fba78a4f","vout":3}]'`

## The Outputs

We'll be using 5 new Ravencoin addresses:

Andy's Raven change address:
`mvGfeg4uZA8XvjVDUywdgYE6TAyz77o5gB`

Andy's banana receive address:
`msXQpCK8UexfgtMbUGwnKjDfE6vqJ4JUPF`

Barb's banana change address:
`mjyoMtEtoxw9edgzpNnEVTH7jqSqiQ529N`

Barb's apple receive address:
`mzPe6rUPYcbDbqnxRc5mcvKkASCAS9JBzL`

Andy's apple change address:
`mzct8GQ5zdaCvbrnRDrR8T87ZuZxkRYNwL`

All asset transfers have to be balanced.  Since we have 1000 APPLES coming in, we have to have 1000 going out.  So Andy will send 2 to Barb's receive address and the rest (998) to his change address.  The Raven will pay 0.0001 for the network fee as normal.

`'{"mvGfeg4uZA8XvjVDUywdgYE6TAyz77o5gB":4.8827125, \
  "msXQpCK8UexfgtMbUGwnKjDfE6vqJ4JUPF":{"transfer":{"BANANAS":1}}, \
  "mjyoMtEtoxw9edgzpNnEVTH7jqSqiQ529N":{"transfer":{"BANANAS":999}}, \
  "mzPe6rUPYcbDbqnxRc5mcvKkASCAS9JBzL":{"transfer":{"APPLES":2}}, \
  "mzct8GQ5zdaCvbrnRDrR8T87ZuZxkRYNwL":{"transfer":{"APPLES":998}}}'`

## Creating The Transaction

Andy can now use `createrawtransaction`, passing in the inputs and outputs and receiving the transaction hex:

```
createrawtransaction '[{"txid":"631cf1566165803f0b89fbfb169d8f0c89129ec3f8536a48e4c4f0f3c4081cff","vout":0},{"txid":"744c61abe6d237939c3567bc44b912ff4c375984229908c964be44f36dec79e3","vout":3},{"txid":"8468ef5193b1f5d6a7bd501f5f8ef5aec7c3d86fa87cec5a0b6f6d86fba78a4f","vout":3}]' '{"mvGfeg4uZA8XvjVDUywdgYE6TAyz77o5gB":4.8827125,"msXQpCK8UexfgtMbUGwnKjDfE6vqJ4JUPF":{"transfer":{"BANANAS":1}},"mjyoMtEtoxw9edgzpNnEVTH7jqSqiQ529N":{"transfer":{"BANANAS":999}},"mzPe6rUPYcbDbqnxRc5mcvKkASCAS9JBzL":{"transfer":{"APPLES":2}},"mzct8GQ5zdaCvbrnRDrR8T87ZuZxkRYNwL":{"transfer":{"APPLES":998}}}'
```

```
0200000003ff1c08c4f3f0c4e4486a53f8c39e12890c8f9d16fbfb890b3f80656156f11c630000000000ffffffffe379ec6df344be64c90899228459374cff12b944bc67359c9337d2e6ab614c740300000000ffffffff4f8aa7fb866d6f0b5aec7ca86fd8c3c7aef58e5f1f50bda7d6f5b19351ef68840300000000ffffffff05926d1a1d000000001976a914a1d62b4f8a6710ad43d56f656c27966513ae8fbf88ac00000000000000003076a91483b7a59ce25d8159fdc2ab70320512d2bc07a1c488acc01472766e740742414e414e415300e1f505000000007500000000000000003076a91430f446c2ac37dc072516603e44e86aea7af2bf6788acc01472766e740742414e414e415300078142170000007500000000000000002f76a914cf084a93cf87d3030b74a1939d9c8f02f6152c8c88acc01372766e74064150504c455300c2eb0b000000007500000000000000002f76a914d18967e005ff7208cc6a9c483b028c7c72ca2d6e88acc01372766e74064150504c455300268b3c170000007500000000
```

## Signing The Transaction

Ok, the structure of the transaction is set.  Now each party needs to sign it in order to unlock the inputs.  Here are the steps:

* Andy signs it using `signrawtransaction`.  This will alter the hex, using his wallet to insert the signatures.

> **Note:** Since Andy doesn't have the keys for all of the inputs he will see an error: `"Unable to sign input, invalid stack size (possibly missing key)" This is normal and expected.`

* Andy sends the signed hex to Barb.
* Barb uses `signrawtransaction` to sign the rest of the inputs.  Again capture the hex output.

## Submit The Transaction

Almost there.  Barb now passes the fully signed hex to `sendrawtransaction`!  It will be communicated to the network and put into the next block.
