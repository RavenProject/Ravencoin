# Rewards

Rewards, also sometimes called dividends, provides a way to send tokenized assets or RVN to token holders.  This can be used to reward shareholders with profits (denominated in RVN), or to reward membership holders, or to reward those that contributed the most to a shared project and earned special tokens.  

Rewards do not require a consensus protocol change, and the rpc calls exist to be able do rewards already.

These capabilities just make it native and easy-to-use from the client.

Example that rewards TRONCO holders with RVN:  
```reward 10000 RVN TRONCO```

## Reward calculation

First, the QTY of TARGET_TOKEN is calculated.  This is the total issuance, minus the qty held by the exception addresses.

Next, the reward calculation takes the qty to send.  This must be specified, and will usually be RVN.

PER_TOKEN_AMOUNT_IN_SATOSHIS = [QTY TO SEND_IN_SATOSHIS] / [QTY OF TARGET_TOKEN]

For RVN this must send an equal number of satoshis to every TARGET_TOKEN.  Remainder satoshis should be sent to the miners.

For a token, this must send an equal number of the token to every TARGET_TOKEN.  The calculation will need to factor in the units.  For example, if you attempted to send 7 non-divisible (units=0) of SEND_TOKEN to every holder of TARGET_TOKEN, but there were 8 or more TARGET_TOKEN holders, then the 'reward' call would fail because it is impossible to reward the TARGET_TOKEN holders equally.

Example: 10 RVN to 3 TRONCO holders.  10 * 100,000,000 sats = 1,000,000,000 RVN sats.
PER_TOKEN_AMOUNT_IN_SATOSHIS = 1,000,000,000 / 3 = 333333333.33333333 (repeating) per TRONCO holder.  The remainder of .3333 (repeating) satoshis per holder will not be sent as an output, and therefore will be given to the miners.  This is 1 sat once multiplied by the 3 TRONCO holders.  Each TRONCO holder will receive exactly 333333333 RVN sats.

One special case - Paying TRONCO to TRONCO.  This special case would require an exception address, and the source of the TRONCO would need to come from one or more of the exception addresses.
reward 400000 TRONCO TRONCO ['exception address']

### Rewards Components
#### Protocol

No protocol change needed.

#### GUI - Desktop

Select a token or RVN to send.
Set the QTY to send.
Select the target token
* If you are sending another token, you must have the owner token for the TARGET_TOKEN.
* If you are sending RVN, you do not need the owner token for the TARGET_TOKEN

GUI will show the exact amount being sent to each TARGET_TOKEN.  It will also calculate and show the remaining which must be returned as change.  If RVN is being sent, it will show the remainder that is being sent to the miners.

#### GUI - Mobile

Mobile will not initially have the rewards feature.

#### RPC

These rpc calls are added in support of rewards:

reward QTY [RVN|TOKEN] TARGET_TOKEN [exception address list]

#### Examples

Example 1 (simple):
```
4 holders of TRONCO (unit 0) (100 Issued)
RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H   10 TRONCO
RPsCVwsq8Uf2dcUSXcYPzVnsAMZtAHw6sV   20 TRONCO
RBp5woWDU8TRMz1TPeemyLxxLL3xsCnQgh   30 TRONCO
RFMD7ZJzexAmiLA9BHxwFCPVeiuAgdVjcP   40 TRONCO

reward 100 RVN TRONCO

Takes 100 RVN (10,000,000,000 sats)
RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H gets 10 RVN (10,000,000,000 RVN sats)
RPsCVwsq8Uf2dcUSXcYPzVnsAMZtAHw6sV gets 20 RVN (20,000,000,000 RVN sats)
RBp5woWDU8TRMz1TPeemyLxxLL3xsCnQgh gets 30 RVN (30,000,000,000 RVN sats)
RFMD7ZJzexAmiLA9BHxwFCPVeiuAgdVjcP gets 40 RVN (40,000,000,000 RVN sats)
```

