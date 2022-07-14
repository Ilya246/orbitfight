#include "globals.hpp"
#include "net.hpp"
#include "types.hpp"

#include <fstream>
#include <iostream>
#include <regex>
#include <string>

using namespace std;

namespace obf {

static const regex int_regex = regex("[0-9]*"),
	double_regex = regex("[0-9]*(\\.[0-9]*)?"),
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

void stripSpecialChars(string& str) {
	int offset = 0;
	for (size_t i = 0; i < str.size(); i++) {
		if (str[i] < 32) {
			offset--;
		}
		if (offset > 0) {
			str[i - offset] = str[i];
		}
	}
	if (offset > 0) {
		str.erase(str.size() - offset);
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
		if (value.empty()) {
			switch (variable.type) {
				case obf::Types::Short_u:
					printf("%u\n", *(uint16_t*)variable.value);
					break;
				case obf::Types::Int:
					printf("%d\n", *(int*)variable.value);
					break;
				case obf::Types::Double:
					printf("%g\n", *(double*)variable.value);
					break;
				case obf::Types::Bool:
					printf("%u\n", *(bool*)variable.value);
					break;
				case obf::Types::String:
					printf("%s\n", (*(string*)variable.value).c_str());
					break;
				default:
					return 6;
			}
			return 2;
		}
		switch (variable.type) {
			case obf::Types::Short_u: {
				if (!regex_match(value, int_regex)) {
					return 3;
				}
				*(uint16_t*)variable.value = (uint16_t)stoi(value);
				break;
			}
			case obf::Types::Int: {
				if (!regex_match(value, int_regex)) {
					return 3;
				}
				*(int*)variable.value = stoi(value);
				break;
			}
			case obf::Types::Double: {
				if (!regex_match(value, double_regex)) {
					return 4;
				}
				*(double*)variable.value = stod(value);
				break;
			}
			case obf::Types::Bool: {
				bool temp = regex_match(value, boolt_regex);
				if (!temp && !regex_match(value, boolf_regex)) {
					return 5;
				}
				*(bool*)variable.value = temp;
				break;
			}
			case obf::Types::String: {
				*(std::string*)variable.value = value;
				break;
			}
			default: {
				return 6;
			}
		}
	} else {
		return 1;
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
		case 3:
			printf("Invalid value at %s:%u. Must be integer.\n", filename.c_str(), lineN);
			break;
		case 4:
			printf("Invalid value at %s:%u. Must be a real number.\n", filename.c_str(), lineN);
			break;
		case 5:
			printf("Invalid value at %s:%u. Must be true|false.\n", filename.c_str(), lineN);
			break;
		case 6:
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
		printf("config <line> - parse argument like a config file line\n");
		printf("say <message> - as a server, say argument into ingame chat\n");
		printf("lookup <id> - print info about entity ID in argument\n");
		printf("reset - regenerate the star system\n");
		printf("players - list currently online players\n");
		return;
	} else if (args[0] == "config") {
		if (args.size() < 2) {
			printf("Invalid argument.\n");
			return;
		}
		int retcode = parseToml(string(args[1]));
		switch (retcode) {
			case 1:
				printf("Invalid key.\n");
				break;
			case 3:
				printf("Invalid value. Must be integer.\n");
				break;
			case 4:
				printf("Invalid value. Must be a real number.\n");
				break;
			case 5:
				printf("Invalid value. Must be true|false.\n");
				break;
			case 6:
				printf("Invalid type specified for variable.\n");
				break;
			default:
				break;
		}
		return;
	} else if (args[0] == "say") {
		if (command.size() < 4) {
			printf("Invalid argument.");
			return;
		}
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
		if (args.size() < 2) {
			printf("Invalid argument.");
			return;
		}
		string id_s = string(args[1]);
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
	} else if (args[0] == "reset") {
		delta = 0.0;
		for (Entity* e :updateGroup) {
			if (e->type() != Entities::Triangle) {
				delete e;
			}
		}
		generateSystem();
		for (Entity* e : updateGroup) {
			if (e->type() != Entities::Triangle) {
				e->syncCreation();
			}
		}
		for (Player* p : playerGroup) {
			setupShip(p->entity);
		}
		std::string sendMessage = "ANNOUNCEMENT: The system has been regenerated.";
		relayMessage(sendMessage);
		if (autorestart) {
			lastAutorestartNotif = -autorestartNotifSpacing;
			lastAutorestart = globalTime;
		}
		return;
	} else if (args[0] == "players") {
		printf("%llu players:\n", playerGroup.size());
		for (Player* p : playerGroup) {
			cout << "	<" << p->name() << ">" << endl;
		}
		return;
	} else if (args[0] == "showfps") {
		printf("%llu\n", framerate);
		return;
	}
	printf("Unknown command.\n");
}

}
