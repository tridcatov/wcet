#include "logger.h"

Logger::Logger(std::string name) {
    componentName = name;
    logEntryCount = 0;
}

llvm::raw_ostream & OutLogger::log() {
    return llvm::outs() << "[" << componentName << ":"
        << ++logEntryCount << "] ";
}

llvm::raw_ostream & ErrLogger::log() {
    return llvm::errs() << "[" << componentName << ":"
        << ++logEntryCount << "] ";
}
