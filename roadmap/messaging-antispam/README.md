# Messaging - Anti-spam

Ravencoin will display messages for all tokens that you own.  But wait, what if I get an unsolicited HERPES token because they found my crypto address on-chain?  Maybe I don’t want messages from the HERPES token issuer.

The holder of the owner token, e.g. ATOKEN! can send messages to holders their token ATOKEN.   The owner token has an exclamation point.  The owner token holder can also create channel tokens like ATOKEN\~News, or ATOKEN\~VoteInfo, or ATOKEN~Emergency   These are message channel tokens and the holder of these tokens can broadcast a message by attaching a special file (IPFS) to the transaction while sending the message channel token to themselves.

By default, if you own a token, you will get all the messages that are sent by channel owners, and the token owner.   Message channel tokens can be easily recognized because they contain a tilde ( ~ ) character.  No other tokens can contain a tilde character.

The Problem:
There is a problem with building a messaging system.  Perhaps one that the early designers of the e-mail system didn’t anticipate, and that's spam.  Spam is just an unwanted message.  Ravencoin users don’t want to get spam messages.   

Messages for tokens that you bought, or acquired through your actions are wanted.  If that isn’t true, then just turn off the channel for that token, and Ravencoin will stop showing messages for that token.

The solution:
Messages for tokens that you didn’t acquire, but were just sent to you unsolicited are probably spam.

The best practice is to provide a brand new, never used address when you receive new tokens.  If you strictly adhere to this practice, then you’ll get messages for tokens you bought or requested, and never get messages for tokens that were sent to you unsolicited.

The only reason spam is possible is that your crypto addresses are public, even if nobody knows the address belongs to you.

Here are the simple steps that Ravencoin takes to prevent spam.
* A channel that you choose to mute will be silenced.  Maybe you don’t want messages from the token owner or their designated channel token holders.
* A token that is sent to an address that has already been used will be considered as potentially spam and all of its channel silenced.  It may not be, but it will be treated as such.  Why?  Because your address is public and someone can send a SPAM token or a SPAM/SUBSPAM token to it.  Tokens can be created in bulk for very little cost, and then sent to everyone at very little cost.  The spam prevention system is to preemptively prevent this type of spam behavior.

The technical:
* Every token sent to an address that hasn’t ever been used will allow messages to be sent by the token owner token, and its channel tokens.
* If a token is sent to an address that has been used before, then the message channel will be muted (turned off).  You can always turn it on if you wish to receive messages for the new token you’ve been sent.
* If a message channel is added to a token that was sent to an already used address, then it will also be muted because the token channel was already muted.  In other words, it will inherit the state of the primary channel.

Testing:
* Send a token to a newly never seen address.  Send a message using the owner token.  The message should be seen.
* Send a token to a newly never seen address.  Create a channel token.  Send a message using the new channel token.  The message should be seen.
* Send a token to an address that already has a token in it.  Send a message using the owner token.  The message should not be seen.
* Send a token to an address that already has a token in it.  Create a channel token.  Send a message using the new channel token.  The message should not be seen.
