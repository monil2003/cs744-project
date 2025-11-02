#include <iostream>
#include <httplib.h>
#include <pqxx/pqxx>
#include <string>
#include <unordered_map>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <sstream>
#include <memory>
#include <functional>

using namespace std;
using namespace std::chrono;
using pqxx::connection;
using pqxx::result;
using pqxx::work;
using std::make_unique;
using std::shared_lock;
using std::shared_mutex;
using std::unique_lock;


const size_t CACHE_MAX_SIZE = 100;

const char *DB_CONN1 =
    "host=127.0.0.1 port=5432 "
    "dbname=stressdb "
    "user=stressuser "
    "password=stresspass";

const char *DB_CONN2 =
    "host=127.0.0.1 port=5433 "
    "dbname=stressdb "
    "user=stressuser "
    "password=stresspass";

const char *DB_CONN3 =
    "host=127.0.0.1 port=5434 "
    "dbname=stressdb "
    "user=stressuser "
    "password=stresspass";

thread_local unique_ptr<pqxx::connection> thread_conn1;
thread_local unique_ptr<pqxx::connection> thread_conn2;
thread_local unique_ptr<pqxx::connection> thread_conn3;

void init_thread_conns() {
    if (!thread_conn1 || !thread_conn1->is_open())
        thread_conn1 = make_unique<pqxx::connection>(DB_CONN1);
    if (!thread_conn2 || !thread_conn2->is_open())
        thread_conn2 = make_unique<pqxx::connection>(DB_CONN2);
    // if (!thread_conn3 || !thread_conn3->is_open())
    //     thread_conn3 = make_unique<pqxx::connection>(DB_CONN3);
}

// Choose DB based on key hash
pqxx::connection &get_thread_conn_for_key(const string &key) {
    init_thread_conns();
    size_t idx = std::hash<std::string>{}(key) % 2;
    if (idx == 0) return *thread_conn1;
    else if (idx == 1) return *thread_conn2;
    else return *thread_conn3;
}


//Metrics
struct Metrics
{
    atomic<size_t> cache_hits{0};
    atomic<size_t> cache_misses{0};
    atomic<size_t> total_gets{0};
    atomic<size_t> total_posts{0};
    atomic<size_t> total_deletes{0};
    atomic<size_t> total_db_writes{0};
    atomic<size_t> total_db_reads{0};
    atomic<double> avg_db_read_latency_ms{0.0};
    atomic<double> avg_db_write_latency_ms{0.0};
} metrics;


class LRUCache
{
public:
    LRUCache(size_t capacity) : max_size(capacity) {}

    bool get(const string &key, string &value)
    {
        shared_lock<shared_mutex> read_lock(lock);
        auto it = map_kv.find(key);
        if (it == map_kv.end())
        {
            metrics.cache_misses++;
            return false;
        }
        value = it->second.first;
        read_lock.unlock();

        unique_lock<shared_mutex> write_lock(lock);
        it = map_kv.find(key);
        if (it != map_kv.end())
        {
            list_kv.splice(list_kv.begin(), list_kv, it->second.second);
            metrics.cache_hits++;
        }
        else
        {
            metrics.cache_misses++;
            return false;
        }
        return true;
    }

    void put(const string &key, const string &value)
    {
        unique_lock<shared_mutex> guard(lock);
        auto it = map_kv.find(key);
        if (it != map_kv.end())
        {
            it->second.first = value;
            list_kv.splice(list_kv.begin(), list_kv, it->second.second);
        }
        else
        {
            list_kv.push_front(key);
            map_kv[key] = {value, list_kv.begin()};
            if (map_kv.size() > max_size)
            {
                string lru_key = list_kv.back();
                list_kv.pop_back();
                map_kv.erase(lru_key);
            }
        }
    }

    void erase(const string &key)
    {
        unique_lock<shared_mutex> guard(lock);
        auto it = map_kv.find(key);
        if (it != map_kv.end())
        {
            list_kv.erase(it->second.second);
            map_kv.erase(it);
        }
    }

