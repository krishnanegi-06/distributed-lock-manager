#include "dlm/common/Logger.h"
#include<iostream>
using namespace std;
namespace dlm{
  void Logger::info(const std::string& message){
    cout<< "[INFO]" <<message<<endl;
  }
  void Logger::warn(const string& message){
    cout<< "[WARN]" <<message<<endl;
  }
  void Logger::error(const string& message){
    cerr<< "[ERROR]" <<message<<endl;
  }

}//namespace dlm