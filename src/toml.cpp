#include "globals.hpp"

#include <fstream>
#include <regex>
#include <string>

using namespace std;

namespace obf {

static const regex int_regex = regex("[0-9]*"),
	double_regex = regex("[0-9]*\\.[0-9]*");
int parseToml(const string &line) {
	string key = "";
	string value = "";
	bool parsingKey = true;
	for (char c : line) {
		switch (c) {
		case '#': return 1;
		case '=':
			parsingKey = false;
		case ' ':
		case '\n':
			break;
		default:
			if (parsingKey) {
				key += c;
			} else {
				value += c;
			}
		}
	}

	if (key.empty() || value.empty()) return 2;
	if (key == "name") {
		obf::name = value;
	} else if (key == "port") {
		if (!regex_match(value, int_regex)) {
			return 3;
		}

		obf::port = stoi(value);
	} else if(key == "syncSpacing") {
		if (!regex_match(value, double_regex)) {
			return 3;
		}
		obf::syncSpacing = stod(value);
	} else {
		return 4;
	}

	return 0;
}

int parseTomlFile(const string &filename) {
	std::ifstream in;
	in.open(filename);
	if (!in) {
		return 1;
	}

	string line;
	for (int lineN = 1; !in.eof(); lineN++) {
		getline(in, line);
		int retcode = parseToml(line);
		switch (retcode) {
		case 3:
			printf("Invalid value at %s:%u.\n", filename.c_str(), lineN);
			break;
		case 4:
			printf("Invalid key at %s:%u.\n", filename.c_str(), lineN);
			break;
		default:
			break;
		}
	}

	return 0;
}

}