Example 2 (simple with 2 exception addresses)
```
5 holders of TRONCO (unit 0) (10000 Issued)
RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H 9900 TRONCO (exception)
RCqsnXo2Uc1tfNxwnFzkTYXfjKP21VX5ZD 	 50 TRONCO (exception) 
RPsCVwsq8Uf2dcUSXcYPzVnsAMZtAHw6sV    5 TRONCO
RBp5woWDU8TRMz1TPeemyLxxLL3xsCnQgh   15 TRONCO
RFMD7ZJzexAmiLA9BHxwFCPVeiuAgdVjcP   30 TRONCO

reward 1000 RVN TRONCO ['RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H','RCqsnXo2Uc1tfNxwnFzkTYXfjKP21VX5ZD']

Takes 1000 RVN (100,000,000,000 sats)
RPsCVwsq8Uf2dcUSXcYPzVnsAMZtAHw6sV gets 100 RVN (10,000,000,000 RVN sats)
RBp5woWDU8TRMz1TPeemyLxxLL3xsCnQgh gets 300 RVN (30,000,000,000 RVN sats)
RFMD7ZJzexAmiLA9BHxwFCPVeiuAgdVjcP gets 600 RVN (60,000,000,000 RVN sats)
```

Example 3 (Payment of BLACKCO to TRONCO holders with 2 exception addresses)  
TRONCO and BLACKCO is set to units 0.  
Note: Can only make this call if you hold TRONCO! -- the ownership token.
```
5 holders of TRONCO (unit 0) (10000 Issued)
RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H 9900 TRONCO (exception)
RCqsnXo2Uc1tfNxwnFzkTYXfjKP21VX5ZD 	 50 TRONCO (exception) 
RPsCVwsq8Uf2dcUSXcYPzVnsAMZtAHw6sV    5 TRONCO
RBp5woWDU8TRMz1TPeemyLxxLL3xsCnQgh   15 TRONCO
RFMD7ZJzexAmiLA9BHxwFCPVeiuAgdVjcP   30 TRONCO

reward 1000 BLACKCO TRONCO ['RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H','RCqsnXo2Uc1tfNxwnFzkTYXfjKP21VX5ZD']

Takes 1000 BLACKCO and distributes them according to TRONCO holdings (minus holdings by exception addresses)
RPsCVwsq8Uf2dcUSXcYPzVnsAMZtAHw6sV gets 100 BLACKCO
RBp5woWDU8TRMz1TPeemyLxxLL3xsCnQgh gets 300 BLACKCO
RFMD7ZJzexAmiLA9BHxwFCPVeiuAgdVjcP gets 600 BLACKCO
```

Example 4 (Payment of DOLLARTOKEN to TRONCO holders with 1 exception address)  
DOLLARTOKEN - a fictional stablecoin is set to units 2.  
Note: Can only make this call if you hold TRONCO! -- the ownership token.
```
5 holders of TRONCO (unit 0) (10000 Issued)
RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H 9900 TRONCO (exception)
RCqsnXo2Uc1tfNxwnFzkTYXfjKP21VX5ZD 	 50 TRONCO
RPsCVwsq8Uf2dcUSXcYPzVnsAMZtAHw6sV    5 TRONCO
RBp5woWDU8TRMz1TPeemyLxxLL3xsCnQgh   15 TRONCO
RFMD7ZJzexAmiLA9BHxwFCPVeiuAgdVjcP   30 TRONCO

reward 1000.00 DOLLARTOKEN TRONCO ['RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H']

Takes 1000.00 DOLLARTOKEN and distributes them according to TRONCO holdings (minus holdings by exception addresses)
RCqsnXo2Uc1tfNxwnFzkTYXfjKP21VX5ZD gets 500.00 DOLLARTOKEN
RPsCVwsq8Uf2dcUSXcYPzVnsAMZtAHw6sV gets  50.00 DOLLARTOKEN
RBp5woWDU8TRMz1TPeemyLxxLL3xsCnQgh gets 150.00 DOLLARTOKEN
RFMD7ZJzexAmiLA9BHxwFCPVeiuAgdVjcP gets 300.00 DOLLARTOKEN
```

