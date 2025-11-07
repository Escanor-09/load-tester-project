#include "httplib.h"
#include <iostream>
#include <sstream>

int main() {
    httplib::Client client("localhost", 8080); // connect to server
    std::string user_input;

    while (true) {
        std::cout << ">> ";
        std::getline(std::cin, user_input);

        if (user_input == "exit")
            break;

        std::istringstream input(user_input);
        std::string cmd, key, value;
        input >> cmd >> key;
        std::getline(input, value);
        if (!value.empty() && value[0] == ' ') value.erase(0, 1); 

        if (cmd == "create") {
            if (key.empty() || value.empty()) {
                std::cout << "Usage: create <key> <value>\n";
                continue;
            }

            auto res = client.Post("/kvstore/create", key + " " + value, "text/plain");
            if (res)
                std::cout << "Server replied: " << res->body << std::endl;
            else
                std::cout << "Request failed\n";

        } else if (cmd == "delete") {
            if (key.empty()) {
                std::cout << "Usage: delete <key>\n";
                continue;
            }

            auto res = client.Delete(("/kvstore/" + key).c_str());
            if (res)
                std::cout << "Server replied: " << res->body << std::endl;
            else
                std::cout << "Request failed\n";

        } else if (cmd == "read") {
            if (key.empty()) {
                std::cout << "Usage: read <key>\n";
                continue;
            }

            auto res = client.Get(("/kvstore/" + key).c_str());
            if (res)
                std::cout << "Server replied: " << res->body << std::endl;
            else
                std::cout << "Request failed\n";

        }else if(cmd == "update"){
            if(key.empty() || value.empty()){
                std::cout << "Usage: update <key> <value>\n";
                continue;
            }
            auto res = client.Put(("/kvstore/" + key).c_str(),value,"text/plain");
            if(res)
                std::cout << "Server replied: " << res->body << std::endl;
            else 
                std::cout << "Request failed\n";
        } else {
            std::cout << "Unknown command. Use create/read/update/delete/exit.\n";
        }
    }

    return 0;
}