    size_t size()
    {
        shared_lock<shared_mutex> guard(lock);
        return map_kv.size();
    }

private:
    size_t max_size;
    list<string> list_kv;
    unordered_map<string, pair<string, list<string>::iterator>> map_kv;
    mutable shared_mutex lock;
};

LRUCache kv_cache(CACHE_MAX_SIZE);

string path_to_key(const httplib::Request &req)
{
    string p = req.path;
    if (!p.empty() && p[0] == '/')
        return p.substr(1);
    return p;
}

void handle_get(const httplib::Request &req, httplib::Response &rsp)
{
    metrics.total_gets++;
    string key = path_to_key(req);
    if (key.empty())
    {
        rsp.status = 400;
        rsp.set_content("Bad Request: empty key", "text/plain");
        return;
    }

    string value;
    if (kv_cache.get(key, value))
    {
        rsp.status = 200;
        rsp.set_content(value, "text/plain");
        return;
    }

    auto start_time = steady_clock::now();
    try
    {
        work W(get_thread_conn_for_key(key));
        string query = "SELECT value_text FROM kv_store WHERE key_text = " + W.quote(key) + ";";
        result R = W.exec(query);
        W.commit();

        metrics.total_db_reads++;
        auto latency = duration_cast<milliseconds>(steady_clock::now() - start_time).count();
        double cur_avg = metrics.avg_db_read_latency_ms.load();
        metrics.avg_db_read_latency_ms.store((cur_avg + latency) / 2.0);

        if (R.empty())
        {
            rsp.status = 404;
            rsp.set_content("Key Not Found", "text/plain");
        }
        else
        {
            string value_from_db = R[0][0].as<string>();
            kv_cache.put(key, value_from_db);
            rsp.status = 200;
            rsp.set_content(value_from_db, "text/plain");
        }
    }
    catch (const exception &e)
    {
        rsp.status = 500;
        rsp.set_content(string("Database error: ") + e.what(), "text/plain");
    }
}

void handle_post(const httplib::Request &req, httplib::Response &rsp)
{
    metrics.total_posts++;
    string key = path_to_key(req);
    if (key.empty())
    {
        rsp.status = 400;
        rsp.set_content("Bad Request: empty key", "text/plain");
        return;
    }

    string value = req.body;
    kv_cache.put(key, value);
    int kx = 0;

    auto start_time = steady_clock::now();
    try
    {
        work W(get_thread_conn_for_key(key));
        string query =
            "INSERT INTO kv_store (key_text, value_text) VALUES (" +
            W.quote(key) + ", " + W.quote(value) +
            ") ON CONFLICT (key_text) DO UPDATE SET value_text = EXCLUDED.value_text;";

        W.exec(query);
        W.commit();

        metrics.total_db_writes++;
        auto latency = duration_cast<milliseconds>(steady_clock::now() - start_time).count();
        double cur_avg = metrics.avg_db_write_latency_ms.load();
        metrics.avg_db_write_latency_ms.store((cur_avg + latency) / 2.0);

        rsp.status = 201;
        rsp.set_content("Stored key: " + key, "text/plain");
    }
    catch (const exception &e)
    {
        rsp.status = 500;
        rsp.set_content(string("Database error: ") + e.what(), "text/plain");
    }
}

