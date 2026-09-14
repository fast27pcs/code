#include "client.h"
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <chrono>

using namespace ECProject;

std::string get_scheme_name(ECProject::PlacementRule rule) {
    switch (rule) {
        case ECProject::PlacementRule::ABLOCK:       return "ABLOCK";
        case ECProject::PlacementRule::ACOL:       return "ACOL";
        case ECProject::PlacementRule::AROW:       return "AROW";
        case ECProject::PlacementRule::M1COL:     return "M1COL";
        case ECProject::PlacementRule::M2ROW:     return "M2ROW";
        case ECProject::PlacementRule::TILE:       return "TILE";
        case ECProject::PlacementRule::DIAGONAL:   return "DIAGONAL";
        default: return "SCHEME_" + std::to_string((int)rule);
    }
}

void test_single_block_repair(Client &client, int block_num)
{
  auto stripe_ids = client.list_stripes();
  int stripe_num = stripe_ids.size();
  std::vector<double> repair_times;
  std::vector<double> decoding_times;
  std::vector<double> cross_cluster_times;
  std::vector<double> meta_times;
  std::vector<int> cross_cluster_transfers;
  std::vector<int> io_cnts;
  std::cout << "Single-Block Repair:" << std::endl;
  for (int i = 0; i < stripe_num; i++)
  {
    std::cout << "[Stripe " << i << "]" << std::endl;
    double temp_repair = 0;
    double temp_decoding = 0;
    double temp_cross_cluster = 0;
    double temp_meta = 0;
    int temp_cc_transfers = 0;
    int temp_io_cnt = 0;
    for (int j = 0; j < block_num; j++)
    {
      std::vector<unsigned int> failures;
      failures.push_back((unsigned int)j);
      auto resp = client.blocks_repair(failures, stripe_ids[i]);
      temp_repair += resp.repair_time;
      temp_decoding += resp.decoding_time;
      temp_cross_cluster += resp.cross_cluster_time;
      temp_meta += resp.meta_time;
      temp_cc_transfers += resp.cross_cluster_transfers;
      temp_io_cnt += resp.io_cnt;
    }
    repair_times.push_back(temp_repair);
    decoding_times.push_back(temp_decoding);
    cross_cluster_times.push_back(temp_cross_cluster);
    meta_times.push_back(temp_meta);
    cross_cluster_transfers.push_back(temp_cc_transfers);
    io_cnts.push_back(temp_io_cnt);
    std::cout << "repair = " << temp_repair / block_num
              << "s, decoding = " << temp_decoding / block_num
              << "s, cross-cluster = " << temp_cross_cluster / block_num
              << "s, meta = " << temp_meta / block_num
              << "s, cross-cluster-count = " << (double)temp_cc_transfers / block_num
              << ", I/Os = " << temp_io_cnt / block_num
              << std::endl;
  }
  auto avg_repair = std::accumulate(repair_times.begin(),
                                    repair_times.end(), 0.0) /
                    (stripe_num * block_num);
  auto avg_decoding = std::accumulate(decoding_times.begin(),
                                      decoding_times.end(), 0.0) /
                      (stripe_num * block_num);
  auto avg_cross_cluster = std::accumulate(cross_cluster_times.begin(),
                                           cross_cluster_times.end(), 0.0) /
                           (stripe_num * block_num);
  auto avg_meta = std::accumulate(meta_times.begin(),
                                  meta_times.end(), 0.0) /
                  (stripe_num * block_num);
  auto avg_cc_transfers = (double)std::accumulate(cross_cluster_transfers.begin(),
                                                  cross_cluster_transfers.end(), 0) /
                          (stripe_num * block_num);
  auto avg_io_cnt = (double)std::accumulate(io_cnts.begin(),
                                            io_cnts.end(), 0) /
                    (stripe_num * block_num);
  std::cout << "^-^[Average]^-^" << std::endl;
  std::cout << "repair = " << avg_repair << "s, decoding = " << avg_decoding
            << "s, cross-cluster = " << avg_cross_cluster
            << "s, meta = " << avg_meta
            << "s, cross-cluster-count = " << avg_cc_transfers
            << ", I/Os = " << avg_io_cnt
            << std::endl;
}

// 
long long nCr(int n, int k) {
    if (k > n) return 0;
    if (k * 2 > n) k = n - k;
    if (k == 0) return 1;
    long long result = n;
    for (int i = 2; i <= k; ++i) {
        result *= (n - i + 1);
        result /= i;
    }
    return result;
}

// 
void generate_all_combinations(int n, int k, std::vector<std::vector<int>>& all_combs) {
    std::string bitmask(k, 1); 
    bitmask.resize(n, 0);      
    do {
        std::vector<int> curr;
        for (int i = 0; i < n; ++i) {
            if (bitmask[i]) curr.push_back(i);
        }
        all_combs.push_back(curr);
    } while (std::prev_permutation(bitmask.begin(), bitmask.end()));
}

