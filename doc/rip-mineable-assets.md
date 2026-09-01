# RIP: Mineable Assets (G-PoW accrual)

Status: Draft (feature branch `feature/mineable-assets` on 2miners/Ravencoin v4.8.0)

Activation: BIP9 deployment `mineable_assets` (bit 12). Intended to activate after
consensus stabilizes (post-4.8.0) and alongside future work such as `p2sh-protocol-update`.

## Summary

Mineable assets (`&NAME`) are minted through optional coinbase claims. Each asset keeps
one accrual bucket: unclaimed schedule periods increase the claimable amount until a
miner includes a full-balance claim in a block.

## Accrual

Per active schedule:

- `total_periods = total_qty / per_block`
- `matured_periods = min(total_periods, (height - start_height) / nth_block)` when `height > start_height`
- `accrued_amount = (matured_periods - claimed_periods) * per_block`, capped by remaining supply
- `accrued_fund = (matured_periods - claimed_periods) * fund_amt`

## Claim rules

- Any block may claim any asset with `accrued_amount > 0` (delayed claim allowed).
- Claim must mint the **full** accrued balance (miner + optional fund outputs).
- At most one claim per mineable asset per block.
- Block weight is the only limit on claims per block.

## Issuance

`issuemineable` registers a schedule. Issuer must hold the root asset ownership token.
Cost: `max(500 RVN, total_periods * 1 RVN)`. A new schedule for the same root asset
preempts the prior schedule and resets accrual.

## Coinbase

When `mineable_assets` is active, coinbase may include `&` asset transfer outputs that
match validated accrual. The prior coinbase asset ban still applies to non-mineable assets.
