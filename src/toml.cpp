#include "globals.hpp"

#include <fstream>
#include <regex>
#include <string>

using namespace std;

namespace obf{

int parseToml(string line){
    string key = "";
    string value = "";
    bool parsingKey = true;
    for(char c : line){
        if(c == '#')return 1;
        if(c == ' ' || c == '\n')continue;
        if(c == '='){
            parsingKey = false;
            continue;
        }
        if(parsingKey){
            key += c;
        }else{
            value += c;
        }
    }
    if(key == "" || value == "")return 2;
    if(key == "name"){
        obf::name = value;
    }else if(key == "port"){
        if(!regex_match(value, regex("[0-9]*"))){
            return 3;
        }
        obf::port = stoi(value);
    }else{
        return 4;
    }
    return 0;
}

int parseTomlFile(string filename){
    std::ifstream in;
    in.open(filename);
    if(!in){
        return 1;
    }
    string line;
    int lineN = 1;
    while(!in.eof()){
        getline(in, line);
        int retcode = parseToml(line);
        switch(retcode){
            case 3:
                printf("Invalid value at %s:%u.\n", filename.c_str(), lineN);
            case 4:
                printf("Invalid key at %s:%u.\n", filename.c_str(), lineN);
        }
        lineN++;
    }
    return 0;
}

}