// 
// test_mode: 0 = Auto, 1 = Force Exhaustive, 2 = Force Random
void test_multiple_blocks_repair_pc(Client &client, int block_num, const ParametersInfo &paras, int failed_num, int test_mode = 1)
{
    auto stripe_ids = client.list_stripes();
    int stripe_num = stripe_ids.size();
    
    std::vector<double> repair_times, decoding_times, cross_cluster_times, meta_times;
    std::vector<int> cross_cluster_transfers, io_cnts;
    int tot_cnt = 0;
    
    ErasureCode *ec = ec_factory(paras.ec_type, paras.cp);
    std::cout << "Multi-Block Repair (e=" << failed_num << ") for " << ec->self_information() << std::endl;

    long long total_combinations = nCr(block_num, failed_num);
    int MAX_EXHAUSTIVE_LIMIT = 2000; 
    
    bool is_exhaustive = false;
    if (test_mode == 1) {
        is_exhaustive = true; 
        std::cout << "-> [MODE: FORCE EXHAUSTIVE] Total Space: " << total_combinations << std::endl;
    } else if (test_mode == 2) {
        is_exhaustive = false; 
        std::cout << "-> [MODE: FORCE RANDOM] Total Space: " << total_combinations << std::endl;
    } else {
        is_exhaustive = (total_combinations <= MAX_EXHAUSTIVE_LIMIT);
        std::cout << "-> [MODE: AUTO " << (is_exhaustive ? "EXHAUSTIVE" : "RANDOM") 
                  << "] Total Space: " << total_combinations << std::endl;
    }
    
    std::vector<std::vector<int>> target_combinations;
    if (is_exhaustive) {
        generate_all_combinations(block_num, failed_num, target_combinations);
    }

    // 
    long long global_total_sampled = 0;
    long long global_undecodable_cnt = 0;

    for (int i = 0; i < stripe_num; i++)
    {
      //  std::cout << "[Stripe " << i << "]" << std::endl;
        double temp_repair = 0, temp_decoding = 0, temp_cross_cluster = 0, temp_meta = 0;
        int temp_cc_transfers = 0, temp_io_cnt = 0;
        int valid_cnt = 0;

        // 当前 Stripe 的统计
        long long stripe_total_sampled = 0;
        long long stripe_undecodable_cnt = 0;


        if (is_exhaustive) {    
            // ============  ============
            for (const auto& failed_blocks : target_combinations) {
                stripe_total_sampled++;
                // 【核心追踪】：记录不可修数量
                if (!ec->check_if_decodable(failed_blocks)) {
                    stripe_undecodable_cnt++;
                    continue;
                }
                
                std::vector<unsigned int> failures(failed_blocks.begin(), failed_blocks.end());
                auto resp = client.blocks_repair(failures, stripe_ids[i]);
                
                if (resp.success) {
                    temp_repair += resp.repair_time; temp_decoding += resp.decoding_time;
                    temp_cross_cluster += resp.cross_cluster_time; temp_meta += resp.meta_time;
                    temp_cc_transfers += resp.cross_cluster_transfers; temp_io_cnt += resp.io_cnt;
                    valid_cnt++;
                    
                    repair_times.push_back(resp.repair_time); decoding_times.push_back(resp.decoding_time);
                    cross_cluster_times.push_back(resp.cross_cluster_time); meta_times.push_back(resp.meta_time);
                    cross_cluster_transfers.push_back(resp.cross_cluster_transfers); io_cnts.push_back(resp.io_cnt);
                }
            }
        } else {
            // ============  ============
            int target_valid_cases = 10; //

            if (total_combinations < target_valid_cases) {
                target_valid_cases = (int)total_combinations;
            }
            
           
            std::mt19937 stripe_rng(2026 + i);

            while (valid_cnt < target_valid_cases) {
                stripe_total_sampled++;
                
                std::vector<int> failed_blocks;
                random_n_num_deterministic(0, block_num - 1, failed_num, failed_blocks, stripe_rng);
                
                // 
                if (!ec->check_if_decodable(failed_blocks)) {
                    stripe_undecodable_cnt++;
                    continue;
                }
                
                std::vector<unsigned int> failures(failed_blocks.begin(), failed_blocks.end());
                auto resp = client.blocks_repair(failures, stripe_ids[i]);
                
                if (resp.success) {
                    temp_repair += resp.repair_time; temp_decoding += resp.decoding_time;
                    temp_cross_cluster += resp.cross_cluster_time; temp_meta += resp.meta_time;
                    temp_cc_transfers += resp.cross_cluster_transfers; temp_io_cnt += resp.io_cnt;
                    valid_cnt++;

                    repair_times.push_back(resp.repair_time); decoding_times.push_back(resp.decoding_time);
                    cross_cluster_times.push_back(resp.cross_cluster_time); meta_times.push_back(resp.meta_time);
                    cross_cluster_transfers.push_back(resp.cross_cluster_transfers); io_cnts.push_back(resp.io_cnt);
                }
                
                // 
                if (stripe_total_sampled > target_valid_cases * 20) {
                    std::cout << "   [WARNING] Too many undecodable cases. Aborting sampling for this stripe to prevent infinite loop." << std::endl;
                    break;
                }
            }
        }

        global_total_sampled += stripe_total_sampled;
        global_undecodable_cnt += stripe_undecodable_cnt;

        if (valid_cnt > 0) {
//            repair_times.push_back(temp_repair); decoding_times.push_back(temp_decoding);
//            cross_cluster_times.push_back(temp_cross_cluster); meta_times.push_back(temp_meta);
//            cross_cluster_transfers.push_back(temp_cc_transfers); io_cnts.push_back(temp_io_cnt);
            
          //  double undecodable_ratio = (double)stripe_undecodable_cnt / stripe_total_sampled * 100.0;
            
          //  std::cout << "   Valid cases = " << valid_cnt 
          //            << " | Undecodable cases = " << stripe_undecodable_cnt
          //            << " | Undecodable Ratio = " << undecodable_ratio << "%"
          //            << "\n   Avg repair = " << temp_repair / valid_cnt << "s"
          //            << ", CC-transfer count = " << (double)temp_cc_transfers / valid_cnt << std::endl;
            tot_cnt += valid_cnt;
          std::cout << "[Stripe " << i << " Average] "
              << "repair = " << temp_repair / valid_cnt << "s, "
              << "decoding = " << temp_decoding / valid_cnt << "s, "
              << "cross-cluster = " << temp_cross_cluster / valid_cnt << "s, "
              << "meta = " << temp_meta / valid_cnt << "s, "
              << "cross-cluster-count = " << (double)temp_cc_transfers / valid_cnt << ", "
              << "I/Os = " << (double)temp_io_cnt / valid_cnt 
              << std::endl;
        }
    }
    
    // 汇总输出逻辑
    auto avg_repair = std::accumulate(repair_times.begin(), repair_times.end(), 0.0) / tot_cnt;
    auto avg_decoding = std::accumulate(decoding_times.begin(), decoding_times.end(), 0.0) / tot_cnt;
    auto avg_cross_cluster = std::accumulate(cross_cluster_times.begin(), cross_cluster_times.end(), 0.0) / tot_cnt;
    auto avg_meta = std::accumulate(meta_times.begin(), meta_times.end(), 0.0) / tot_cnt;
    auto avg_cc_transfers = (double)std::accumulate(cross_cluster_transfers.begin(), cross_cluster_transfers.end(), 0) / tot_cnt;
    auto avg_io_cnt = (double)std::accumulate(io_cnts.begin(), io_cnts.end(), 0) / tot_cnt;
    
    double global_undecodable_ratio = (double)global_undecodable_cnt / global_total_sampled * 100.0;

    std::cout << "\n^-^[Global Average & Fault Tolerance]^-^" << std::endl;
    std::cout << ">>> OVERALL UNDECODABLE RATIO (Data Loss Rate): " << global_undecodable_ratio << "%" << " (" << global_undecodable_cnt << "/" << global_total_sampled << ")" << std::endl;
    std::cout << "repair = " << avg_repair << "s, decoding = " << avg_decoding
              << "s, cross-cluster = " << avg_cross_cluster
              << "s, meta = " << avg_meta
              << "s, cross-cluster-count = " << avg_cc_transfers
              << ", I/Os = " << avg_io_cnt << std::endl;
              

    std::string scheme_name = ec->self_information();
    std::string p_rule = get_scheme_name(paras.placement_rule);
    std::string filename = "res/cdf_" + scheme_name + p_rule + "_f" + std::to_string(failed_num) + ".csv";
    std::ofstream cdf_file(filename);
    
    if (cdf_file.is_open()) {
        cdf_file << "RepairTime_s\n";
        for (double rt : repair_times) {
            cdf_file << rt << "\n";
        }
        cdf_file.close();
        std::cout << ">>> CDF raw data exported to " << filename << std::endl;
    }

    if (ec != nullptr) {
        delete ec;
    }
}



// ==========================================
//  (Normal Read Throughput)
// ==========================================
void test_normal_read_throughput(Client &client, int stripe_num, const ParametersInfo &paras, 
                                 const std::vector<std::vector<std::string>> &ms_object_keys)
{
    std::cout << "\n--- [Normal Read Throughput Test] ---" << std::endl;
    
    double get_time = 0.0;
    size_t total_read_bytes = 0; 
    

    auto start = std::chrono::steady_clock::now();

    // 
    for (int i = 0; i < stripe_num; i++)
    {
        for (int j = 0; j < paras.cp.k; j++)
        {
            auto value = client.get(ms_object_keys[i][j]);
            
            // 
            total_read_bytes += value.size(); 
        }
    }

    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    get_time = elapsed.count();

    // Bytes -> MB
    double total_read_mb = (double)total_read_bytes / (1024.0 * 1024.0);
    
    std::cout << "Total read bytes: " << total_read_bytes << " Bytes" << std::endl;
    std::cout << "Total get time: " << get_time << " s, average get time: "
              << get_time / (stripe_num * paras.cp.k) << " s" << std::endl;
    std::cout << ">>> Read Throughput: " << total_read_mb / get_time << " MB/s <<<\n" << std::endl;
}

