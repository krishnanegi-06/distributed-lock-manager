#include "dlm/lock_manager/LockManager.h"
#include "dlm/common/Logger.h"

namespace dlm{

  bool isCompatible(LockMode existing , LockMode requested){
    if (existing == LockMode :: SHARED && requested == LockMode:: SHARED){
      return true;
    }
    return false;
  }
  void LockManager::acquireLock(int transactionId , const string& rowId, LockMode mode){
    unique_lock<mutex>lock(mutex_);
    while(lockTable_.find(rowId)!=lockTable_.end() && !isCompatible(lockTable_[rowId].mode, mode)){
      Logger::info("Transaction "+ to_string(transactionId)+ " waiting for row " + rowId);
      cv_.wait(lock);
    }
    if(lockTable_.find(rowId) == lockTable_.end()) {
      lockTable_[rowId] = LockEntry{mode , 1};
    }
    else{
      lockTable_[rowId].ownerCount++;
    }
    Logger::info("Transaction "+to_string(transactionId) + " GRANTED lock on row "+ rowId);
  }
  void LockManager::releaseLock(int transactionId, const string& rowId) {
    unique_lock<mutex> lock(mutex_);
    if(lockTable_.find(rowId) == lockTable_.end()) {
        Logger::warn("Transactin " + to_string(transactionId) + " tried to release unheld lock on row" + rowId);
        return;
    }
    lockTable_[rowId].ownerCount--;
    if(lockTable_[rowId].ownerCount<=0) {
      lockTable_.erase(rowId);
    }
    Logger::info("Transaction"+to_string(transactionId)+ " RELEASED lock on row " + rowId);
    cv_.notify_all();
  }
}