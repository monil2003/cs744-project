#include <iostream>
#include <httplib.h>
#include <pqxx/pqxx>
#include <string>
#include <unordered_map>
#include <list>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <sstream>
#include <memory>
#include <thread>
#include <functional>
#include <queue>
#include <condition_variable>

// Lock-free concurrent queue (from moodycamel library — header-only, minimal)
#include "concurrentqueue.h" // Add: sudo dnf install concurrentqueue-devel  OR include header file manually

using namespace std;
using namespace std::chrono;
using pqxx::connection;
using pqxx::work;
using pqxx::result;
using std::make_unique;
using std::shared_lock;
using std::unique_lock;
using std::shared_mutex;

std::string pattern = "xyz";
std::string s;

// ==================== CONFIGURATION ====================
const size_t CACHE_MAX_SIZE = 100;
const size_t NUM_DB_WRITERS = 32;      // Number of concurrent DB writer threads
const size_t BATCH_SIZE = 30;         // Batch size per writer
const char *DB_CONN =
    "host=localhost port=5433 "
    "dbname=postgres "
    "user=postgres "
    "password=mysecretpassword";

// ==================== THREAD-LOCAL CONNECTIONS ====================
thread_local std::unique_ptr<pqxx::connection> thread_conn_ptr;
thread_local std::unique_ptr<pqxx::connection> thread_conn_writer_ptr;

pqxx::connection &get_thread_conn() {
    if (!thread_conn_ptr || !thread_conn_ptr->is_open()) {
        thread_conn_ptr = make_unique<pqxx::connection>(DB_CONN);
    }
    return *thread_conn_ptr;
}

pqxx::connection &get_thread_conn_writer() {
    if (!thread_conn_writer_ptr || !thread_conn_writer_ptr->is_open()) {
        thread_conn_writer_ptr = make_unique<pqxx::connection>(DB_CONN);
    }
    return *thread_conn_writer_ptr;
}

// ==================== METRICS STRUCT ====================
struct Metrics {
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

// ==================== LRU CACHE CLASS ====================
class LRUCache {
public:
    LRUCache(size_t capacity) : max_size(capacity) {}

    bool get(const string &key, string &value) {
        shared_lock<shared_mutex> read_lock(lock);
        auto it = map_kv.find(key);
        if (it == map_kv.end()) {
            metrics.cache_misses++;
            return false;
        }
        value = it->second.first;
        read_lock.unlock();

        unique_lock<shared_mutex> write_lock(lock);
        it = map_kv.find(key);
        if (it != map_kv.end()) {
            list_kv.splice(list_kv.begin(), list_kv, it->second.second);
            metrics.cache_hits++;
        } else {
            metrics.cache_misses++;
            return false;
        }
        return true;
    }

    void put(const string &key, const string &value) {
        unique_lock<shared_mutex> guard(lock);
        auto it = map_kv.find(key);
        if (it != map_kv.end()) {
            it->second.first = value;
            list_kv.splice(list_kv.begin(), list_kv, it->second.second);
        } else {
            list_kv.push_front(key);
            map_kv[key] = {value, list_kv.begin()};
            if (map_kv.size() > max_size) {
                string lru_key = list_kv.back();
                list_kv.pop_back();
                map_kv.erase(lru_key);
            }
        }
    }

    void erase(const string &key) {
        unique_lock<shared_mutex> guard(lock);
        auto it = map_kv.find(key);
        if (it != map_kv.end()) {
            list_kv.erase(it->second.second);
            map_kv.erase(it);
        }
    }