/*
// 
void test_foreground_interference(Client &client, const std::vector<std::vector<std::string>> &ms_object_keys, 
                                  int stripe_num, const ParametersInfo &paras, int failed_num) 
{   
    std::cout << "\n=========================================" << std::endl;
    std::cout << "  FOREGROUND INTERFERENCE TEST " << std::endl;
    std::cout << "  Scheme: " << paras.placement_rule << " | Failed: " << failed_num << std::endl;
    std::cout << "=========================================\n" << std::endl;

    std::atomic<bool> keep_running(true);
    std::atomic<int> current_iops(0);

    // 
    std::vector<std::string> all_keys;
    for (const auto& keys : ms_object_keys) {
        for (const auto& k : keys) {
            all_keys.push_back(k);
        }
    }
    int total_keys = all_keys.size();

    // 
    auto foreground_worker = [&](int thread_id) {
        int unique_client_port = CLIENT_PORT + thread_id + 1;
        Client local_client("127.0.0.1", unique_client_port, "127.0.0.1", COORDINATOR_PORT);
        
        // 
        std::mt19937 rng(std::random_device{}() + thread_id);
        std::uniform_int_distribution<int> dist(0, total_keys - 1);

        while (keep_running) {
            int rand_idx = dist(rng);
            auto value = local_client.get(all_keys[rand_idx]); 
            // 
            if (value.size() > 0) current_iops++;
        }
    };

    // 3. 
    std::string scheme_name = get_scheme_name(paras.placement_rule);
    std::string out_filename = "res/interference_" + scheme_name + "_f" + std::to_string(failed_num) + ".csv";
    auto monitor_worker = [&]() {
        std::ofstream outfile(out_filename);
        outfile << "Time_s,IOPS\n";
        int time_sec = 0;
        while (keep_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            time_sec++;
            int ops_this_sec = current_iops.exchange(0);
            std::cout << "[Monitor] Time: " << time_sec << "s | Foreground IOPS: " << ops_this_sec << std::endl;
            outfile << time_sec << "," << ops_this_sec << "\n";
        }
        outfile.close();
    };

    // 
    int num_fg_threads = 5; // 
    std::vector<std::thread> fg_threads;
    for (int i = 0; i < num_fg_threads; i++) {
        fg_threads.emplace_back(foreground_worker, i); 
    }
    std::thread monitor_thread(monitor_worker);

    // ================= =================
    std::cout << "\n>>> [Phase 1] Running steady state for 10 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // =================  =================
    std::cout << "\n>>> [Phase 2] Triggering Background Multi-Block Repair (" << failed_num << " blocks)..." << std::endl;
    auto bg_start = std::chrono::steady_clock::now();
    
    auto stripe_ids = client.list_stripes();
    int block_num = paras.cp.k + paras.cp.m;
    
    for (int i = 0; i < stripe_num; i++) {
        srand(2026 + i);

        std::vector<int> failed_blocks;
        random_n_num(0, block_num - 1, failed_num, failed_blocks);
        
        std::vector<unsigned int> failures(failed_blocks.begin(), failed_blocks.end());
        auto resp = client.blocks_repair(failures, stripe_ids[i]);
    }

    auto bg_end = std::chrono::steady_clock::now();
    std::chrono::duration<double> bg_duration = bg_end - bg_start;
    std::cout << "\n>>> [Info] Background repair finished in " << bg_duration.count() 
              << " seconds! " << std::endl;

    // =================  =================
    std::cout << "\n>>> [Phase 3] Running recovery state for 10 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // 
    keep_running = false;
    for (auto& t : fg_threads) {
        if (t.joinable()) t.join();
    }
    if (monitor_thread.joinable()) monitor_thread.join();

    std::cout << ">>> Experiment completed! Data saved to " << out_filename << std::endl;
}
*/

//  (Parallel Background Repair with Uniform Workload)
void test_foreground_interference_parallel_uniform(Client &client, const std::vector<std::vector<std::string>> &ms_object_keys, 
                                                   int stripe_num, const ParametersInfo &paras, int failed_num) 
{   
    std::string scheme_name = get_scheme_name(paras.placement_rule);
    std::cout << "\n=========================================" << std::endl;
    std::cout << "  PARALLEL BACKGROUND REPAIR INTERFERENCE TEST (UNIFORM) " << std::endl;
    std::cout << "  Scheme: " << scheme_name << " | Failed: " << failed_num << std::endl;
    std::cout << "=========================================\n" << std::endl;

    std::atomic<bool> keep_running(true);
    std::atomic<int> current_iops(0);

    // 1. 
    std::vector<std::string> all_keys;
    for (const auto& keys : ms_object_keys) {
        for (const auto& k : keys) {
            all_keys.push_back(k);
        }
    }
    int total_keys = all_keys.size();

    // 2.
    int num_fg_threads = 5; 
    auto foreground_worker = [&](int thread_id) {
        int unique_client_port = CLIENT_PORT + thread_id + 1;
        Client local_client("192.168.0.217", unique_client_port, "192.168.0.217", COORDINATOR_PORT);
        
        // 
        std::mt19937 rng(std::random_device{}() + thread_id);
        std::uniform_int_distribution<int> dist(0, total_keys - 1);

        while (keep_running) {
            int rand_idx = dist(rng);
            try {
                auto value = local_client.get(all_keys[rand_idx]); 
                if (value.size() > 0) {
                    current_iops++;
                }
            } catch (const std::exception& e) {
                // 
            } catch (...) {
                // 
            }
        }
    };

    // 3. 
    // 
    std::string out_filename = "res/interference_parallel_uniform_" + scheme_name + "_f" + std::to_string(failed_num) + ".csv";
    auto monitor_worker = [&]() {
        std::ofstream outfile(out_filename);
        outfile << "Time_s,IOPS\n";
        int time_sec = 0;
        while (keep_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            time_sec++;
            int ops_this_sec = current_iops.exchange(0);
            std::cout << "[Monitor] Time: " << time_sec << "s | Foreground IOPS: " << ops_this_sec << std::endl;
            outfile << time_sec << "," << ops_this_sec << "\n";
        }
        outfile.close();
    };

    // 
    std::vector<std::thread> fg_threads;
    for (int i = 0; i < num_fg_threads; i++) {
        fg_threads.emplace_back(foreground_worker, i); 
    }
    std::thread monitor_thread(monitor_worker);

    // ================= =================
    std::cout << "\n>>> [Phase 1] Running steady state for 10 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // =================  =================
    std::cout << "\n>>> [Phase 2] Triggering Parallel Background Multi-Block Repair (" << failed_num << " blocks)..." << std::endl;
    auto bg_start = std::chrono::steady_clock::now();
    
    auto stripe_ids = client.list_stripes();
    int block_num = paras.cp.k + paras.cp.m;

    
    std::mt19937 select_rng(8888); 

  
    int target_repair_count = std::min(20, (int)stripe_ids.size());

    
    std::vector<int> shuffled_indices(stripe_ids.size());
    std::iota(shuffled_indices.begin(), shuffled_indices.end(), 0);
    std::shuffle(shuffled_indices.begin(), shuffled_indices.end(), select_rng);

    
    struct RepairTask {
        int stripe_id;
        std::vector<int> failed_blocks;
    };
    std::vector<RepairTask> tasks;
    for (int i = 0; i < target_repair_count; i++) {
        int idx = shuffled_indices[i];
        int target_stripe_id = stripe_ids[idx];

        
        std::mt19937 block_rng(2026 + target_stripe_id); 
        std::vector<int> all_block_indices(block_num);
        std::iota(all_block_indices.begin(), all_block_indices.end(), 0);
        std::shuffle(all_block_indices.begin(), all_block_indices.end(), block_rng);

        std::vector<int> failed_blocks(all_block_indices.begin(), all_block_indices.begin() + failed_num);

        RepairTask t;
        t.stripe_id = target_stripe_id;
        t.failed_blocks = failed_blocks;
        tasks.push_back(t);
    }

    
    std::atomic<int> next_task_idx(0);
    int bg_concurrency_limit = 1; 
    std::vector<std::thread> bg_workers;

    for (int i = 0; i < bg_concurrency_limit; i++) {
        bg_workers.emplace_back([tasks, target_repair_count, num_fg_threads, &next_task_idx, i]() {
            // 
            int unique_bg_port = CLIENT_PORT + num_fg_threads + 1000 + i; 
            Client bg_client("192.168.0.217", unique_bg_port, "192.168.0.217", COORDINATOR_PORT);
            
            while (true) {
                int current_task = next_task_idx.fetch_add(1);
                if (current_task >= target_repair_count) {
                    break;
                }

                RepairTask task = tasks[current_task];
                std::vector<unsigned int> failures(task.failed_blocks.begin(), task.failed_blocks.end());
                
                try {
                    bg_client.blocks_repair(failures, task.stripe_id);
                } catch (...) {
                    // 后台网络抖动容错
                }
            }
        });
    }

    // 
    for (auto& t : bg_workers) {
        if (t.joinable()) t.join();
    }

    auto bg_end = std::chrono::steady_clock::now();
    std::chrono::duration<double> bg_duration = bg_end - bg_start;
    std::cout << "\n>>> [Info] Parallel background repair finished in " << bg_duration.count() 
              << " seconds! " << std::endl;

    // ================= =================
    std::cout << "\n>>> [Phase 3] Running recovery state for 10 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // 
    keep_running = false;
    for (auto& t : fg_threads) {
        if (t.joinable()) t.join();
    }
    if (monitor_thread.joinable()) monitor_thread.join();

    std::cout << ">>> Experiment completed! Data saved to " << out_filename << std::endl;
}

