#include "globals.hpp"
#include "types.hpp"

#include <fstream>
#include <iostream>
#include <regex>
#include <string>

using namespace std;

namespace obf {

static const regex int_regex = regex("[0-9]*"),
	double_regex = regex("[0-9]*\\.[0-9]*"),
	boolt_regex = regex("([tT][rR][uU][eE])|1"), boolf_regex = regex("([fF][aA][lL][sS][eE])|0");

void splitString(string_view str, vector<string_view> &vec, char delim) {
	size_t last = 0;
	for (size_t i = 0; i < str.size(); i++) {
		if (str[i] == delim) {
			if (i > last) {
				vec.push_back(str.substr(last, i - last));
			}
			last = i + 1;
		}
	}
	if (last < str.size()) {
		vec.push_back(str.substr(last, str.size() - last));
	}
}

int parseToml(const string& line) {
	string key = "";
	string value = "";
	bool parsingKey = true;
	for (char c : line) {
		switch (c) {
		case '#':
			goto stopParsing;
		case '=':
			parsingKey = false;
			break;
		case ' ':
			break;
		case '\n':
			break;
		default:
			if (parsingKey) {
				key += c;
			} else {
				value += c;
			}
			break;
		}
	}

stopParsing:

	if (key.empty()) {
		return (int)!value.empty(); // supposed to tolerate empty lines while still checking for empty keys
	}

	const auto &it = obf::vars.find(key);
	if (it != obf::vars.end()) {
		Var variable = it->second;
		switch (variable.type) {
			case obf::Types::Short_u: {
				if (!regex_match(value, int_regex)) {
					return 2;
				}
				*(uint16_t*)variable.value = (uint16_t)stoi(value);
				break;
			}
			case obf::Types::Int: {
				if (!regex_match(value, int_regex)) {
					return 2;
				}
				*(int*)variable.value = stoi(value);
				break;
			}
			case obf::Types::Double: {
				if (!regex_match(value, double_regex)) {
					return 3;
				}
				*(double*)variable.value = stod(value);
				break;
			}
			case obf::Types::Bool: {
				bool temp = regex_match(value, boolt_regex);
				if (!temp && !regex_match(value, boolf_regex)) {
					return 4;
				}
				*(bool*)variable.value = temp;
				break;
			}
			case obf::Types::String: {
				*(std::string*)variable.value = value;
				break;
			}
			default: {
				return 5;
			}
		}
	}
	return 0;
}

int parseTomlFile(const string& filename) {
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
		case 1:
			printf("Invalid key at %s:%u.\n", filename.c_str(), lineN);
			break;
		case 2:
			printf("Invalid value at %s:%u. Must be integer.\n", filename.c_str(), lineN);
			break;
		case 3:
			printf("Invalid value at %s:%u. Must be a real number.\n", filename.c_str(), lineN);
			break;
		case 4:
			printf("Invalid value at %s:%u. Must be true|false.\n", filename.c_str(), lineN);
			break;
		case 5:
			printf("Invalid type specified for variable at %s:%u.\n", filename.c_str(), lineN);
			break;
		default:
			break;
		}
	}

	return 0;
}

void parseCommand (const string_view& command) {
	vector<string_view> args;
	splitString(command, args, ' ');
	if (args[0] == "help") {
		printf("help - print this\n");
		printf("config - parse argument like a config file line\n");
		printf("say - say argument into ingame chat\n");
		printf("lookup - print info about entity ID in argument\n");
		return;
	} else if (args[0] == "config") {
		int retcode = parseToml(string(command.substr(7)));
		switch (retcode) {
			case 1:
				printf("Invalid key.\n");
				break;
			case 2:
				printf("Invalid value. Must be integer.\n");
				break;
			case 3:
				printf("Invalid value. Must be a real number.\n");
				break;
			case 4:
				printf("Invalid value. Must be true|false.\n");
				break;
			case 5:
				printf("Invalid type specified for variable.\n");
				break;
			default:
				break;
		}
		return;
	} else if (args[0] == "say") {
		sf::Packet chatPacket;
		std::string sendMessage;
		sendMessage.append("Server: ").append(command.substr(4));
		chatPacket << Packets::Chat << sendMessage;
		for (Player* p : playerGroup) {
			p->tcpSocket.send(chatPacket);
		}
		cout << sendMessage << endl;
		return;
	} else if (args[0] == "lookup") {
		string id_s(command.substr(7));
		if (!regex_match(id_s, int_regex)) {
			printf("Invalid ID.\n");
			return;
		}
		size_t id = stoi(id_s);
		for (Entity* e : updateGroup) {
			if (e->id == id) {
				printf("Mass %g, radius %g, relative to star 0: x %g, y %g, vX %g, vY %g\n", e->mass, e->radius, e->x - stars[0]->x, e->y - stars[0]->y, e->velX - stars[0]->velX, e->velY - stars[0]->velY);
				return;
			}
		}
		printf("No entity ID %llu found.\n", id);
		return;
	}
	printf("Unknown command.\n");
}

}
