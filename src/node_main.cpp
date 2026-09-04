#include "dlm/common/Logger.h"
#include "dlm/lock_manager/LockManager.h"
#include <thread>
#include <chrono>
using namespace std;
void simulateTransaction(dlm::LockManager& lm, int txnId, const string& rowId) {
  lm.acquireLock(txnId, rowId, dlm::LockMode::SHARED);
  dlm::Logger::info("Transaction " +to_string(txnId) + " is working on row" + rowId);
  this_thread::sleep_for(chrono::seconds(2));
  lm.releaseLock(txnId, rowId);
}
int main(){
  dlm::Logger::info("Node starting ...");
  dlm::LockManager lm;
  thread t1(simulateTransaction, ref(lm),1,"row_10");
  thread t2(simulateTransaction, ref(lm), 2,"row_10");

  t1.join();
  t2.join();
  dlm::Logger::info("Test Complete .");
  return 0; 
}