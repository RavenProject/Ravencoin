# RIP-25 Core 4.8 staging audit archive

This file recorded the initial review of the RIP-25 port to the Core 4.8.0 security baseline. Its former in-progress table and pre-merge blockers are historical. The current status source is [the RIP-25 v4.8 security remediation register](doc/RIP-0025-v4.8-security-remediation-register.md), which records the findings, remedies, test evidence, and remaining gates.

The 4.8.1 candidate is maintained in [ALENOC/Ravencoin PR #14](https://github.com/ALENOC/Ravencoin/pull/14). [RavenProject PR #1330](https://github.com/RavenProject/Ravencoin/pull/1330) proposes the current code against official master. [The original PR #1281](https://github.com/RavenProject/Ravencoin/pull/1281) retains its historical native-RVN branch.

The port preserves the Core 4.8.0 header-height and asset-transfer overflow protections, assigns transfer overflow to version bit 11 and native-RVN PQ to bit 12, and adds a separate bit-13 rule for new asset and owner-token outputs. Native witness-v2 RVN spends use ML-DSA-44. After bit 13 activates, protected asset spends require a matching native RVN witness-v2 input signed with ML-DSA-44 and no ECDSA signature on the asset input. Historical outputs retain their original spending rules until their owners migrate voluntarily.

Bit 12 also changes the block-weight limit from 8 to 12 MWU at activation with a PQ witness discount, then to 16 MWU after a nominal year of blocks. Core 4.8.0 validators can reject blocks accepted under that activated rule. Miner signaling therefore requires a coordinated validator and service upgrade.

FINDING-067 remains HIGH and OPEN for operator readiness, the bit-12 go/no-go decision, and final 4.8.1 release steps. Exact-SHA CI and package evidence for any candidate is recorded in the register and the corresponding pull request. Native Windows and macOS GUI testing is assigned to community testing after publication. An unsigned RC pre-release is for evaluation and does not approve network activation or a final release.
