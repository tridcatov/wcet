#include "logger.h"

Logger::Logger(std::string name) {
    componentName = name;
    logEntryCount = 0;
}

llvm::raw_ostream & OutLogger::log(int level) {
    level = level < 0 ? 0 : level;
    for (int i = 0; i < level; i++)
        llvm::outs() << "\t";
    return llvm::outs() << "[" << componentName << ":"
        << ++logEntryCount << "] ";
}

llvm::raw_ostream & ErrLogger::log(int level) {
    level = level < 0 ? 0 : level;
    for (int i = 0; i < level; i++)
        llvm::errs() << "\t";
    return llvm::errs() << "[" << componentName << ":"
        << ++logEntryCount << "] ";
}
