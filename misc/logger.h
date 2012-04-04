#include "llvm/Support/raw_ostream.h"

class Logger {
protected:
    std::string componentName;
    unsigned long logEntryCount;
    Logger(std::string name); 
public:
    virtual llvm::raw_ostream & log(int level = 0) = 0;
};

class OutLogger : public Logger {
public:
    OutLogger(std::string name) : Logger(name) {
    }

    ~OutLogger()
    {
    }
    llvm::raw_ostream & log(int level = 0);
};

class ErrLogger : public Logger {
public:
    ErrLogger(std::string name) : Logger(name){
    }
    ~ErrLogger()
    {
    }
    llvm::raw_ostream & log(int level = 0);
};
