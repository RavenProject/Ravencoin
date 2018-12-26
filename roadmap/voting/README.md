# Voting

Voting for Ravencoin is a protocol level solution, which allows UTXOs that expire.  

The advantage to expiring UTXOs is that they do not need to be held in memory after the expiration because the right to vote has expired, and therefore the expired votes are worthless.

Layered on top of the voting is:
* Vote description - built on top of messaging.
* Vote ballot specification - JSON spec that defines vote addresses for each vote option.
* Vote distribution - built on top of rewards.
* Vote tracking.
* Voting with RPC calls.
* Voting in UI.

## Voting - protocol layer

Creating a vote will create vote tokens.  These vote tokens are identical to normally issued tokens, except that they are created in exact qty and units as an already issued token.

## RPC

```issuevote BASETOKEN NAME ipfs_hash lastblocktovote [optional exempt address list]```

Creates a BASETOKEN^NAME token in exactly the same qty and units as BASETOKEN.  IPFS hash is the meta-data message that defines what the vote is for - see vote message spec.  The lastblocktovote is a block height which is the last block a vote is allowed into.  After this block, the UTXOs for the un-voted tokens can be expired and removed.  If left blank, lastblocktovote will be set to approximately 30 days or 43200 blocks from current block height.  Exempt tokens are immediately burned at the same time that the vote tokens are distributed to the BASETOKEN holders.

Vote tokens will be distributed in exact amounts to holders of BASETOKEN.

## Vote message specification (in IPFS)
```
{
"subject":"Vote for membership expansion",
"message": "Members of our community have requested that we increase membership.  To this end, we are holding a vote.  You have until Thursday to register your vote.",
"ballot": { "RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H":"Yes, expand membership.",
            "RCqsnXo2Uc1tfNxwnFzkTYXfjKP21VX5ZD":"No, do not expand membership."
          }
}
```

Voting addresses may be set to vanity burn addresses like:
```
RVoteYesXXXXXXXXXXXXXXXXXXXXbDU8Rx
RVoteNoXXXXXXXXXXXXXXXXXXXXXWdk9zp
RAddMembershipXXXXXXXXXXXXXXWSzhgw
RYesToFranchiseXXXXXXXXXXXXXamSLQv
RAbstainXXXXXXXXXXXXXXXXXXXXaHW7mE
```

## Vote Token Creation in UI
The UI and RPC would let only the token owner create a vote.  Votes must be created in exact proportion to the issued tokens.  If exempt addresses are specified, then those tokens are immediately sent to a vote Burn address.

## Voting in UI
The presence of a vote token allows the UI to present a voting interface that presents the ballot options and sends the vote tokens to the correct voting address.

For simplification, all vote tokens will be voted for the selected option when using the UI.

The normal transfer RPC call would allow vote splitting by transferring some votes to one address and other votes to another address.

## Vote tracking
Vote tracking adds up the votes in the addresses that match those in the ballot and presents the results.




