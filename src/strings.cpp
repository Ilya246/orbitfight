#include "globals.hpp"
#include "net.hpp"
#include "strings.hpp"
#include "types.hpp"

#include <fstream>
#include <iostream>
#include <regex>
#include <string>

using namespace std;

namespace obf {
static char out[128];

void splitString(const string& str, vector<string>& vec, char delim) {
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

void displayMessage(const string& message, bool print) {
	for (int i = storedMessageCount - 1; i > 0; i--) {
		storedMessages[i] = storedMessages[i - 1];
	}
	if (print) {
		printf("%s\n", message.c_str());
	}
	storedMessages[0] = sf::String(message);
}
void displayMessage(const string& message) {
	displayMessage(message, true);
}
void printPreferred(const string& s) {
	cout << s;
	if (!headless) {
		string buf;
		buf.reserve(s.size());
		for (char c : s) {
			if (c == '\n') {
				if (buf.size() == 0) {
					continue;
				}
				displayMessage(buf, false);
				buf.clear();
			} else {
				buf += c;
			}
		}
		if (buf.size() != 0) {
			displayMessage(buf, false);
		}
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
					printPreferred(std::to_string(*(uint16_t*)variable.value));
					break;
				case obf::Types::Int:
					printPreferred(std::to_string(*(int*)variable.value));
					break;
				case obf::Types::Double:
					sprintf(out, "%g", *(double*)variable.value);
					printPreferred(string(out));
					break;
				case obf::Types::Bool:
					printPreferred(to_string(*(bool*)variable.value));
					break;
				case obf::Types::String:
					printPreferred(*(string*)variable.value);
					break;
				default:
					return 6;
			}
			printf("\n");
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

void parseCommand (const string& command) {
	vector<string> args;
	splitString(command, args, ' ');
	if (args.size() == 0) {
		printf("Invalid command.\n");
		return;
	}
	if (args[0] == "help") {
		printPreferred("help - print this\n"
		"config <line> - parse argument like a config file line\n"
		"lookup <id> - print info about entity ID in argument\n"
		"count - print amount of entities in existence\n"
		"showfps - print current framerate\n");
		if (headless) {
			printPreferred("reset - regenerate the star system\n"
			"players - list currently online players\n"
			"say <message> - say argument into ingame chat\n");
		}
		return;
	} else if (args[0] == "config") {
		if (args.size() < 2) {
			printPreferred("Invalid argument.\n");
			return;
		}
		int retcode = parseToml(string(args[1]));
		switch (retcode) {
			case 1:
				printPreferred("Invalid key.\n");
				break;
			case 3:
				printPreferred("Invalid value. Must be integer.\n");
				break;
			case 4:
				printPreferred("Invalid value. Must be a real number.\n");
				break;
			case 5:
				printPreferred("Invalid value. Must be true|false.\n");
				break;
			case 6:
				printPreferred("Invalid type specified for variable.\n");
				break;
			default:
				break;
		}
		return;
	} else if (args[0] == "say") {
		if (!headless) {
			displayMessage("This command only works if you're the server.");
			return;
		}
		if (command.size() < 4) {
			printf("Invalid argument.\n");
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
			printPreferred("Invalid argument.\n");
			return;
		}
		string id_s = string(args[1]);
		if (!regex_match(id_s, int_regex)) {
			printPreferred("Invalid ID.\n");
			return;
		}
		size_t id = stoi(id_s);
		for (Entity* e : updateGroup) {
			if (e->id == id) {
				sprintf(out, "Mass %g, radius %g, relative to star 0: x %g, y %g, vX %g, vY %g\n", e->mass, e->radius, e->x - stars[0]->x, e->y - stars[0]->y, e->velX - stars[0]->velX, e->velY - stars[0]->velY);
				printPreferred(string(out));
				return;
			}
		}
		printPreferred("No entity ID "+to_string(id)+" found.\n");
		return;
	} else if (args[0] == "count") {
		printPreferred(to_string(updateGroup.size())+"\n");
		return;
	} else if (args[0] == "reset") {
		if (!headless) {
			displayMessage("This command only works if you're the server.");
			return;
		}
		delta = 0.0;
		fullClear();
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
		if (!headless) {
			displayMessage("This command only works if you're the server.");
			return;
		}
		printf("%llu players:\n", playerGroup.size());
		for (Player* p : playerGroup) {
			cout << "	<" << p->name() << ">" << endl;
		}
		return;
	} else if (args[0] == "showfps") {
		printPreferred(to_string(framerate)+"\n");
		return;
	}
	printPreferred("Unknown command.\n");
}

}