std::vector<int> load_trace_file(const std::string& filename) {
    std::vector<int> trace_seq;
    std::ifstream infile(filename);
    if (!infile.is_open()) {
        std::cerr << "Cannot find trace.txt! Please check the file path: " << filename << std::endl;
        exit(1);
    }
    int key_index;
    while (infile >> key_index) {
        trace_seq.push_back(key_index);
    }
    std::cout << ">>> Loaded " << trace_seq.size() << " GET requests from trace!" << std::endl;
    return trace_seq;
}


/*
//
void test_foreground_interference_trace(Client &client, const std::vector<std::vector<std::string>> &ms_object_keys, 
                                        int stripe_num, const ParametersInfo &paras, int failed_num) 
{   
    std::string scheme_name = get_scheme_name(paras.placement_rule);
    std::cout << "\n=========================================" << std::endl;
    std::cout << "  PRODUCTION TRACE INTERFERENCE TEST " << std::endl;
    std::cout << "  Scheme: " << scheme_name << " | Failed: " << failed_num << std::endl;
    std::cout << "=========================================\n" << std::endl;

    std::atomic<bool> keep_running(true);
    std::atomic<int> current_iops(0);

    // 1. 
    std::vector<std::string> all_keys;
    for (const auto& keys : ms_object_keys) {
        for (const auto& k : keys) {
            all_keys.push_back(k);
        }
    }
    int total_keys = all_keys.size();

    
    std::vector<int> global_trace = load_trace_file("data/trace.txt"); 
    std::atomic<size_t> trace_ptr(0); 

    // 2. 
    auto foreground_worker = [&](int thread_id) {
        int unique_client_port = CLIENT_PORT + thread_id + 1;
        Client local_client("127.0.0.1", unique_client_port, "127.0.0.1", COORDINATOR_PORT);
        
        while (keep_running) {
            size_t current_idx = trace_ptr.fetch_add(1);
            
            if (current_idx >= global_trace.size()) {
                current_idx = current_idx % global_trace.size();
            }

            int target_trace_id = global_trace[current_idx];
            int safe_key_idx = target_trace_id % total_keys;

            auto value = local_client.get(all_keys[safe_key_idx]); 
            current_iops++;
        }
    };

    // 3. 
    std::string out_filename = "res/interference_trace_" + scheme_name + "_f" + std::to_string(failed_num) + ".csv";
    auto monitor_worker = [&]() {
        std::ofstream outfile(out_filename);
        outfile << "Time_s,IOPS\n";
        int time_sec = 0;
        while (keep_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            time_sec++;
            int ops_this_sec = current_iops.exchange(0);
            std::cout << "[Monitor] Time: " << time_sec << "s | Foreground IOPS: " << ops_this_sec << std::endl;
            outfile << time_sec << "," << ops_this_sec << "\n";
        }
        outfile.close();
    };

    int num_fg_threads = 5; 
    std::vector<std::thread> fg_threads;
    for (int i = 0; i < num_fg_threads; i++) {
        fg_threads.emplace_back(foreground_worker, i); 
    }
    std::thread monitor_thread(monitor_worker);

    // ================= =================
    std::cout << "\n>>> [Phase 1] Running steady state for 10 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // =================  =================
    std::cout << "\n>>> [Phase 2] Triggering Background Multi-Block Repair (" << failed_num << " blocks)..." << std::endl;
    auto bg_start = std::chrono::steady_clock::now();
    
    auto stripe_ids = client.list_stripes();
    int block_num = paras.cp.k + paras.cp.m;
    
    for (int i = 0; i < stripe_num; i++) {
        srand(2026 + i);

        std::vector<int> failed_blocks;
        random_n_num(0, block_num - 1, failed_num, failed_blocks);
        
        std::vector<unsigned int> failures(failed_blocks.begin(), failed_blocks.end());
        auto resp = client.blocks_repair(failures, stripe_ids[i]);
    }

    auto bg_end = std::chrono::steady_clock::now();
    std::chrono::duration<double> bg_duration = bg_end - bg_start;
    std::cout << "\n>>> [Info] Background repair finished in " << bg_duration.count() 
              << " seconds! " << std::endl;

    // ================= =================
    std::cout << "\n>>> [Phase 3] Running recovery state for 10 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    //
    keep_running = false;
    for (auto& t : fg_threads) {
        if (t.joinable()) t.join();
    }
    if (monitor_thread.joinable()) monitor_thread.join();

    std::cout << ">>> Experiment completed! Data saved to " << out_filename << std::endl;
}
*/

// (Parallel Background Repair)
void test_foreground_interference_parallel_production(Client &client, const std::vector<std::vector<std::string>> &ms_object_keys, 
                                           int stripe_num, const ParametersInfo &paras, int failed_num) 
{   
    std::string scheme_name = get_scheme_name(paras.placement_rule);
    std::cout << "\n=========================================" << std::endl;
    std::cout << "  PARALLEL BACKGROUND REPAIR INTERFERENCE TEST " << std::endl;
    std::cout << "  Scheme: " << scheme_name << " | Failed: " << failed_num << std::endl;
    std::cout << "=========================================\n" << std::endl;

    std::atomic<bool> keep_running(true);
    std::atomic<int> current_iops(0);

    // 1. 
    std::vector<std::string> all_keys;
    for (const auto& keys : ms_object_keys) {
        for (const auto& k : keys) {
            all_keys.push_back(k);
        }
    }
    int total_keys = all_keys.size();


    std::vector<int> global_trace = load_trace_file("data/trace.txt"); 
    std::atomic<size_t> trace_ptr(0); 

    // 2.
    int num_fg_threads = 5; 
    auto foreground_worker = [&](int thread_id) {
        int unique_client_port = CLIENT_PORT + thread_id + 1;
        Client local_client("192.168.0.217", unique_client_port, "192.168.0.217", COORDINATOR_PORT);
        
        while (keep_running) {
            size_t current_idx = trace_ptr.fetch_add(1);
            if (current_idx >= global_trace.size()) {
                current_idx = current_idx % global_trace.size();
            }

            int target_trace_id = global_trace[current_idx];
            int safe_key_idx = target_trace_id % total_keys;

            try {
                auto value = local_client.get(all_keys[safe_key_idx]); 
                if (value.size() > 0) {
                    current_iops++;
                }
            } catch (const std::exception& e) {
      
            } catch (...) {
                // 
            }
        }
    };

    // 3.
    std::string out_filename = "res/interference_parallel_production_" + scheme_name + "_f" + std::to_string(failed_num) + ".csv";
    auto monitor_worker = [&]() {
        std::ofstream outfile(out_filename);
        outfile << "Time_s,IOPS\n";
        int time_sec = 0;
        while (keep_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            time_sec++;
            int ops_this_sec = current_iops.exchange(0);
            std::cout << "[Monitor] Time: " << time_sec << "s | Foreground IOPS: " << ops_this_sec << std::endl;
            outfile << time_sec << "," << ops_this_sec << "\n";
        }
        outfile.close();
    };

    // 
    std::vector<std::thread> fg_threads;
    for (int i = 0; i < num_fg_threads; i++) {
        fg_threads.emplace_back(foreground_worker, i); 
    }
    std::thread monitor_thread(monitor_worker);

    // ================= =================
    std::cout << "\n>>> [Phase 1] Running steady state for 10 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // ================= =================
    std::cout << "\n>>> [Phase 2] Triggering Parallel Background Multi-Block Repair (" << failed_num << " blocks)..." << std::endl;
    auto bg_start = std::chrono::steady_clock::now();
    
    auto stripe_ids = client.list_stripes();
    int block_num = paras.cp.k + paras.cp.m;

    std::mt19937 select_rng(8888); 
    int target_repair_count = std::min(20, (int)stripe_ids.size());

    std::vector<int> shuffled_indices(stripe_ids.size());
    std::iota(shuffled_indices.begin(), shuffled_indices.end(), 0);
    std::shuffle(shuffled_indices.begin(), shuffled_indices.end(), select_rng);

    struct RepairTask {
        int stripe_id;
        std::vector<int> failed_blocks;
    };
    std::vector<RepairTask> tasks;
    for (int i = 0; i < target_repair_count; i++) {
        int idx = shuffled_indices[i];
        int target_stripe_id = stripe_ids[idx];

        std::mt19937 block_rng(2026 + target_stripe_id); 
        std::vector<int> all_block_indices(block_num);
        std::iota(all_block_indices.begin(), all_block_indices.end(), 0);
        std::shuffle(all_block_indices.begin(), all_block_indices.end(), block_rng);

        std::vector<int> failed_blocks(all_block_indices.begin(), all_block_indices.begin() + failed_num);

        RepairTask t;
        t.stripe_id = target_stripe_id;
        t.failed_blocks = failed_blocks;
        tasks.push_back(t);
    }

  
    std::atomic<int> next_task_idx(0);
    int bg_concurrency_limit = 1; 
    std::vector<std::thread> bg_workers;

    for (int i = 0; i < bg_concurrency_limit; i++) {
        bg_workers.emplace_back([tasks, target_repair_count, num_fg_threads, &next_task_idx, i]() {
            int unique_bg_port = CLIENT_PORT + num_fg_threads + 1000 + i; 
            Client bg_client("192.168.0.217", unique_bg_port, "192.168.0.217", COORDINATOR_PORT);
            
            while (true) {
                int current_task = next_task_idx.fetch_add(1);
                if (current_task >= target_repair_count) {
                    break;
                }

                RepairTask task = tasks[current_task];
                std::vector<unsigned int> failures(task.failed_blocks.begin(), task.failed_blocks.end());
                
                try {
                    bg_client.blocks_repair(failures, task.stripe_id);
                } catch (...) {
                    // 
                }

            }
        });
    }

    // 
    for (auto& t : bg_workers) {
        if (t.joinable()) t.join();
    }

    auto bg_end = std::chrono::steady_clock::now();
    std::chrono::duration<double> bg_duration = bg_end - bg_start;
    std::cout << "\n>>> [Info] Parallel background repair finished in " << bg_duration.count() 
              << " seconds! " << std::endl;

    // ================= =================
    std::cout << "\n>>> [Phase 3] Running recovery state for 10 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    keep_running = false;
    for (auto& t : fg_threads) {
        if (t.joinable()) t.join();
    }
    if (monitor_thread.joinable()) monitor_thread.join();

    std::cout << ">>> Experiment completed! Data saved to " << out_filename << std::endl;
}

