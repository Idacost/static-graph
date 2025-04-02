#include <iostream>
#include <string>
#include <queue>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>
#include <curl/curl.h>
#include <stdexcept>
#include "rapidjson/error/error.h"
#include "rapidjson/reader.h"
#include <rapidjson/document.h>
#include <chrono>
#include <pthread.h>
#include <mutex>

using namespace std;
using namespace rapidjson;

struct ParseException : std::runtime_error, rapidjson::ParseResult {
    ParseException(rapidjson::ParseErrorCode code, const char* msg, size_t offset) : 
        std::runtime_error(msg), 
        rapidjson::ParseResult(code, offset) {}
};

#define RAPIDJSON_PARSE_ERROR_NORETURN(code, offset) \
    throw ParseException(code, #code, offset)

bool debug = false;
const string SERVICE_URL = "http://hollywood-graph-crawler.bridgesuncc.org/neighbors/";
const int MAX_THREADS = 8;
mutex visited_mutex, next_level_mutex;

struct ThreadData {
    CURL* curl;
    vector<string>* current_level;
    vector<string>* next_level;
    unordered_set<string>* visited;
};

string url_encode(CURL* curl, string input) {
    char* out = curl_easy_escape(curl, input.c_str(), input.size());
    string s = out;
    curl_free(out);
    return s;
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

string fetch_neighbors(CURL* curl, const string& node) {
    string url = SERVICE_URL + url_encode(curl, node);
    string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: C++-Client/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    return (res == CURLE_OK) ? response : "{}";
}

vector<string> get_neighbors(const string& json_str) {
    vector<string> neighbors;
    try {
        Document doc;
        doc.Parse(json_str.c_str());
        if (doc.HasMember("neighbors") && doc["neighbors"].IsArray()) {
            for (const auto& neighbor : doc["neighbors"].GetArray())
                neighbors.push_back(neighbor.GetString());
        }
    } catch (const ParseException& e) {
        cerr << "Error while parsing JSON: " << json_str << endl;
        throw e;
    }
    return neighbors;
}

void* process_nodes(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    vector<string> local_next_level;
    
    for (const auto& node : *(data->current_level)) {
        string json = fetch_neighbors(data->curl, node);
        vector<string> neighbors = get_neighbors(json);
        
        for (const auto& neighbor : neighbors) {
            lock_guard<mutex> lock(visited_mutex);
            if (data->visited->insert(neighbor).second) {
                local_next_level.push_back(neighbor);
            }
        }
    }
    
    lock_guard<mutex> lock(next_level_mutex);
    data->next_level->insert(data->next_level->end(), local_next_level.begin(), local_next_level.end());
    
    return nullptr;
}

vector<vector<string>> bfs_parallel(CURL* curl, const string& start, int depth) {
    vector<vector<string>> levels;
    unordered_set<string> visited;
    
    levels.push_back({start});
    visited.insert(start);
    
    for (int d = 0; d < depth; d++) {
        vector<string> next_level;
        int num_nodes = levels[d].size();
        int num_threads = min(num_nodes, MAX_THREADS);
        
        vector<pthread_t> threads(num_threads);
        vector<ThreadData> thread_data(num_threads);
        int nodes_per_thread = (num_nodes + num_threads - 1) / num_threads;
        
        for (int i = 0; i < num_threads; i++) {
            int start_idx = i * nodes_per_thread;
            int end_idx = min(start_idx + nodes_per_thread, num_nodes);
            
            thread_data[i] = {curl, new vector<string>(levels[d].begin() + start_idx, levels[d].begin() + end_idx), &next_level, &visited};
            pthread_create(&threads[i], nullptr, process_nodes, &thread_data[i]);
        }
        
        for (int i = 0; i < num_threads; i++) {
            pthread_join(threads[i], nullptr);
            delete thread_data[i].current_level;
        }
        
        levels.push_back(move(next_level));
    }
    
    return levels;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <node_name> <depth>\n";
        return 1;
    }

    string start_node = argv[1];
    int depth;
    try {
        depth = stoi(argv[2]);
    } catch (const exception& e) {
        cerr << "Error: Depth must be an integer.\n";
        return 1;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        cerr << "Failed to initialize CURL" << endl;
        return -1;
    }

    const auto start = chrono::steady_clock::now();
    for (const auto& n : bfs_parallel(curl, start_node, depth)) {
        for (const auto& node : n)
            cout << "- " << node << "\n";
        cout << n.size() << "\n";
    }
    const auto finish = chrono::steady_clock::now();
    cout << "Time to crawl: " << chrono::duration<double>(finish - start).count() << "s\n";
    curl_easy_cleanup(curl);
    return 0;
}