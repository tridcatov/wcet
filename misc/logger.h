#include "llvm/Support/raw_ostream.h"

class Logger {
protected:
    std::string componentName;
    unsigned long logEntryCount;
    Logger(std::string name); 
public:
    virtual llvm::raw_ostream & log() = 0;
};

class OutLogger : public Logger {
public:
    OutLogger(std::string name) : Logger(name) {
    }
    llvm::raw_ostream & log();
};

class ErrLogger : public Logger {
public:
    ErrLogger(std::string name) : Logger(name){
    }
    llvm::raw_ostream & log();
};