//  (Throughput Saturation Sweep Test)
void test_throughput_saturation(Client &client, const std::vector<std::vector<std::string>> &ms_object_keys) 
{
    std::cout << "\n=========================================" << std::endl;
    std::cout << "  FOREGROUND SATURATION SWEEP TEST  " << std::endl;
    std::cout << "=========================================\n" << std::endl;

    std::vector<std::string> all_keys;
    for (const auto& keys : ms_object_keys) {
        for (const auto& k : keys) {
            all_keys.push_back(k);
        }
    }
    int total_keys = all_keys.size();

    std::vector<int> thread_counts = {1, 2, 4, 5, 6, 7, 8, 10, 12, 16};
    int test_duration_sec = 10; 

    std::string out_filename = "res/saturation_sweep.csv";
    std::ofstream outfile(out_filename);
    if (!outfile.is_open()) {
        std::cerr << ">>> [ERROR] Failed to open file for writing: " << out_filename << std::endl;
        return;
    }
    outfile << "Thread_Count,Average_IOPS\n";

    for (int tc : thread_counts) {
        std::cout << ">>> Testing with " << tc << " concurrent thread(s) for " << test_duration_sec << " seconds..." << std::endl;

        std::atomic<bool> keep_running(true);
        std::atomic<int> total_ops(0);
        std::vector<std::thread> fg_threads;

        for (int i = 0; i < tc; i++) {
            fg_threads.emplace_back([&, i]() {
                // 
                int unique_client_port = CLIENT_PORT + 200 + i; 
                Client local_client("192.168.0.217", unique_client_port, "192.168.0.217", COORDINATOR_PORT);
                
                // 
                std::mt19937 rng(std::random_device{}() + i);
                std::uniform_int_distribution<int> dist(0, total_keys - 1);

                while (keep_running) {
                    int rand_idx = dist(rng);
                    try {
                        auto value = local_client.get(all_keys[rand_idx]);
                        if (value.size() > 0) {
                            total_ops++;
                        }
                    } catch (...) {
                        // 
                    }
                }
            });
        }

        std::this_thread::sleep_for(std::chrono::seconds(test_duration_sec));

        keep_running = false;
        for (auto& t : fg_threads) {
            if (t.joinable()) t.join();
        }

        double avg_iops = static_cast<double>(total_ops) / test_duration_sec;
        std::cout << "[Result] Threads: " << tc << " | Average IOPS: " << avg_iops << "\n" << std::endl;
        
        outfile << tc << "," << avg_iops << "\n";
   
        std::this_thread::sleep_for(std::chrono::seconds(2)); 
    }

    outfile.close();
    std::cout << ">>> Saturation sweep completed! Data saved to " << out_filename << std::endl;
}


void test_multiple_blocks_repair(Client &client, int block_num, const ParametersInfo &paras,
                                 int failed_num)
{
  auto stripe_ids = client.list_stripes();
  int stripe_num = stripe_ids.size();
  std::vector<double> repair_times;
  std::vector<double> decoding_times;
  std::vector<double> cross_cluster_times;
  std::vector<double> meta_times;
  std::vector<int> cross_cluster_transfers;
  std::vector<int> io_cnts;
  int run_time = 5;
  int tot_cnt = 0;
  ErasureCode *ec = ec_factory(paras.ec_type, paras.cp);
  std::cout << "Multi-Block Repair:" << std::endl;
  for (int i = 0; i < stripe_num; i++)
  {
    std::cout << "[Stripe " << i << "]" << std::endl;
    double temp_repair = 0;
    double temp_decoding = 0;
    double temp_cross_cluster = 0;
    double temp_meta = 0;
    int temp_cc_transfers = 0;
    int temp_io_cnt = 0;
    int cnt = 0;
    for (int j = 0; j < run_time; j++)
    {
      // int failed_num = random_range(2, 4);
      std::vector<int> failed_blocks;
      random_n_num(0, block_num - 1, failed_num, failed_blocks);
      std::vector<unsigned int> failures;
      for (auto &block : failed_blocks)
      {
        failures.push_back((unsigned int)block);
      }
      if (!ec->check_if_decodable(failed_blocks))
      {
        j--;
        continue;
      }
      auto resp = client.blocks_repair(failures, stripe_ids[i]);
      if (resp.success)
      {
        temp_repair += resp.repair_time;
        temp_decoding += resp.decoding_time;
        temp_cross_cluster += resp.cross_cluster_time;
        temp_meta += resp.meta_time;
        temp_cc_transfers += resp.cross_cluster_transfers;
        temp_io_cnt += resp.io_cnt;
        cnt++;
      }
    }
    repair_times.push_back(temp_repair);
    decoding_times.push_back(temp_decoding);
    cross_cluster_times.push_back(temp_cross_cluster);
    meta_times.push_back(temp_meta);
    cross_cluster_transfers.push_back(temp_cc_transfers);
    io_cnts.push_back(temp_io_cnt);
    std::cout << "repair = " << temp_repair / cnt
              << "s, decoding = " << temp_decoding / cnt
              << "s, cross-cluster = " << temp_cross_cluster / cnt
              << "s, meta = " << temp_meta / cnt
              << "s, cross-cluster-count = " << (double)temp_cc_transfers / cnt
              << ", I/Os = " << temp_io_cnt / cnt
              << std::endl;
    tot_cnt += cnt;
  }
  auto avg_repair = std::accumulate(repair_times.begin(),
                                    repair_times.end(), 0.0) /
                    tot_cnt;
  auto avg_decoding = std::accumulate(decoding_times.begin(),
                                      decoding_times.end(), 0.0) /
                      tot_cnt;
  auto avg_cross_cluster = std::accumulate(cross_cluster_times.begin(),
                                           cross_cluster_times.end(), 0.0) /
                           tot_cnt;
  auto avg_meta = std::accumulate(meta_times.begin(),
                                  meta_times.end(), 0.0) /
                  tot_cnt;
  auto avg_cc_transfers = (double)std::accumulate(cross_cluster_transfers.begin(),
                                                  cross_cluster_transfers.end(), 0) /
                          tot_cnt;
  auto avg_io_cnt = (double)std::accumulate(io_cnts.begin(),
                                            io_cnts.end(), 0) /
                    tot_cnt;
  std::cout << "^-^[Average]^-^" << std::endl;
  std::cout << "repair = " << avg_repair << "s, decoding = " << avg_decoding
            << "s, cross-cluster = " << avg_cross_cluster
            << "s, meta = " << avg_meta
            << "s, cross-cluster-count = " << avg_cc_transfers
            << ", I/Os = " << avg_io_cnt
            << std::endl;
  if (ec != nullptr)
  {
    delete ec;
    ec = nullptr;
  }
}

