#include<unordered_map>
#include<list>
#include<mutex>
#include<string>


class LRUCache{

    private:
        size_t capacity;
        std::list<std::pair<std::string,std::string>> cache_list; //for maintaining LRU
        std::unordered_map<std::string,std::list<std::pair<std::string,std::string>>::iterator> cache_map;
        mutable std::mutex mtx;

    public:
        LRUCache(size_t capacity): capacity(capacity){}

        //read operation
        std::string read_key(const std::string &key){
            std::lock_guard<std::mutex> lock(mtx);
            auto find_key = cache_map.find(key);
            if(find_key == cache_map.end()){
                return "";
            }
            cache_list.splice(cache_list.begin(),cache_list,find_key->second);
            return find_key->second->second;
        }

        //create key value pair
        int create_key(const std::string &key, const std::string &value){
            std::lock_guard<std::mutex> lock(mtx);
            auto find_key = cache_map.find(key);

            if(find_key != cache_map.end()){
                //key already exists
                return -1;
            }
            //key does not exist
            else{
                //if list size is greater than capacity
                if(cache_list.size() >= capacity){
                    auto lru = cache_list.back();
                    cache_map.erase(lru.first);
                    cache_list.pop_back();
                }

                cache_list.emplace_front(key,value);
                cache_map[key] = cache_list.begin();
            }
            return 0;
        }

        //delete key
        int delete_key(const std::string &key){
            std::lock_guard<std::mutex> lock(mtx);
            auto find_key = cache_map.find(key);

            if(find_key != cache_map.end()){
                cache_list.erase(find_key -> second);
                cache_map.erase(find_key);
                return 0;
            }
            return -1;
        }
};