    size_t size() {
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

// ==================== WRITE REQUEST & QUEUE ====================
struct WriteRequest {
    string key;
    string value;
};

moodycamel::ConcurrentQueue<WriteRequest> write_queue;
atomic<bool> stop_writer{false};

// ==================== DB WRITER LOOP ====================
void db_writer_loop(int id) {
    vector<WriteRequest> batch;
    batch.reserve(BATCH_SIZE);

    while (!stop_writer) {
        batch.clear();
        WriteRequest req;

        // Drain up to BATCH_SIZE requests
        for (size_t i = 0; i < BATCH_SIZE && write_queue.try_dequeue(req); ++i) {
            batch.push_back(std::move(req));
        }

        if (!batch.empty()) {
            auto start_time = steady_clock::now();
            try {
                work W(get_thread_conn_writer());
                stringstream query;
                query << "INSERT INTO kv_store (key_text, value_text) VALUES ";
                for (size_t i = 0; i < batch.size(); ++i) {
                    query << "(" << W.quote(batch[i].key) << ", " << W.quote(s) << ")";
                    if (i + 1 != batch.size()) query << ",";
                }
                query << " ON CONFLICT (key_text) DO UPDATE SET value_text = EXCLUDED.value_text;";

                W.exec(query.str());
                W.commit();
                metrics.total_db_writes += batch.size();

                auto latency = duration_cast<milliseconds>(steady_clock::now() - start_time).count();
                double cur_avg = metrics.avg_db_write_latency_ms.load();
                metrics.avg_db_write_latency_ms.store((cur_avg + latency) / 2.0);
            } catch (const exception &e) {
                cerr << "[Writer " << id << "] DB error: " << e.what() << endl;
            }
        } else {
            this_thread::sleep_for(5ms);
        }
    }

    cerr << "[Writer " << id << "] Exiting.\n";
}

// ==================== HTTP HANDLERS ====================
string path_to_key(const httplib::Request &req) {
    string p = req.path;
    if (!p.empty() && p[0] == '/') return p.substr(1);
    return p;
}

void handle_get(const httplib::Request &req, httplib::Response &rsp) {
    metrics.total_gets++;
    string key = path_to_key(req);
    if (key.empty()) {
        rsp.status = 400; rsp.set_content("Bad Request: empty key", "text/plain"); return;
    }

    string value;
    if (kv_cache.get(key, value)) {
        rsp.status = 200;
        rsp.set_content(value, "text/plain");
        return;
    }

    auto start_time = steady_clock::now();
    try {
        work W(get_thread_conn());
        string query = "SELECT value_text FROM kv_store WHERE key_text = " + W.quote(key) + ";";
        result R = W.exec(query);
        W.commit();

        metrics.total_db_reads++;
        auto latency = duration_cast<milliseconds>(steady_clock::now() - start_time).count();
        double cur_avg = metrics.avg_db_read_latency_ms.load();
        metrics.avg_db_read_latency_ms.store((cur_avg + latency) / 2.0);

        if (R.empty()) {
            rsp.status = 404;
            rsp.set_content("Key Not Found", "text/plain");
        } else {
            string val = R[0][0].as<string>();
            kv_cache.put(key, val);
            rsp.status = 200;
            rsp.set_content(val, "text/plain");
        }
    } catch (const exception &e) {
        rsp.status = 500;
        rsp.set_content(string("Database error: ") + e.what(), "text/plain");
    }
}

void handle_post(const httplib::Request &req, httplib::Response &rsp) {
    metrics.total_posts++;
    string key = path_to_key(req);
    if (key.empty()) {
        rsp.status = 400; rsp.set_content("Bad Request: empty key", "text/plain"); return;
    }

    string value = req.body;
    // kv_cache.put(key, value);
    write_queue.enqueue({key, value}); // lock-free enqueue

    rsp.status = 201;
    rsp.set_content("Accepted key " + key, "text/plain");
}

void handle_delete(const httplib::Request &req, httplib::Response &rsp) {
    metrics.total_deletes++;
    string key = path_to_key(req);
    if (key.empty()) {
        rsp.status = 400; rsp.set_content("Bad Request: empty key", "text/plain"); return;
    }

    try {
        work W(get_thread_conn());
        string query = "DELETE FROM kv_store WHERE key_text = " + W.quote(key) + ";";
        W.exec(query);
        W.commit();
        kv_cache.erase(key);

        rsp.status = 200;
        rsp.set_content("Deleted key: " + key, "text/plain");
    } catch (const exception &e) {
        rsp.status = 500;
        rsp.set_content(string("Database error: ") + e.what(), "text/plain");
    }
}

void handle_metrics(const httplib::Request &, httplib::Response &rsp) {
    stringstream ss;
    ss << "Cache Hits: " << metrics.cache_hits.load() << "\n";
    ss << "Cache Misses: " << metrics.cache_misses.load() << "\n";
    ss << "Cache Hit Ratio: " 
       << (metrics.total_gets.load() > 0 
           ? (double)metrics.cache_hits.load() / metrics.total_gets.load() * 100.0 
           : 0.0) << "%\n";
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

// ==================== MAIN ====================
int main() {
    s.reserve(1000); 
    for (int i = 0; i < 1000; i += pattern.length()) {
        s += pattern;
    }
    s.resize(1000); 

    httplib::Server svr;
    svr.set_keep_alive_timeout(0);

    // Start multiple DB writer threads
    vector<thread> writer_threads;
    for (size_t i = 0; i < NUM_DB_WRITERS; ++i)
        writer_threads.emplace_back(db_writer_loop, i);
    cout << "Started " << NUM_DB_WRITERS << " DB writer threads.\n";

    svr.Get("/", [](const httplib::Request &, httplib::Response &rsp) {
        rsp.set_content("Async KV Server with Multi-Threaded Lock-Free Writers Running!", "text/plain");
    });

    svr.Get("/metrics", handle_metrics);
    svr.Get(R"(/.*)", handle_get);
    svr.Post(R"(/.*)", handle_post);
    svr.Delete(R"(/.*)", handle_delete);

    svr.new_task_queue = [] { return new httplib::ThreadPool(300); };

    cout << "Starting HTTP KV Server on port 8080..." << endl;
    svr.listen("0.0.0.0", 8080);

    // Cleanup
    cout << "Server shutting down..." << endl;
    stop_writer = true;
    for (auto &t : writer_threads)
        if (t.joinable()) t.join();

    cout << "All DB writer threads joined. Exiting.\n";
    return 0;
}
