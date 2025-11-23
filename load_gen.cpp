#include "httplib.h"
#include<iostream>
#include<thread>
#include<vector>
#include<atomic>
#include<chrono>
#include<fstream>


using namespace std;

atomic<long long> global_key_counter(0);

struct Stats {
    long long total_requests = 0;
    long long total_success = 0;
    long long total_fail = 0;
    long long total_latency_ns = 0;
};

enum Workload{
    PUT_ALL,
    GET_ALL,
    GET_POPULAR,
    GET_PUT
};

void worker(int id, int duration_sec, Workload type, Stats &stats){
    httplib::Client client("127.0.0.1",8080);
    client.set_keep_alive(true);

    auto end_time = chrono::steady_clock::now() + chrono::seconds(duration_sec);

    long long counter = 0;

    vector<string> popular_keys;
    for(int i = 0; i < 15; i++){
        popular_keys.push_back("popular_"+to_string(i));
    }

    while(chrono::steady_clock::now() < end_time){
        auto start = chrono::steady_clock::now();
        stats.total_requests++;

        bool ok = false;

        if(type == PUT_ALL){

            long long key_num = global_key_counter++;

            string key = "k_" + to_string(key_num);
            string value = "v_" + to_string(key_num);
            string body = key + " " + value;

            auto res = client.Post("/kvstore/create",body,"text/plain");
            ok = (res && (res->status == 201 || res->status == 409));
        }

        else if(type == GET_ALL){

            long long key_num = global_key_counter++;

            string key = "k_" + to_string(key_num);

            auto res = client.Get(("/kvstore/" + key).c_str());
            ok = (res && (res->status == 200 || res->status == 404));
        }

        else if(type == GET_POPULAR){

            int idx = counter % popular_keys.size();
            string key = popular_keys[idx];

            auto res = client.Get(("/kvstore/" + key).c_str());
            ok = (res && (res->status == 200 || res->status == 404));
        }

        else if(type == GET_PUT){

            if(counter % 2 == 0){

                long long key_num = global_key_counter++;

                string key = "k_" + to_string(key_num);
                string value = "v_" + to_string(key_num);
                string body = key + " " + value;

                auto res = client.Post("/kvstore/create",body,"text/plain");
                ok = (res && (res->status == 201 || res->status == 409));
            }else{

                long long key_num = (global_key_counter > 0) ? global_key_counter - 1: 0;

                string key = "k_" + to_string(key_num);

                auto res = client.Get(("/kvstore/" + key).c_str());
                ok = (res && (res->status == 200 || res->status == 404));
            }
        }

        auto end = chrono::steady_clock::now();
        stats.total_latency_ns += chrono::duration_cast<chrono::nanoseconds>(end-start).count();

        if(ok){
            stats.total_success++;
        }else{
            stats.total_fail++;
        }
        counter++;

    }
}

int main(){

    int threads,duration;
    string workload_str;

    cout<< "Enter the number of threads: ";
    cin>>threads;
    cout<<"Enter the duration (seconds): ";
    cin>>duration;
    cout<<"Enter workload type (put_all/get_all/get_popular/get_put): ";
    cin>>workload_str;

    Workload type;

    if(workload_str == "put_all"){
        type = PUT_ALL;
    }else if(workload_str == "get_all"){
        type = GET_ALL;
    }else if(workload_str == "get_popular"){
        type = GET_POPULAR;
    }else if(workload_str == "get_put"){
        type = GET_PUT;
    }else{
        cout << "Invalid workload. Use: put_all / get_all / get_popular / get_put\n";
        return 0;
    }

    if(type == GET_POPULAR){
        httplib::Client client("127.0.0.1",8080);
        client.set_keep_alive(true);

        cout<<"Adding 15 popular Keys..\n";
        for(int i = 0; i < 15; i++){
            string key = "popular_" + to_string(i);
            string value = "v_popular_" + to_string(i);
            string body = key + " " + value;

            auto res = client.Post("/kvstore/create",body,"text/plain");

            if(!res || (res->status != 201 && res->status != 409)){
                cout<<"Adding Failed"<<key<<endl;
            }
            
            client.Get(("/kvstore/" + key).c_str());
        }
    }

    vector<thread> workers;
    vector<Stats> stats(threads);

    for(int i = 0; i < threads; i++){
        workers.emplace_back(worker,i,duration,type,ref(stats[i]));
    }

    for(auto &t : workers){
        t.join();
    }


    long long total_req = 0;
    long long total_ok = 0;
    long double total_latency = 0;

    for(auto &s: stats){
        total_req += s.total_requests;
        total_ok += s.total_success;
        total_latency += s.total_latency_ns;
    }

    double avg_latency_ms = (total_latency/total_ok)/1e6;

    double throughput = (double) total_ok / duration;

    cout << "\nRESULTS\n";
    cout<<"Total Requests: "<<total_req <<endl;
    cout<<"Successful Requests: "<<total_ok<<endl;
    cout<<"Failed Requests:" << (total_req - total_ok) <<endl;
    cout<<"Average Troughput: "<< throughput <<"req/sec\n";
    cout<<"Average Response Time: "<<avg_latency_ms<<"ms\n";
    cout<<"\n";

    ofstream csv("results.csv", ios::app);

    if(csv.tellp() == 0){
        csv<< "threads,workload,total_requests,total_success,total_fail,throughput,avg_latency_ms\n";
    }

    csv <<threads << ","
        << workload_str << ","
        << total_req << ","
        << total_ok << ","
        << (total_req - total_ok) << ","
        << throughput << ","
        << avg_latency_ms << "\n";
    
    csv.close();

    return 0;
}