Example 5 (Payment of INDIVISIBLE to TRONCO holders)  
INDIVISIBLE - a token with units set to 0 (therefore indivisible).  
Note: Can only make this call if you hold TRONCO! -- the ownership token.  
Note: Failure occurs because of indivisibility of INDIVISIBLE token.  Unable to reward equally.
```
5 holders of TRONCO (unit 0) (10000 Issued)
RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H 9900 TRONCO
RCqsnXo2Uc1tfNxwnFzkTYXfjKP21VX5ZD 	 50 TRONCO
RPsCVwsq8Uf2dcUSXcYPzVnsAMZtAHw6sV    5 TRONCO
RBp5woWDU8TRMz1TPeemyLxxLL3xsCnQgh   15 TRONCO
RFMD7ZJzexAmiLA9BHxwFCPVeiuAgdVjcP   30 TRONCO

reward 9999 INDIVISIBLE TRONCO

Takes 9999 INDIVISIBLE and attempts to distribute equally according to TRONCO holdings

Results in FAILURE - Error "Unable to reward evenly"
```

Example 6 (Payment of INDIVISIBLE to TRONCO holders)  
INDIVISIBLE - a token with units set to 0 (therefore indivisible).  
Note: Can only make this call if you hold TRONCO! -- the ownership token.  
Note: Remainder is sent back to sending address.
```
5 holders of TRONCO (unit 0) (10000 Issued)
RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H 9900 TRONCO
RCqsnXo2Uc1tfNxwnFzkTYXfjKP21VX5ZD 	 50 TRONCO
RPsCVwsq8Uf2dcUSXcYPzVnsAMZtAHw6sV    5 TRONCO
RBp5woWDU8TRMz1TPeemyLxxLL3xsCnQgh   15 TRONCO
RFMD7ZJzexAmiLA9BHxwFCPVeiuAgdVjcP   30 TRONCO

reward 10001 INDIVISIBLE TRONCO

Takes 10001 INDIVISIBLE and attempts to distribute equally according to TRONCO holdings
RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H gets 9900 INDIVISIBLE
RCqsnXo2Uc1tfNxwnFzkTYXfjKP21VX5ZD gets   50 INDIVISIBLE
RPsCVwsq8Uf2dcUSXcYPzVnsAMZtAHw6sV gets    5 INDIVISIBLE
RBp5woWDU8TRMz1TPeemyLxxLL3xsCnQgh gets   15 INDIVISIBLE
RFMD7ZJzexAmiLA9BHxwFCPVeiuAgdVjcP gets   30 INDIVISIBLE
Remaining 1 INDIVISIBLE sent back to the first sending address.
```

Example 7 (Payment of VERYDIVISIBLE to TRONCO holders)  
VERYDIVISIBLE - a token with units set to 8 (therefore divisible to 8 decimal places).  
Note: Can only make this call if you hold TRONCO! -- the ownership token.  
Note: Remainder is sent back to sending address.  
```
2 holders of TRONCO (unit 0) (3 Issued)
RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H 1 TRONCO
RCqsnXo2Uc1tfNxwnFzkTYXfjKP21VX5ZD 2 TRONCO

reward 1 VERYDIVISIBLE TRONCO

Takes 1 VERYDIVISIBLE and attempts to distribute equally according to TRONCO holdings
RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H gets 0.33333333 VERYDIVISIBLE
RCqsnXo2Uc1tfNxwnFzkTYXfjKP21VX5ZD gets 0.66666666 VERYDIVISIBLE
Remaining 0.00000001 VERYDIVISIBLE sent back to the first sending address.
```