void test_multiple_blocks_repair_lrc(Client &client, const ParametersInfo &paras,
                                     int failed_num)
{
  auto stripe_ids = client.list_stripes();
  int stripe_num = stripe_ids.size();
  std::vector<double> repair_times;
  std::vector<double> decoding_times;
  std::vector<double> cross_cluster_times;
  std::vector<double> meta_times;
  std::vector<int> cross_cluster_transfers;
  std::vector<int> io_cnts;
  int run_time = 10;
  int tot_cnt = 0;
  LocallyRepairableCode *lrc = lrc_factory(paras.ec_type, paras.cp);
  std::vector<std::vector<int>> groups;
  lrc->grouping_information(groups);
  int group_num = (int)groups.size();
  std::cout << "Multi-Block Repair:" << std::endl;
  for (int i = 0; i < stripe_num; i++)
  {
    std::cout << "[Stripe " << i << "]" << std::endl;
    double temp_repair = 0;
    double temp_decoding = 0;
    double temp_cross_cluster = 0;
    double temp_meta = 0;
    int temp_cc_transfers = 0;
    int temp_io_cnt = 0;
    int cnt = 0;
    for (int j = 0; j < run_time; j++)
    {
      // int gid = random_index((size_t)group_num);
      int ran_data_idx = random_index((size_t)(lrc->k + lrc->g));
      int gid = ran_data_idx / lrc->r;
      std::vector<int> failed_blocks;
      random_n_element(2, groups[gid], failed_blocks);
      if (failed_num > 2)
      {
        int t_gid = random_index((size_t)group_num);
        int t_idx = random_index(groups[t_gid].size());
        int failed_idx = groups[t_gid][t_idx];
        while (std::find(failed_blocks.begin(), failed_blocks.end(), failed_idx) != failed_blocks.end())
        {
          t_gid = random_index((size_t)group_num);
          t_idx = random_index(groups[t_gid].size());
          failed_idx = groups[t_gid][t_idx];
        }
        failed_blocks.push_back(failed_idx);
        if (failed_num > 3)
        {
          int tt_gid = 0;
          if (gid == t_gid && paras.cp.g < 3)
          {
            tt_gid = (gid + random_index((size_t)(group_num - 1)) + 1) % group_num;
          }
          else
          {
            tt_gid = random_index((size_t)group_num);
          }
          t_idx = random_index(groups[tt_gid].size());
          failed_idx = groups[tt_gid][t_idx];
          while (std::find(failed_blocks.begin(), failed_blocks.end(), failed_idx) != failed_blocks.end())
          {
            if (gid == t_gid && paras.cp.g < 3)
            {
              tt_gid = (gid + random_index((size_t)(group_num - 1)) + 1) % group_num;
            }
            else
            {
              tt_gid = random_index((size_t)group_num);
            }
            t_idx = random_index(groups[tt_gid].size());
            failed_idx = groups[tt_gid][t_idx];
          }
          failed_blocks.push_back(failed_idx);
        }
      }
      if (!lrc->check_if_decodable(failed_blocks))
      {
        j--;
        continue;
      }
      std::vector<unsigned int> failures;
      for (auto &block : failed_blocks)
      {
        failures.push_back((unsigned int)block);
      }
      auto resp = client.blocks_repair(failures, stripe_ids[i]);
      if (resp.success)
      {
        temp_repair += resp.repair_time;
        temp_decoding += resp.decoding_time;
        temp_cross_cluster += resp.cross_cluster_time;
        temp_meta += resp.meta_time;
        temp_cc_transfers += resp.cross_cluster_transfers;
        temp_io_cnt += resp.io_cnt;
        cnt++;
      }
    }
    repair_times.push_back(temp_repair);
    decoding_times.push_back(temp_decoding);
    cross_cluster_times.push_back(temp_cross_cluster);
    meta_times.push_back(temp_meta);
    cross_cluster_transfers.push_back(temp_cc_transfers);
    io_cnts.push_back(temp_io_cnt);
    std::cout << "repair = " << temp_repair / cnt
              << "s, decoding = " << temp_decoding / cnt
              << "s, cross-cluster = " << temp_cross_cluster / cnt
              << "s, meta = " << temp_meta / cnt
              << "s, cross-cluster-count = " << (double)temp_cc_transfers / cnt
              << ", I/Os = " << temp_io_cnt / cnt
              << std::endl;
    tot_cnt += cnt;
  }
  auto avg_repair = std::accumulate(repair_times.begin(),
                                    repair_times.end(), 0.0) /
                    tot_cnt;
  auto avg_decoding = std::accumulate(decoding_times.begin(),
                                      decoding_times.end(), 0.0) /
                      tot_cnt;
  auto avg_cross_cluster = std::accumulate(cross_cluster_times.begin(),
                                           cross_cluster_times.end(), 0.0) /
                           tot_cnt;
  auto avg_meta = std::accumulate(meta_times.begin(),
                                  meta_times.end(), 0.0) /
                  tot_cnt;
  auto avg_cc_transfers = (double)std::accumulate(cross_cluster_transfers.begin(),
                                                  cross_cluster_transfers.end(), 0) /
                          tot_cnt;
  auto avg_io_cnt = (double)std::accumulate(io_cnts.begin(),
                                            io_cnts.end(), 0) /
                    tot_cnt;
  std::cout << "^-^[Average]^-^" << std::endl;
  std::cout << "repair = " << avg_repair << "s, decoding = " << avg_decoding
            << "s, cross-cluster = " << avg_cross_cluster
            << "s, meta = " << avg_meta
            << "s, cross-cluster-count = " << avg_cc_transfers
            << ", I/Os = " << avg_io_cnt
            << std::endl;
  if (lrc != nullptr)
  {
    delete lrc;
    lrc = nullptr;
  }
}

void test_stripe_merging(Client &client, int step_size)
{
  my_assert(step_size > 1);
  auto stripe_ids = client.list_stripes();
  int stripe_num = stripe_ids.size();
  std::cout << "Stripe Merging:" << std::endl;
  auto resp = client.merge(step_size);
  std::cout << "[Total]" << std::endl;
  std::cout << "merging = " << resp.merging_time
            << "s, computing = " << resp.computing_time
            << "s, cross-cluster = " << resp.cross_cluster_time
            << "s, meta =" << resp.meta_time
            << "s, cross-cluster-count = " << resp.cross_cluster_transfers
            << ", I/Os = " << resp.io_cnt
            << std::endl;
  std::cout << "[Average for every " << step_size << " stripes]" << std::endl;
  std::cout << "merging = " << resp.merging_time / stripe_num
            << "s, computing = " << resp.computing_time / stripe_num
            << "s, cross-cluster = " << resp.cross_cluster_time / stripe_num
            << "s, meta =" << resp.meta_time / stripe_num
            << "s, cross-cluster-count = "
            << (double)resp.cross_cluster_transfers / stripe_num
            << ", I/Os = " << resp.io_cnt / stripe_num
            << std::endl;
}

