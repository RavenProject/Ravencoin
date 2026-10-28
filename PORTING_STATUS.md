# RIP-25 Core 4.8.1 candidate status

The current native-RVN and asset PQ candidate is maintained in [ALENOC/Ravencoin PR #14](https://github.com/ALENOC/Ravencoin/pull/14). [RavenProject PR #1330](https://github.com/RavenProject/Ravencoin/pull/1330) proposes the code against official master. The original [RavenProject PR #1281](https://github.com/RavenProject/Ravencoin/pull/1281) remains a historical native-RVN proposal with its published branch unchanged.

The candidate preserves the Core 4.8.0 security baseline, including the KAWPOW header-height check and the transfer overflow rule on version bit 11. Native-RVN ML-DSA-44 signaling uses bit 12. The separate asset and owner-token rule uses bit 13 after its prerequisites. New protected asset outputs require a matching ML-DSA-signed native RVN witness-v2 input when spent; historical outputs remain classically spendable until voluntarily migrated.

Bit 12 raises the block-weight limit from 8 to 12 MWU at activation with a PQ witness discount, then to 16 MWU after a nominal year of blocks. This needs a coordinated validator upgrade because Core 4.8.0 can reject larger blocks. The exact build and test status, known limitations, and all finding dispositions are in [the RIP-25 v4.8 security remediation register](doc/RIP-0025-v4.8-security-remediation-register.md).

FINDING-067 remains HIGH and OPEN for operator readiness, the bit-12 go/no-go decision, and final 4.8.1 release steps. An unsigned RC pre-release is for controlled evaluation. Native Windows and macOS GUI testing is assigned to community testing after publication.
