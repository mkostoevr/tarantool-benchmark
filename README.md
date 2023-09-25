# Tarantool Perfmeter

A tool for Tarantool performance analysis.

## Manual analysis

1. Execute the Tarantool to test.
2. Run `ttbench -p <port> test_name [-c <request_count>] [-b <batch_size>]`.
3. Optionally specify the test payload format in a yaml file and pass it as `-i <yaml_file>`:
   
   ```yaml
   - type: 'unsigned'
     distribution: 'linear'
   ```
   
   Currently supported types: `unsigned`.
   
   Currently supported distributions: `incremental`, `decremental`, `linear`.

## Config-based analysis

TBD

<!--
```yaml
---
  script: "memtx_empty.lua"
  request: "insert"
  space: "s"
  payload:
    - type: "unsigned"
      is_unique: true
    - type: "string"
      min_len: 1
      max_len: 16
  count: 1000000
  batch: 1000
```
-->