void generate_random_multi_block_failures_lrc(std::string filename,
                                              int stripe_num, const ParametersInfo &paras, int failed_num)
{
  LocallyRepairableCode *lrc = lrc_factory(paras.ec_type, paras.cp);
  std::vector<std::vector<int>> groups;
  lrc->grouping_information(groups);
  int group_num = (int)groups.size();
  std::string suf = lrc->type() + "_" + std::to_string(paras.cp.k) + "_" +
                    std::to_string(paras.cp.l) + "_" + std::to_string(paras.cp.g) + "_" +
                    std::to_string(failed_num);
  filename += suf;
  std::ofstream outFile(filename);
  if (!outFile)
  {
    std::cerr << "Error! Unable to open " << filename << std::endl;
    return;
  }
  int cases_per_stripe = 10;
  for (int i = 0; i < cases_per_stripe * stripe_num; i++)
  {
    int ran_data_idx = random_index((size_t)(lrc->k + lrc->g));
    int gid = ran_data_idx / lrc->r;
    std::vector<int> failed_blocks;
    random_n_element(2, groups[gid], failed_blocks);
    if (failed_num > 2)
    {
      int t_gid = random_index((size_t)group_num);
      int t_idx = random_index(groups[t_gid].size());
      int failed_idx = groups[t_gid][t_idx];
      while (std::find(failed_blocks.begin(), failed_blocks.end(), failed_idx) != failed_blocks.end())
      {
        t_gid = random_index((size_t)group_num);
        t_idx = random_index(groups[t_gid].size());
        failed_idx = groups[t_gid][t_idx];
      }
      failed_blocks.push_back(failed_idx);
      if (failed_num > 3)
      {
        int tt_gid = 0;
        if (gid == t_gid && paras.cp.g < 3)
        {
          tt_gid = (gid + random_index((size_t)(group_num - 1)) + 1) % group_num;
        }
        else
        {
          tt_gid = random_index((size_t)group_num);
        }
        t_idx = random_index(groups[tt_gid].size());
        failed_idx = groups[tt_gid][t_idx];
        while (std::find(failed_blocks.begin(), failed_blocks.end(), failed_idx) != failed_blocks.end())
        {
          if (gid == t_gid && paras.cp.g < 3)
          {
            tt_gid = (gid + random_index((size_t)(group_num - 1)) + 1) % group_num;
          }
          else
          {
            tt_gid = random_index((size_t)group_num);
          }
          t_idx = random_index(groups[tt_gid].size());
          failed_idx = groups[tt_gid][t_idx];
        }
        failed_blocks.push_back(failed_idx);
      }
    }
    if (!lrc->check_if_decodable(failed_blocks))
    {
      i--;
      continue;
    }
    else
    {
      for (const auto &num : failed_blocks)
      {
        outFile << num << " ";
      }
      outFile << "\n";
    }
  }
  outFile.close();
}

void test_multiple_blocks_repair_lrc_with_testcases(std::string filename,
                                                    Client &client, const ParametersInfo &paras, int failed_num)
{
  auto stripe_ids = client.list_stripes();
  int stripe_num = stripe_ids.size();
  std::vector<double> repair_times;
  std::vector<double> decoding_times;
  std::vector<double> cross_cluster_times;
  std::vector<double> meta_times;
  std::vector<int> cross_cluster_transfers;
  std::vector<int> io_cnts;
  int run_time = 10;
  int tot_cnt = 0;
  LocallyRepairableCode *lrc = lrc_factory(paras.ec_type, paras.cp);
  std::vector<std::vector<int>> groups;
  lrc->grouping_information(groups);
  int group_num = (int)groups.size();
  std::string suf = lrc->type() + "_" + std::to_string(paras.cp.k) + "_" +
                    std::to_string(paras.cp.l) + "_" + std::to_string(paras.cp.g) + "_" +
                    std::to_string(failed_num);
  filename += suf;
  std::ifstream inFile(filename);
  if (!inFile)
  {
    std::cerr << "Error! Unable to open " << filename << std::endl;
    return;
  }
  std::string line;
  std::cout << "Multi-Block Repair:" << std::endl;
  int ii = 0;
  int test_stripe_num = 5;
  for (int i = 0; i < test_stripe_num; i++)
  {
    std::cout << "[Stripe " << i << "]" << std::endl;
    double temp_repair = 0;
    double temp_decoding = 0;
    double temp_cross_cluster = 0;
    double temp_meta = 0;
    int temp_cc_transfers = 0;
    int temp_io_cnt = 0;
    int cnt = 0;
    for (int j = 0; j < run_time; j++)
    {
      std::vector<int> failed_blocks;
      std::getline(inFile, line);
      std::istringstream lineStream(line);
      int num;
      while (lineStream >> num)
      {
        failed_blocks.push_back(num);
      }
      if (i < test_stripe_num - stripe_num)
      {
        continue;
      }
      std::vector<unsigned int> failures;
      for (auto &block : failed_blocks)
      {
        failures.push_back((unsigned int)block);
      }
      auto resp = client.blocks_repair(failures, stripe_ids[ii]);
      if (resp.success)
      {
        temp_repair += resp.repair_time;
        temp_decoding += resp.decoding_time;
        temp_cross_cluster += resp.cross_cluster_time;
        temp_meta += resp.meta_time;
        temp_cc_transfers += resp.cross_cluster_transfers;
        temp_io_cnt += resp.io_cnt;
        cnt++;
      }
    }
    if (i < test_stripe_num - stripe_num)
    {
      continue;
    }
    ii++;
    repair_times.push_back(temp_repair);
    decoding_times.push_back(temp_decoding);
    cross_cluster_times.push_back(temp_cross_cluster);
    meta_times.push_back(temp_meta);
    cross_cluster_transfers.push_back(temp_cc_transfers);
    io_cnts.push_back(temp_io_cnt);
    std::cout << "repair = " << temp_repair / cnt
              << "s, decoding = " << temp_decoding / cnt
              << "s, cross-cluster = " << temp_cross_cluster / cnt
              << "s, meta = " << temp_meta / cnt
              << "s, cross-cluster-count = " << (double)temp_cc_transfers / cnt
              << ", I/Os = " << temp_io_cnt / cnt
              << std::endl;
    tot_cnt += cnt;
  }
  inFile.close();
  auto avg_repair = std::accumulate(repair_times.begin(),
                                    repair_times.end(), 0.0) /
                    tot_cnt;
  auto avg_decoding = std::accumulate(decoding_times.begin(),
                                      decoding_times.end(), 0.0) /
                      tot_cnt;
  auto avg_cross_cluster = std::accumulate(cross_cluster_times.begin(),
                                           cross_cluster_times.end(), 0.0) /
                           tot_cnt;
  auto avg_meta = std::accumulate(meta_times.begin(),
                                  meta_times.end(), 0.0) /
                  tot_cnt;
  auto avg_cc_transfers = (double)std::accumulate(cross_cluster_transfers.begin(),
                                                  cross_cluster_transfers.end(), 0) /
                          tot_cnt;
  auto avg_io_cnt = (double)std::accumulate(io_cnts.begin(),
                                            io_cnts.end(), 0) /
                    tot_cnt;
  std::cout << "^-^[Average]^-^" << std::endl;
  std::cout << "repair = " << avg_repair << "s, decoding = " << avg_decoding
            << "s, cross-cluster = " << avg_cross_cluster
            << "s, meta = " << avg_meta
            << "s, cross-cluster-count = " << avg_cc_transfers
            << ", I/Os = " << avg_io_cnt
            << std::endl;
  if (lrc != nullptr)
  {
    delete lrc;
    lrc = nullptr;
  }
}

