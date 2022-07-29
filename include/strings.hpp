#pragma once

#include <regex>
#include <vector>

using namespace std;

namespace obf {
inline regex int_regex = regex("[0-9]*"),
	double_regex = regex("[0-9]*(\\.[0-9]*)?"),
	boolt_regex = regex("([tT][rR][uU][eE])|1"), boolf_regex = regex("([fF][aA][lL][sS][eE])|0");

void splitString(const string&, vector<string>&, char);
void stripSpecialChars(string&);

int parseToml(const string&);
int parseTomlFile(const string&);

void parseCommand(const string&);

}
