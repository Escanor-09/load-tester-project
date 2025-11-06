#include "httplib.h"
#include "kvcache.h"
#include <iostream>
#include <sstream>
#include <pqxx/pqxx>

LRUCache cache(100);

pqxx::connection get_connection() {
    return pqxx::connection("host=127.0.0.1 port=6432 dbname=kvstore user=postgres password=Escanor09@");
}

//handling create
void handle_create(const httplib::Request &req, httplib::Response &res) {
    std::string from_user = req.body;
    std::istringstream input(from_user);
    std::string key, value;
    std::getline(input, key, ' ');
    std::getline(input, value);

    try {
        pqxx::connection conn = get_connection();
        pqxx::work txn(conn);

        txn.exec_params("INSERT INTO kv_data (key,value) VALUES ($1,$2)", key, value);
        txn.commit();

        cache.create_key(key, value);
        res.set_content("Created Successfully", "text/plain");
    } catch (const std::exception &e) {
        res.set_content(std::string("Error: ") + e.what(), "text/plain");
    }
}

//handling delete
void handle_delete(const httplib::Request &req, httplib::Response &res) {
    std::string from_user = req.body;
    std::istringstream input(from_user);
    std::string key;
    std::getline(input, key);

    try {
        pqxx::connection conn = get_connection();
        pqxx::work txn(conn);

        txn.exec_params("DELETE FROM kv_data WHERE key=$1", key);
        txn.commit();

        cache.delete_key(key);
        res.set_content("Deleted Successfully", "text/plain");
    } catch (const std::exception &e) {
        res.set_content(std::string("Error: ") + e.what(), "text/plain");
    }
}

//handling read
void handle_read(const httplib::Request &req, httplib::Response &res) {
    std::string key = req.matches[1]; 

    try {
        //reading from cache
        std::string cache_value = cache.read_key(key);
        if (!cache_value.empty()) {
            res.set_content(cache_value, "text/plain");
            return;
        }

        pqxx::connection conn = get_connection();
        pqxx::work txn(conn);
        pqxx::result r = txn.exec_params("SELECT value FROM kv_data WHERE key=$1", key);

        if (r.empty()) {
            res.set_content("Key not Found", "text/plain");
        } else {
            std::string db_value = r[0][0].c_str();
            cache.create_key(key, db_value);
            res.set_content(db_value, "text/plain");
        }
    } catch (const std::exception &e) {
        res.set_content(std::string("Error: ") + e.what(), "text/plain");
    }
}

int main() {
    httplib::Server server;

    server.Post("/kvstore/create", handle_create);
    server.Post("/kvstore/delete", handle_delete);
    server.Get(R"(/kvstore/(\w+))", handle_read);

    const int num_threads = 20;
    server.new_task_queue = [num_threads]{return new httplib::ThreadPool(num_threads);};

    std::cout << "Server is listening on Port 8080\n";
    server.listen("0.0.0.0", 8080);
}