void test_repair_performance(
    std::string path_prefix, int stripe_num, const ParametersInfo &paras, int failed_num, bool optimized)
{
  int block_num = paras.cp.k + paras.cp.m;

  Client client("192.168.0.217", CLIENT_PORT, "192.168.0.217", COORDINATOR_PORT);

  // set erasure coding parameters
  client.set_ec_parameters(paras);

  struct timeval start_time, end_time;
  // generate key-value pair
  std::vector<size_t> value_lengths(stripe_num, 0);
  std::vector<std::vector<std::string>> ms_object_keys;
  std::vector<std::vector<size_t>> ms_object_sizes;
  std::vector<std::vector<unsigned int>> ms_object_accessrates;
  // std::vector<std::vector<std::string>> ms_object_values;
  int obj_id = 0;
  for (int i = 0; i < stripe_num; i++)
  {
    std::vector<std::string> object_keys;
    for (int j = 0; j < paras.cp.k; j++)
    {
      value_lengths[i] += paras.block_size;
      object_keys.emplace_back("obj" + std::to_string(obj_id++));
    }
    ms_object_keys.emplace_back(object_keys);
    ms_object_sizes.emplace_back(std::vector<size_t>(paras.cp.k, paras.block_size));
    ms_object_accessrates.emplace_back(std::vector<unsigned int>(paras.cp.k, 1));
  }
#ifdef IN_MEMORY
  std::unordered_map<std::string, std::string> key_value;
  generate_unique_random_strings_difflen(5, stripe_num, value_lengths, key_value);
#endif

  // set
  double set_time = 0;
#ifdef IN_MEMORY
  int i = 0;
  for (auto &kv : key_value)
  {
    gettimeofday(&start_time, NULL);
    double encoding_time = client.set(kv.second, ms_object_keys[i], ms_object_sizes[i],
                                      ms_object_accessrates[i]);
    gettimeofday(&end_time, NULL);
    double temp_time = end_time.tv_sec - start_time.tv_sec +
                       (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    set_time += temp_time;
    std::cout << "[SET] set time: " << temp_time << ", encoding time: "
              << encoding_time << std::endl;
    ++i;
  }
  std::cout << "Total set time: " << set_time << ", average set time:"
            << set_time / stripe_num << std::endl;
  std::cout << "Write Throughput: " << paras.block_size * paras.cp.k * stripe_num / (set_time * 1024)
            << " KB/s" << std::endl;

/*
#else
  for (int i = 0; i < stripe_num; i++)
  {
    std::string readpath = path_prefix + "/../../data/Object";
    double encoding_time = 0;
    gettimeofday(&start_time, NULL);
    if (access(readpath.c_str(), 0) == -1)
    {
      std::cout << "[Client] file does not exist!" << std::endl;
      exit(-1);
    }
    else
    {
      char *buf = new char[value_lengths[i]];
      std::ifstream ifs(readpath);
      ifs.read(buf, value_lengths[i]);
      encoding_time = client.set(std::string(buf, value_lengths[i]), ms_object_keys[i],
                                 ms_object_sizes[i], ms_object_accessrates[i]);
      ifs.close();
      // std::vector<std::string> object_values;
      // for (int j = 0; j < paras.cp.k; j++) {
      //   object_values.emplace_back(std::string(buf + j * paras.block_size, paras.block_size));
      // }
      // ms_object_values.emplace_back(object_values);
      delete buf;
    }
    gettimeofday(&end_time, NULL);
    double temp_time = end_time.tv_sec - start_time.tv_sec +
                       (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    set_time += temp_time;
    std::cout << "[SET] set time: " << temp_time << ", encoding time: "
              << encoding_time << std::endl;
  }
  std::cout << "Total set time: " << set_time << ", average set time:"
            << set_time / stripe_num << std::endl;
  std::cout << "Write Throughput: " << paras.block_size * paras.cp.k * stripe_num / (set_time * 1024)
            << " KB/s" << std::endl;
#endif
*/
#else
  for (int i = 0; i < stripe_num; i++)
  {
    std::string readpath = path_prefix + "/../../data/Object";
    double encoding_time = 0;
    
    // 1. 
    if (access(readpath.c_str(), 0) == -1)
    {
      std::cout << "[Client] file does not exist!" << std::endl;
      exit(-1);
    }
    
    char *buf = new char[value_lengths[i]];
    std::ifstream ifs(readpath);
    ifs.read(buf, value_lengths[i]);
    ifs.close();
    

    std::string payload(buf, value_lengths[i]); 
    delete[] buf; 

    // 2. 
    gettimeofday(&start_time, NULL);
    
    encoding_time = client.set(payload, ms_object_keys[i],
                               ms_object_sizes[i], ms_object_accessrates[i]);
                               
    gettimeofday(&end_time, NULL);
    
    // 3.
    double temp_time = end_time.tv_sec - start_time.tv_sec +
                       (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    set_time += temp_time;
    std::cout << "[SET] set time: " << temp_time << ", encoding time: "
              << encoding_time << std::endl;
  }
  
  std::cout << "Total set time: " << set_time << ", average set time:"
            << set_time / stripe_num << std::endl;
            
  // 4. 
  double total_mb = (double)(paras.block_size * paras.cp.k * stripe_num) / (1024.0 * 1024.0);
  std::cout << "Write Throughput: " << total_mb / set_time << " MB/s" << std::endl;
#endif

  /*
    // get
    double get_time = 0.0;
    auto start = std::chrono::steady_clock::now();
    for (auto &object_keys : ms_object_keys)
    {
      for (auto &key : object_keys)
      {
        auto value = client.get(key);
        // my_assert(value == ms_object_values[i][j]);
      }
    }
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    get_time = elapsed.count();
    std::cout << "Total get time: " << get_time << " , average get time:"
              << double(get_time) / double(stripe_num * paras.cp.k) << std::endl;
    std::cout << "Read Throughput: " << double(paras.block_size * paras.cp.k * stripe_num) / double(get_time * 1024)
              << " MB/s" << std::endl;
  */

  bool is_ec = paras.is_ec_now;

  // redundancy transitioning, from replicas to ec
  if (!is_ec)
  {
    auto resp = client.redundancy_transition(optimized);
    if (resp.iftransed)
    {
      std::cout << "[Rep->EC] transition time: " << resp.transition_time
                << ", encoding time: " << resp.encoding_time
                << ", cross-cluster time: " << resp.cross_cluster_time
                << ", meta time: " << resp.meta_time
                << ", cross-cluster-count: " << resp.cross_cluster_transfers
                << "(data=" << resp.data_reloc_cnt
                << ", parity=" << resp.parity_reloc_cnt
                << "), I/Os = " << resp.io_cnt
                << std::endl;
      std::cout << "[Average for every stripes]" << std::endl;
      std::cout << "[Rep->EC] transition time: " << resp.transition_time / stripe_num
                << ", encoding time: " << resp.encoding_time / stripe_num
                << ", cross-cluster time: " << resp.cross_cluster_time / stripe_num
                << ", meta time: " << resp.meta_time / stripe_num
                << ", cross-cluster-count: " << resp.cross_cluster_transfers / stripe_num
                << "(data=" << resp.data_reloc_cnt / stripe_num
                << ", parity=" << resp.parity_reloc_cnt / stripe_num
                << "), I/Os = " << resp.io_cnt / stripe_num
                << std::endl;
      std::cout << "[Max] transition time: " << resp.max_trans_time
                << ", [Min] transition time: " << resp.min_trans_time << std::endl;
    }
    else
    {
      std::cout << "[Rep->EC] failed!" << std::endl;
    }
    is_ec = true;
  }

  // test repair
  if (is_ec)
  {
//    if (failed_num == 1)
//    {
//      test_single_block_repair(client, block_num);
//    }
    if (failed_num >=1 && failed_num <= 10)
    {
      // test_mode: 0 = Auto, 1 = Force Exhaustive, 2 = Force Random
      test_multiple_blocks_repair_pc(client, block_num, paras, failed_num, 1); 
    }
    else if (failed_num >= 11 && failed_num <= 20)
    {
      // uniform workload
      // 11 single block, 12 two blocks, 13 three blocks...
      test_foreground_interference_parallel_uniform(client, ms_object_keys, stripe_num, paras, failed_num - 10);
    }
    else if (failed_num >= 21 && failed_num <= 30)
    {
      // production workload
      // 21 single block, 22 two blocks, 23 three blocks...
      test_foreground_interference_parallel_production(client, ms_object_keys, stripe_num, paras, failed_num - 20);
    }
    else if (failed_num == 31)
    {
      test_normal_read_throughput(client, stripe_num, paras, ms_object_keys);
    }
    else if (failed_num == 41)
    {
      test_throughput_saturation(client, ms_object_keys);
    }

  }

  // delete
  client.delete_all_stripes();
}

int main(int argc, char **argv)
{
  if (argc != 4 && argc != 5)
  {
    std::cout << "./run_client config_file stripe_num failed_num" << std::endl;
    exit(0);
  }

  char buff[256];
  getcwd(buff, 256);
  std::string cwf = std::string(argv[0]);
  std::string path_prefix = std::string(buff) + cwf.substr(1, cwf.rfind('/') - 1);

  ParametersInfo paras;
  parse_args(nullptr, paras, path_prefix + "/../" + std::string(argv[1]));
  int stripe_num = std::stoi(argv[2]);

  int failed_num = std::stoi(argv[3]);
  //my_assert(0 <= failed_num && failed_num <= 2);
  bool optimized = true;
  if (argc == 5)
  {
    optimized = (std::string(argv[4]) == "true");
  }

  test_repair_performance(path_prefix, stripe_num, paras, failed_num, optimized);

  return 0;
}