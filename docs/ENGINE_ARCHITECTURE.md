# Engine Architecture

## System boundaries

The engine is organized as a conventional relational database pipeline with
durability and concurrency control spanning multiple layers:

1. SQL is parsed by the retained HuaDB front end.
2. The binder and catalog resolve schemas, functions, and expressions.
3. The planner creates logical and physical operator trees.
4. Optimization rewrites predicates, selects indexes, and changes join order.
5. Executors pull records through a Volcano-style iterator interface.
6. Tables and indexes access heap pages through the buffer pool.
7. The log manager and transaction manager provide recovery and isolation.

## Storage layer

Table pages use a slotted layout. A record is addressed by `(page_id, slot_id)`,
which becomes the physical row identifier used by indexes and execution
operators. Page chains allow a table scan to continue across multiple pages.

The implementation adds insert, delete, update, and sequential-scan behavior on
top of this layout. The LRU buffer strategy decides which unpinned frame to
evict and keeps the buffer pool's page metadata consistent during replacement.

Write operations mark pages dirty with the LSN of the corresponding log record.
This links physical page state back to recovery metadata.

## Recovery

The manager tracks:

- active transactions and their last LSN;
- dirty pages and their page LSN;
- the next LSN to assign;
- the latest LSN guaranteed to be on disk.

Each log record contains a previous-LSN pointer, which forms a per-transaction
undo chain. Rollback walks backward through that chain and applies compensation
logic. Checkpoints record active transactions and dirty pages.

Recovery runs in three phases:

1. **Analysis** reconstructs active transactions and dirty pages from the last
   checkpoint and later log records.
2. **Redo** restores committed or otherwise required page changes.
3. **Undo** rolls back transactions that did not complete.

## Transactions and locks

The transaction manager records an active transaction set and a command ID for
each transaction. Table scans evaluate record visibility using `xmin`, `xmax`,
the reader's snapshot, and the configured isolation level.

The lock manager supports shared, exclusive, intention-shared,
intention-exclusive, and shared-intention-exclusive modes at table and row
granularity. It implements:

- compatibility checks between lock modes;
- lock upgrades when a transaction requests stronger access;
- waiting and deadlock-handling parameters;
- release of all locks owned by a transaction.

## Execution

Execution operators share a pull-based `Next()` interface. The implemented
operators cover:

- **Limit:** returns a bounded prefix of child records.
- **Order By:** buffers child records and sorts them using evaluated
  expressions and column direction.
- **Nested-loop join:** compares each outer record with all matching inner
  records.
- **Merge join:** traverses sorted inputs while handling duplicate keys.

The planner and hash-join operator are part of the shared framework; the
portfolio work extends the execution surface around them.

## Optimization

The optimizer uses explicit, reviewable rewrites rather than a monolithic pass:

- comparison normalization moves constants to a consistent side;
- `NOT` over a comparison becomes the inverse comparison;
- predicates are split at `AND` boundaries;
- filter predicates are pushed to the nearest scan or join boundary;
- indexes are selected when predicates have a usable leading key;
- join trees are flattened, catalog cardinalities are combined, and a greedy
  low-cardinality tree is constructed.

The join-order implementation estimates equality selectivity from catalog
distinct counts and uses predicate coverage to break ties when choosing the
next table.

## B+ tree indexing

The index stores composite keys plus heap RIDs. Construction performs an
in-memory sort followed by a bottom-up page build:

- allocate and initialize the metadata page;
- create the first leaf;
- append sorted index entries and split to new pages when full;
- promote each new page's minimum key to the parent level;
- maintain previous/next leaf links;
- set the final root page after the rightmost path is attached.

Search routes through internal pages with composite-key comparisons, then scans
leaf pages for point or range predicates. The execution layer converts eligible
sequential scans into B+ tree scans and unions result RIDs when multiple index
paths are needed.

## Correctness focus

The implementation is tested around behavior rather than nominal SQL output
alone. The suite covers crash recovery, transaction visibility, lock ordering,
write skew, the Halloween anomaly, join execution, optimizer plans, and B+ tree
structure.

## Deliberate limits

- The project is single-node and does not implement replication or distributed
  query execution.
- The optimizer is intentionally compact and does not model I/O or CPU cost
  with production-grade detail.
- SQL parsing and several framework components come from upstream HuaDB.
- The repository is an educational systems project, not a production DBMS.
