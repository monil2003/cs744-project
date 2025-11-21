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
atomic<unsigned long> total_success(0);
atomic<unsigned long> total_errors(0);
atomic<unsigned long> total_resp_time(0);
atomic<int> v(1);

void worker_thread(const string &w_type, int thread_id)
{
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> popular_dist(1, 100);
    uniform_int_distribution<> all_dist(1, 10000000);

    httplib::Client cli("localhost", 8080);

    unsigned long local_success = 0;
    unsigned long local_error = 0;
    unsigned long local_time = 0;

    while (keep_running)
    {
        string key;
        httplib::Result res;

        chrono::steady_clock::time_point start_time;
        chrono::steady_clock::time_point end_time;

        if (w_type == "get_popular1")
        {
            key = "key-" + to_string((thread_id % 10));
            string path = "/" + key;
            start_time = chrono::steady_clock::now();
            res = cli.Get(path.c_str());
            end_time = chrono::steady_clock::now();
        }
        else if (w_type == "get_popular2")
        {
            key = "key-" + to_string(popular_dist(gen));
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
            key = "key-" + to_string(all_dist(gen));
            string path = "/" + key;
            start_time = chrono::steady_clock::now();
            res = cli.Post(path.c_str(), key, "text/plain");
            end_time = chrono::steady_clock::now();
        }
        else if (w_type == "put_all_big")
        {
            string big_value(2 * 10 * 1024, 'X');
            key = "key-" + to_string(v.fetch_add(1));
            string path = "/" + key;
            start_time = chrono::steady_clock::now();
            res = cli.Post(path.c_str(), big_value, "text/plain");
            end_time = chrono::steady_clock::now();
        }
        else if (w_type == "put_key_1")
        {
            key = "key-1";
            string path = "/" + key;
            start_time = chrono::steady_clock::now();
            res = cli.Post(path.c_str(), key, "text/plain");
            end_time = chrono::steady_clock::now();
        }
        else if (w_type == "heating")
        {
            for (int i = 0; i < 10; i++)
            {
                key = "key-" + to_string(i);
                string path = "/" + key;
                cli.Post(path.c_str(), key, "text/plain");
            }
            return;
        }
        else if (w_type == "mixed")
        {
            int op_type = all_dist(gen) % 10;
            if (op_type < 4)
            {
                key = "key-" + to_string((thread_id % 10) + 1);
                string path = "/" + key;
                start_time = chrono::steady_clock::now();
                res = cli.Get(path.c_str());
                end_time = chrono::steady_clock::now();
            }
            else if (op_type < 7)
            {
                key = "key-" + to_string(all_dist(gen));
                string path = "/" + key;
                start_time = chrono::steady_clock::now();
                res = cli.Get(path.c_str());
                end_time = chrono::steady_clock::now();
            }
            else
            {
                key = "key-" + to_string(all_dist(gen));
                string path = "/" + key;
                start_time = chrono::steady_clock::now();
                res = cli.Post(path.c_str(), key, "text/plain");
                end_time = chrono::steady_clock::now();
            }
        }

        if (!keep_running)
            break;

        if (!res)
        {
            local_error++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto duration_ms = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();

        if (res->status == 200 || res->status == 201 || res->status == 404)
        {
            local_success++;
            local_time += static_cast<unsigned long>(duration_ms);
        }
        else
        {
            local_error++;
            local_time += static_cast<unsigned long>(duration_ms);
        }
    }

    total_success += local_success;
    total_errors += local_error;
    total_resp_time += local_time;
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        cerr << "Usage: ./loader <num_threads> <duration_second> <w_type>" << endl;
        cerr << "Workload types: get_popular1, get_popular2, get_all, put_all, put_all_big, put_key_1, mixed, heating" << endl;
        return 1;
    }

    unsigned long num_threads = stoul(argv[1]);
    unsigned long duration_seconds = stoul(argv[2]);
    string w_type = argv[3];

    if (w_type != "get_popular1" && w_type != "get_popular2" && w_type != "get_all" && w_type != "put_all" && w_type != "heating" && w_type != "put_all_big" && w_type != "mixed" && w_type != "put_key_1")
    {
        cerr << "Invalid workload type." << endl;
        return 1;
    }

    cout << "Starting load test..." << endl;
    cout << "Threads:  " << num_threads << endl;
    cout << "Duration: " << duration_seconds << " seconds" << endl;
    cout << "Workload: " << w_type << endl;

    vector<thread> threads;

    for (unsigned long i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(worker_thread, w_type, static_cast<int>(i));
    }

    this_thread::sleep_for(chrono::seconds(duration_seconds));

    keep_running = false;
    cout << "\nStopping all worker threads..." << endl;

    for (auto &t : threads)
    {
        if (t.joinable())
            t.join();
    }

    cout << "Calculating metrics..." << endl;
    cout << "------------------------------------------------------" << endl;

    unsigned long t_success = total_success.load();
    unsigned long t_errors = total_errors.load();
    unsigned long total_reqs = t_success + t_errors;
    unsigned long total_time_ms = total_resp_time.load();

    if (total_reqs == 0)
    {
        cout << "No requests were completed" << endl;
        return 0;
    }

    double throughput = static_cast<double>(total_reqs) / static_cast<double>(duration_seconds);
    double avg_response_time_ms = (total_reqs > 0) ? static_cast<double>(total_time_ms) / static_cast<double>(total_reqs) : 0.0;
    double error_rate = (static_cast<double>(t_errors) / static_cast<double>(total_reqs)) * 100.0;

    cout << "Total Requests (All):      " << total_reqs << endl;
    cout << "Successful (2xx/404):      " << t_success << endl;
    cout << "Failed (Conn/5xx/400):     " << t_errors << endl;
    cout << "Error Rate:                " << error_rate << " %" << endl;
    cout << "Average Throughput:        " << throughput << " reqs/sec" << endl;
    cout << "Average Response Time:     " << avg_response_time_ms << " ms" << endl;

    return 0;
}