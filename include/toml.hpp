#pragma once

namespace obf {

int parseToml(const std::string&);
int parseTomlFile(const std::string&);

void parseCommand(const std::string_view&);

}
