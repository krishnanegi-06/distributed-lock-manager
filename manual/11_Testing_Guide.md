# Chapter 11: Testing Guide & Test Suite

The test suite (`tests/test_all.cpp`) provides rigorous automated verification of all individual subsystems, protocol boundaries, edge cases, and concurrency properties.

---

## 1. Test Framework Design

The test suite is built without external dependencies (such as GTest or Catch2) using lightweight, high-performance C++ macros:

```cpp
#define ASSERT_TRUE(cond) \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return false; \
    }

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        std::cerr << "Assertion failed: " #a " == " #b " (" << (a) << " vs " << (b) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return false; \
    }
```

Each test is an independent function returning `bool`. The test harness tracks passed/failed counts and outputs standardized summary diagnostics.

---

## 2. Exhaustive Test Suite Breakdown

### Group A: Lock Manager Unit Tests
1. **`shared_lock_compatibility`**:
   - Acquires `SHARED` on resource `"R1"` with $T_1$.
   - Acquires `SHARED` on resource `"R1"` with $T_2$.
   - Asserts both return `true`.
2. **`exclusive_lock_blocks`**:
   - Acquires `EXCLUSIVE` on resource `"R1"` with $T_1$.
   - Attempts to acquire `SHARED` on `"R1"` with $T_2$ (with 100ms timeout).
   - Asserts $T_2$ returns `false` (timeout).
3. **`release_lock`**:
   - $T_1$ holds exclusive lock; $T_2$ blocked in a separate thread.
   - $T_1$ releases lock via `releaseLock()`.
   - Asserts $T_2$ wakes up and successfully acquires `"R1"`.
4. **`release_all_locks`**:
   - $T_1$ acquires locks on `"R1"`, `"R2"`, `"R3"`.
   - Calls `releaseAllLocks(T1)`.
   - Asserts that checking any of `"R1"`, `"R2"`, or `"R3"` returns `false`.
5. **`wait_for_edges`**:
   - $T_1$ holds `"R1"`; $T_2$ and $T_3$ block waiting for `"R1"`.
   - Calls `getWaitForEdges()`.
   - Asserts edges $(T_2 \to T_1)$ and $(T_3 \to T_1)$ are both present.
6. **`shared_exclusive_conflict`**:
   - Confirms that while `SHARED` is held, an `EXCLUSIVE` request is rejected.

---

### Group B: Deadlock Detection Tests
7. **`no_deadlock`**:
   - Supplies a DAG: $T_1 \to T_2 \to T_3$.
   - Asserts `detectCycles()` returns an empty vector.
8. **`simple_deadlock`**:
   - Supplies a 2-node cycle: $T_1 \to T_2$ and $T_2 \to T_1$.
   - Asserts `detectCycles()` detects the cycle and identifies $T_2$ (youngest) as victim.
9. **`three_way_deadlock`**:
   - Supplies a 3-node cycle: $T_1 \to T_2 \to T_3 \to T_1$.
   - Asserts `detectCycles()` detects the cycle and identifies $T_3$ (youngest) as victim.
10. **`empty_edges`**:
    - Invokes `detectCycles()` with an empty edge vector.
    - Asserts no exception is thrown and returns empty.

---

### Group C: Transaction & WAL Tests
11. **`transaction_lifecycle`**:
    - Verifies transition through `ACTIVE` $\to$ `PREPARING` $\to$ `COMMITTED`.
12. **`txn_manager_begin_commit`**:
    - Verifies starting a transaction, acquiring locks, and committing cleanly.
13. **`txn_manager_abort`**:
    - Verifies that aborting a transaction rolls back state and frees all locks.
14. **`txn_manager_prepare`**:
    - Verifies the prepare phase in isolation for 2PC participation.
15. **`transaction_log`**:
    - Writes `BEGIN`, `LOCK`, `PREPARE`, and `COMMIT` records to a test WAL file.
    - Closes file and instantiates a recovery reader.
    - Asserts all 4 records are read back accurately in correct sequence.

---

### Group D: Network Protocol Tests
16. **`message_roundtrip`**:
    - Populates a `Message` with all fields: `type = LOCK_REQUEST`, `sender = 1`, `receiver = 2`, `txnId = 100`, `resource = "acc:1"`, `payload = "DATA"`.
    - Serializes to string and deserializes back.
    - Asserts all fields match exactly.
17. **`message_empty_fields`**:
    - Tests serialization and deserialization when `resourceId` and `payload` are empty strings.

---

## 3. How to Run Tests

### Running Directly:
```powershell
.\build\Debug\dlm_tests.exe
```

### Running via CTest:
```powershell
cd build
ctest -C Debug --output-on-failure
```

---

## 4. Adding a New Test

To add a new test case:
1. Define your test function in `tests/test_all.cpp`:
   ```cpp
   static bool test_my_new_feature() {
       LockManager lm;
       ASSERT_TRUE(lm.acquireLock(1, "resource_X", LockMode::EXCLUSIVE));
       ASSERT_TRUE(lm.releaseLock(1, "resource_X"));
       return true;
   }
   ```
2. Register it in `main()` of `test_all.cpp`:
   ```cpp
   RUN_TEST(test_my_new_feature);
   ```
3. Recompile and run:
   ```powershell
   cmake --build build --config Debug --target dlm_tests
   .\build\Debug\dlm_tests.exe
   ```
