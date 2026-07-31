#include "httplib.h"
#include "kvcache.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <pqxx/pqxx>

LRUCache cache(1000);

int g_server_port = 9001;

pqxx::connection get_connection()
{
    std::string db_name = "kvstore_" + std::to_string(g_server_port);
    std::string conn_str = "host=127.0.0.1 port=5432 dbname=" + db_name + " user=postgres password=Escanor09@";
    return pqxx::connection(conn_str);
}

void handle_create(const httplib::Request &req, httplib::Response &res)
{
    res.set_header("Connection", "close");
    std::string from_user = req.body;
    std::istringstream input(from_user);
    std::string key, value;
    std::getline(input, key, ' ');
    std::getline(input, value);

    try
    {
        pqxx::connection conn = get_connection();
        pqxx::work txn(conn);

        std::string query = "INSERT INTO kv_data (key, value) VALUES ($1, $2) ";

        txn.exec_params(query, key, value);
        txn.commit();

        cache.create_key(key, value);

        res.status = 201;
        res.set_content("Created Successfully", "text/plain");
    }
    catch (const pqxx::unique_violation &e)
    {
        res.status = 409;
        res.set_content("Error: Key already exists", "text/plain");
    }
    catch (const std::exception &e)
    {
        res.status = 500;
        res.set_content(std::string("Error: ") + e.what(), "text/plain");
    }
}

void handle_delete(const httplib::Request &req, httplib::Response &res)
{

    res.set_header("Connection", "close");

    std::string key = req.matches[1];

    try
    {
        pqxx::connection conn = get_connection();
        pqxx::work txn(conn);

        pqxx::result r = txn.exec_params("DELETE FROM kv_data WHERE key=$1 RETURNING key", key);
        if (r.empty())
        {
            res.status = 404;
            res.set_content("Key Not Found\n", "text/plain");
            return;
        }
        txn.commit();

        cache.delete_key(key);
        res.status = 200;
        res.set_content("Deleted Successfully\n", "text/plain");
    }

    catch (const std::exception &e)
    {
        res.status = 500;
        res.set_content(std::string("Error: ") + e.what(), "text/plain");
    }
}

void handle_read(const httplib::Request &req, httplib::Response &res)
{

    res.set_header("Connection", "close");

    std::string key = req.matches[1];

    try
    {

        std::string cache_value = cache.read_key(key);

        std::ofstream log_file("server_audit.log", std::ios_base::app);
        if (log_file.is_open())
        {
            std::string dummy_log_data(50000, 'X');
            log_file << "AUDIT LOG - Key Requested: " << key << " " << dummy_log_data << "\n";
            log_file.flush();
        }

        if (!cache_value.empty())
        {
            res.status = 200;

            // volatile uint32_t dummy_hash = 0;
            // for (int i = 0; i < 5000000; i++)
            // {
            //     dummy_hash += (i ^ 0x55AA55AA) + (i % 256);
            // }
            res.set_content(cache_value, "text/plain");
            return;
        }

        pqxx::connection conn = get_connection();
        pqxx::work txn(conn);
        pqxx::result r = txn.exec_params("SELECT value FROM kv_data WHERE key=$1", key);

        if (r.empty())
        {
            res.status = 404;
            res.set_content("Key not Found\n", "text/plain");
        }
        else
        {
            std::string db_value = r[0][0].c_str();
            cache.create_key(key, db_value);
            res.status = 200;
            res.set_content(db_value, "text/plain");
        }
    }
    catch (const std::exception &e)
    {
        res.status = 500;
        res.set_content(std::string("Error: ") + e.what(), "text/plain");
    }
}

void handle_update(const httplib::Request &req, httplib::Response &res)
{
    res.set_header("Connection", "close");

    std::string key = req.matches[1];
    std::string value = req.body;

    bool is_replication = req.has_header("X-Internal-Replication");

    try
    {

        pqxx::connection conn = get_connection();
        pqxx::work txn(conn);

        pqxx::result r;

        if (is_replication)
        {
            std::string query = "INSERT INTO kv_data (key, value) VALUES ($1, $2) "
                                "ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value RETURNING key";
            r = txn.exec_params(query, key, value);
        }
        else
        {
            r = txn.exec_params("UPDATE kv_data SET value=$1 WHERE key=$2 RETURNING key", value, key);
        }
        if (r.empty() && !is_replication)
        {
            res.status = 404;
            res.set_content("Key not found in DB\n", "text/plain");
            return;
        }
        txn.commit();

        cache.delete_key(key);
        res.status = 200;
        res.set_content("Updated Successfully\n", "text/plain");
    }

    catch (const std::exception &e)
    {
        res.status = 500;
        res.set_content(std::string("Error: ") + e.what(), "text/plain");
    }
}

void handle_all_keys(const httplib::Request &req, httplib::Response &res)
{
    res.set_header("Connection", "close");
    try
    {

        pqxx::connection conn = get_connection();
        pqxx::work txn(conn);

        pqxx::result r = txn.exec("SELECT key FROM kv_data");

        std::string response_body = "";
        for (const auto &row : r)
        {
            response_body += row[0].c_str() + std::string("\n");
        }
        std::cout << "[ALLKEYS] Port " << g_server_port << " returning:\n";
        std::cout << response_body << std::endl;

        res.status = 200;
        res.set_content(response_body, "text/plain");
    }
    catch (const std::exception &e)
    {
        res.status = 500;
        res.set_content(std::string("Error: ") + e.what(), "text/plain");
    }
}

void handle_sync(const httplib::Request &req, httplib::Response &res)
{
    res.set_header("Connection", "close");
    std::string key = req.matches[1];
    std::string value = req.body;

    try
    {
        pqxx::connection conn = get_connection();
        pqxx::work txn(conn);

        std::string query = "INSERT INTO kv_data (key, value) VALUES ($1, $2) "
                            "ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value RETURNING key";
        pqxx::result r = txn.exec_params(query, key, value);
        txn.commit();

        cache.delete_key(key);

        if (r.empty())
        {
            res.status = 200;
            res.set_content("Ignored: Never version already present\n", "text/plain");
        }

        else
        {
            res.status = 201;
            res.set_content("Synced Successfully\n", "text/plain");
        }
    }
    catch (const std::exception &e)
    {
        res.status = 500;
        res.set_content(std::string("Error: ") + e.what(), "text/plain");
    }
}

int main(int argc, char *argv[])
{
    if (argc > 1)
    {
        try
        {
            g_server_port = std::stoi(argv[1]);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Invalid  port argument! USing default port 9001.\n";
            g_server_port = 9001;
        }
    }

    httplib::Server server;

    server.Post("/kvstore/create", handle_create);
    server.Put(R"(/kvstore/(\w+))", handle_update);
    server.Delete(R"(/kvstore/(\w+))", handle_delete);
    server.Get("/kvstore/allkeys", handle_all_keys);
    server.Get(R"(/kvstore/(\w+))", handle_read);

    server.Get("/health", [](const httplib::Request &, httplib::Response &res)
               {
        res.status = 200;
        res.set_content("OK","text/plain"); });

    server.Post(R"(/kvstore/sync/(\w+))", handle_sync);

    const int num_threads = 300;
    server.new_task_queue = [num_threads]
    { return new httplib::ThreadPool(num_threads); };

    std::cout << "Server is running on port " << g_server_port << " (Connected to DB: kvstore_)" << g_server_port << ")\n";
    server.listen("0.0.0.0", g_server_port);
}