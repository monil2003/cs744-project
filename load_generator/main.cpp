// loader
#include <httplib.h>
#include <iostream>
#include <string>
#include <random>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

using namespace std;

atomic<bool> keep_running(true);
atomic<unsigned long> total_req(0);
atomic<unsigned long> total_resp_time(0);
atomic<int> v(1);
// static string big_value(1024*1024, 'X');   //To show mem full application not allowed;
void worker_thread(const string &w_type, int thread_id)
{
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> popular_dist(1, 10);
    uniform_int_distribution<> all_dist(1, 10000000);

    httplib::Client cli("localhost", 8080);
    // httplib::Client cli("host.docker.internal",8080);
    while (keep_running)
    {
        string key;
        httplib::Result res;
        chrono::steady_clock::time_point start_time;
        chrono::steady_clock::time_point end_time;

        if (w_type == "get_popular")
        {
            key = "key-" + to_string((thread_id % 10) + 1);
            string path = "/" + key;
            start_time = chrono::steady_clock::now();
            res = cli.Get(path.c_str());
            end_time = chrono::steady_clock::now();
        }
        else if (w_type == "get_all")
        {
            key = "key-" + to_string(all_dist(gen));
            string path = "/" + key;
            start_time = chrono::steady_clock::now();
            res = cli.Get(path.c_str());
            end_time = chrono::steady_clock::now();
        }
        else if (w_type == "put_all")
        {
            // Generate a unique key
            // key = "key-" + to_string(v.fetch_add(1));
            // string big_value(1024 * 1024 * 1024, 'X');
            key = "key-" + to_string(all_dist(gen));
            string path = "/" + key;
            start_time = chrono::steady_clock::now();
            res = cli.Post(path.c_str(),key, "text/plain");
            // res = cli.Post(path.c_str(), key, "text/plain");
            // cout<<key;
            end_time = chrono::steady_clock::now();
            // string().swap(big_value);
        }

        if (!keep_running)
            break;

        if (res && (res->status == 200 || res->status == 201 || res->status == 404 || res->status == 500))
        {
            auto duration_ms = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
            total_req++;
            total_resp_time += static_cast<unsigned long>(duration_ms);
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        cerr << "Usage: ./loader <num_threads> <duration_second> <w_type>" << endl;
        cerr << "Workload types: get_popular,get_all,put_all" << endl;
        return 1;
    }

    unsigned long num_threads = stoul(argv[1]);
    unsigned long duration_seconds = stoul(argv[2]);
    string w_type = argv[3];

    if (w_type != "get_popular" && w_type != "get_all" && w_type != "put_all")
    {
        cerr << "Invalid workload type. Use 'get_popular', 'get_all', or 'put_all'." << endl;
        return 1;
    }

    cout << "Starting load test..." << endl;
    cout << "Threads: " << num_threads << endl;
    cout << "Duration: " << duration_seconds << " seconds" << endl;
    cout << "Workload: " << w_type << endl;

    vector<thread> threads;

    for (unsigned long i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(worker_thread, w_type, static_cast<int>(i));
    }

    this_thread::sleep_for(chrono::seconds(duration_seconds));

    keep_running = false;
    cout << "\nStopping all worker threads and calculating metrics..." << endl;

    for (auto &t : threads)
    {
        if (t.joinable())
            t.join();
    }

    cout << "\nTest complete." << endl;
    cout << "------------------------------------------------------" << endl;

    unsigned long total_reqs = total_req.load();
    unsigned long total_time_ms = total_resp_time.load();

    if (total_reqs == 0)
    {
        cout << "No requests were completed" << endl;
        return 0;
    }

    double throughput = static_cast<double>(total_reqs) / static_cast<double>(duration_seconds);
    double avg_response_time_ms = static_cast<double>(total_time_ms) / static_cast<double>(total_reqs);

    cout << "Total Requests Completed:    " << total_reqs << endl;
    cout << "Average Throughput:          " << throughput << " reqs/sec" << endl;
    cout << "Average Response Time:       " << avg_response_time_ms << " ms" << endl;

    return 0;
}
