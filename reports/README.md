# Implementation Reports

Detailed reports for the six cumulative database-engine milestones. The reports
cover implementation strategy, invariants, debugging findings, and notable edge
cases that are difficult to infer from the final code alone.

| Report | Component | Topics |
| --- | --- | --- |
| [Storage and buffer management](01-storage-and-buffer-management.pdf) | Milestone 1 | Slotted pages, record lifecycle, page chains, sequential scans, and LRU replacement |
| [Recovery and ARIES](02-recovery-and-aries.pdf) | Milestone 2 | Log chains, page LSNs, rollback, redo, dirty-page analysis, and crash recovery |
| [Transactions and concurrency](03-transactions-and-concurrency.pdf) | Milestone 3 | Record visibility, MVCC snapshots, Halloween prevention, isolation levels, and strict two-phase locking |
| [Query execution](04-query-execution.pdf) | Milestone 4 | Limit/offset, blocking sort, nested-loop join, and merge join |
| [Query optimization](05-query-optimization.pdf) | Milestone 5 | Predicate normalization, De Morgan rewrites, predicate pushdown, and greedy join ordering |
| [B+ tree indexing](06-bplus-tree-indexing.pdf) | Milestone 6 | Slotted B+ tree pages, bulk construction, routing keys, leaf links, search, and index scans |

> These reports contain personal identification and implementation details.
> Keep the repository private unless the relevant course and privacy requirements
> permit redistribution.
