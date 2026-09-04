#pragma once
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <string>
using namespace std;

namespace dlm{

  enum class LockMode{
    SHARED,
    EXCLUSIVE
  };
  struct LockEntry {
    LockMode mode;
    int ownerCount;
  };
  class LockManager {
    public:
      void acquireLock(int transactionId, const string& rowId, LockMode mode);
      void releaseLock(int transactionId, const string& rowId);
    private:
      mutex mutex_;
      condition_variable cv_;
      unordered_map<string,LockEntry> lockTable_;
  };

}//name space dlm