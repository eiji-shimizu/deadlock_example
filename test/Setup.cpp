#include "General.h"

#include "Common.h"
#include "Logger.h"
#include "Utils.h"

#include <iostream>

PapierMache::Logger<std::ostream> logger{std::cout};

//const std::map<std::string, std::map<std::string, std::string>> webConfiguration{PapierMache::readConfiguration("./webconfig/server.ini")};