void handle_delete(const httplib::Request &req, httplib::Response &rsp)
{
    metrics.total_deletes++;
    string key = path_to_key(req);
    if (key.empty())
    {
        rsp.status = 400;
        rsp.set_content("Bad Request: empty key", "text/plain");
        return;
    }

    auto start_time = steady_clock::now();
    try
    {
        work W(get_thread_conn_for_key(key));
        string query = "DELETE FROM kv_store WHERE key_text = " + W.quote(key) + ";";
        W.exec(query);
        W.commit();

        kv_cache.erase(key);
        metrics.total_db_writes++;

        auto latency = duration_cast<milliseconds>(steady_clock::now() - start_time).count();
        double cur_avg = metrics.avg_db_write_latency_ms.load();
        metrics.avg_db_write_latency_ms.store((cur_avg + latency) / 2.0);

        rsp.status = 200;
        rsp.set_content("Deleted key: " + key, "text/plain");
    }
    catch (const exception &e)
    {
        rsp.status = 500;
        rsp.set_content(string("Database error: ") + e.what(), "text/plain");
    }
}

void handle_metrics(const httplib::Request &, httplib::Response &rsp)
{
    stringstream ss;
    ss << "Cache Hits: " << metrics.cache_hits.load() << "\n";
    ss << "Cache Misses: " << metrics.cache_misses.load() << "\n";
    ss << "Cache Hit Ratio: "
       << (metrics.total_gets.load() > 0
               ? (double)metrics.cache_hits.load() / metrics.total_gets.load() * 100.0
               : 0.0)
       << "%\n";
    ss << "Cache Size: " << kv_cache.size() << "/" << CACHE_MAX_SIZE << "\n";
    ss << "Total GETs: " << metrics.total_gets.load() << "\n";
    ss << "Total POSTs: " << metrics.total_posts.load() << "\n";
    ss << "Total DELETEs: " << metrics.total_deletes.load() << "\n";
    ss << "Total DB Reads: " << metrics.total_db_reads.load() << "\n";
    ss << "Total DB Writes: " << metrics.total_db_writes.load() << "\n";
    ss << "Avg DB Read Latency (ms): " << metrics.avg_db_read_latency_ms.load() << "\n";
    ss << "Avg DB Write Latency (ms): " << metrics.avg_db_write_latency_ms.load() << "\n";

    rsp.status = 200;
    rsp.set_content(ss.str(), "text/plain");
}

int main()
{
    try
    {
        pqxx::connection c1("host=127.0.0.1 port=5432 dbname=stressdb user=stressuser password=stresspass");
        pqxx::work txn1(c1);
        txn1.exec(R"(
            CREATE TABLE IF NOT EXISTS kv_store (
                key_text TEXT PRIMARY KEY,
                value_text TEXT
            );
        )");
        txn1.commit();
        std::cout << "Table created successfully!\n";
        pqxx::connection c2("host=127.0.0.1 port=5433 dbname=stressdb user=stressuser password=stresspass");
        pqxx::work txn2(c2);
        txn2.exec(R"(
            CREATE TABLE IF NOT EXISTS kv_store (
                key_text TEXT PRIMARY KEY,
                value_text TEXT
            );
        )");
        txn2.commit();
        std::cout << "Table created successfully!\n";
        pqxx::connection c3("host=127.0.0.1 port=5434 dbname=stressdb user=stressuser password=stresspass");
        pqxx::work txn3(c3);
        txn3.exec(R"(
            CREATE TABLE IF NOT EXISTS kv_store (
                key_text TEXT PRIMARY KEY,
                value_text TEXT
            );
        )");
        txn3.commit();
        std::cout << "Table created successfully!\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
    }

    httplib::Server svr;
    svr.set_keep_alive_timeout(0);

    svr.Get("/", [](const httplib::Request &, httplib::Response &rsp)
            { rsp.set_content("Synchronous KV Server with LRU Cache is running!", "text/plain"); });

    svr.Get("/metrics", handle_metrics);
    svr.Get(R"(/.*)", handle_get);
    svr.Post(R"(/.*)", handle_post);
    svr.Delete(R"(/.*)", handle_delete);

    cout << "Starting HTTP KV Server (sync DB I/O) on port 8081..." << endl;
    svr.new_task_queue = []
    {
        return new httplib::ThreadPool(200); // increase manually
    };

    svr.listen("0.0.0.0", 8080);
    return 0;
}