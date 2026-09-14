#include "coordinator.h"

namespace ECProject
{
  void Coordinator::generate_placement_ec(unsigned int stripe_id)
  {
    Stripe &stripe = stripe_table_[stripe_id];
    int n = stripe.ec->k + stripe.ec->m;
    for (int i = 0; i < n; i++) {
      stripe.block_ids.push_back(cur_block_id_++);
    }
    stripe.ec->placement_rule = ec_schema_.placement_rule;
    stripe.ec->generate_partition();
    if (IF_DEBUG) {
      if (check_ec_family(ec_schema_.ec_type) == ECFAMILY::LRCs) {
        auto lrc = dynamic_cast<LocallyRepairableCode*>(stripe.ec);
        std::vector<std::vector<int>> groups;
        lrc->grouping_information(groups);
        auto msg = lrc->self_information();
        msg += "\n" + lrc->print_info(groups, "grouping");
        write_logs(Logger::LogLevel::DEBUG, msg);
      }
      auto msg = stripe.ec->print_info(stripe.ec->partition_plan, "partition");
      write_logs(Logger::LogLevel::DEBUG, msg);
    }

    int idx = merge_groups_.size() - 1;
    bool new_group = false;
    if (idx < 0 || merge_groups_[idx].size() == ec_schema_.x) {
      new_group = true;
    }

    if (ec_schema_.placement_rule == OPTIMAL &&
        ec_schema_.multistripe_placement_rule != RAND) {
      if (ec_schema_.multistripe_placement_rule == DISPERSED) {
        if (new_group) {
          free_clusters_.clear();
          for (unsigned int i = 0; i < num_of_clusters_; i++) {
            free_clusters_.push_back(i);
          }
        }
        int required_cluster_num = (int)stripe.ec->partition_plan.size();
        my_assert((int)free_clusters_.size() >= required_cluster_num);
        select_nodes_by_random(free_clusters_, stripe.blocks2nodes, stripe_id, required_cluster_num);
      } else if (ec_schema_.multistripe_placement_rule == AGGREGATED ||
                 ec_schema_.multistripe_placement_rule == VERTICAL) {
        if (new_group) {
          lucky_cid_ = random_index(num_of_clusters_);
        }
        select_nodes_in_order(stripe.blocks2nodes, stripe_id);
      } else if (ec_schema_.multistripe_placement_rule == HORIZONTAL) {
        if (new_group) {
          lucky_cid_ = random_index(num_of_clusters_);
          free_clusters_.clear();
          for (unsigned int i = 0; i < num_of_clusters_; i++) {
            if (i != lucky_cid_) {
              free_clusters_.push_back(i);
            }
          }
        }
        int required_cluster_num = (int)stripe.ec->partition_plan.size();
        my_assert((int)free_clusters_.size() >= required_cluster_num - 1);
        select_nodes_by_random(free_clusters_, stripe.blocks2nodes, stripe_id, required_cluster_num - 1);
      }
    } else {
      std::vector<unsigned int> free_clusters;
      for (unsigned int i = 0; i < num_of_clusters_; i++) {
        free_clusters.push_back(i);
      }
      int required_cluster_num = (int)stripe.ec->partition_plan.size();
      select_nodes_by_random(free_clusters, stripe.blocks2nodes, stripe_id, required_cluster_num);
    }
    if (new_group) {
      std::vector<unsigned int> temp;
      temp.push_back(stripe_id);
      merge_groups_.push_back(temp);
    } else {
      merge_groups_[idx].push_back(stripe_id);
    }

    if (IF_DEBUG) {
      print_placement_result("Generate placement:");
    }
  }

  void Coordinator::generate_placement_replica(unsigned int stripe_id)
  {
    // placement for replica 1
    Stripe &stripe = stripe_table_[stripe_id];
    int n = stripe.ec->k + stripe.ec->m;
    for (int i = 0; i < n; i++) {
      stripe.block_ids.push_back(cur_block_id_++);
    }
    stripe.ec->placement_rule = ec_schema_.placement_rule;
    stripe.ec->generate_partition();
    if (IF_DEBUG) {
      if (check_ec_family(ec_schema_.ec_type) == ECFAMILY::LRCs) {
        auto lrc = dynamic_cast<LocallyRepairableCode*>(stripe.ec);
        std::vector<std::vector<int>> groups;
        lrc->grouping_information(groups);
        auto msg = lrc->self_information();
        msg += "\n" + lrc->print_info(groups, "grouping");
        write_logs(Logger::LogLevel::DEBUG, msg);
      }
      auto msg = stripe.ec->print_info(stripe.ec->partition_plan, "partition");
      write_logs(Logger::LogLevel::DEBUG, msg);
    }

    int idx = merge_groups_.size() - 1;
    bool new_group = false;
    if (idx < 0 || merge_groups_[idx].size() == ec_schema_.x) {
      new_group = true;
    }

    if (ec_schema_.placement_rule == OPTIMAL &&
        ec_schema_.multistripe_placement_rule != RAND) {
      if (ec_schema_.multistripe_placement_rule == DISPERSED) {
        if (new_group) {
          free_clusters_.clear();
          for (unsigned int i = 0; i < num_of_clusters_; i++) {
            free_clusters_.push_back(i);
          }
        }
        int required_cluster_num = (int)stripe.ec->partition_plan.size();
        my_assert((int)free_clusters_.size() >= required_cluster_num);
        select_nodes_by_random(free_clusters_, stripe.blocks2nodes, stripe_id, required_cluster_num);
      } else if (ec_schema_.multistripe_placement_rule == AGGREGATED ||
                 ec_schema_.multistripe_placement_rule == VERTICAL) {
        if (new_group) {
          lucky_cid_ = random_index(num_of_clusters_);
        }
        select_nodes_in_order(stripe.blocks2nodes, stripe_id);
      } else if (ec_schema_.multistripe_placement_rule == HORIZONTAL) {
        if (new_group) {
          lucky_cid_ = random_index(num_of_clusters_);
          free_clusters_.clear();
          for (unsigned int i = 0; i < num_of_clusters_; i++) {
            if (i != lucky_cid_) {
              free_clusters_.push_back(i);
            }
          }
        }
        int required_cluster_num = (int)stripe.ec->partition_plan.size();
        my_assert((int)free_clusters_.size() >= required_cluster_num - 1);
        select_nodes_by_random(free_clusters_, stripe.blocks2nodes, stripe_id, required_cluster_num - 1);
      }
    } else {
      std::vector<unsigned int> free_clusters;
      for (unsigned int i = 0; i < num_of_clusters_; i++) {
        free_clusters.push_back(i);
      }
      int required_cluster_num = (int)stripe.ec->partition_plan.size();
      select_nodes_by_random(free_clusters, stripe.blocks2nodes, stripe_id, required_cluster_num);
    }
    if (new_group) {
      std::vector<unsigned int> temp;
      temp.push_back(stripe_id);
      merge_groups_.push_back(temp);
    } else {
      merge_groups_[idx].push_back(stripe_id);
    }

    std::vector<unsigned int> free_clusters;
    for (unsigned int i = 0; i < num_of_clusters_; i++) {
      free_clusters.push_back(i);
    }
    std::unordered_set<unsigned int> used_clusters;
    for (auto& nid : stripe.blocks2nodes) {
      used_clusters.insert(node_table_[nid].map2cluster);
    }
    for (auto& cid : used_clusters) {
      auto it = std::find(free_clusters.begin(), free_clusters.end(), cid);
      if (it != free_clusters.end()) {
        free_clusters.erase(it);
      }
    }

    // placement for replica 2
    int cluster_idx = random_index(free_clusters.size());
    unsigned int cluster_id = free_clusters[cluster_idx];
    Cluster &cluster = cluster_table_[cluster_id];
    std::vector<unsigned int> free_nodes;
    for (int i = 0; i < num_of_nodes_per_cluster_; i++) {
      free_nodes.push_back(cluster.nodes[i]);
    }
    size_t free_nodes_num = int(free_nodes.size());
    for (int i = 0; i < stripe.ec->k; i++) {
      my_assert(free_nodes_num);
      // randomly select a node
      int node_idx = random_index(free_nodes_num);
      unsigned int node_id = free_nodes[node_idx];
      stripe.replicas2.emplace_back(node_id);
      // remove the chosen node from the free list
      auto it_n = std::find(free_nodes.begin(), free_nodes.end(), node_id);
      free_nodes.erase(it_n);
      free_nodes_num--;
    }
    auto it_r = std::find(free_clusters.begin(), free_clusters.end(), cluster_id);
    free_clusters.erase(it_r);

    // placement for replica 3
    if (!ec_schema_.from_two_replica) {
      select_nodes_by_random_for_replica(free_clusters, stripe.replicas3, stripe_id);
    }

    if (IF_DEBUG) {
      print_placement_result_replica("Generate placement for replicas:", stripe_id);
    }
  }

  void Coordinator::select_nodes_by_random(
            std::vector<unsigned int>& free_clusters,
            std::vector<unsigned int>& blocks2nodes,
            unsigned int stripe_id, int split_idx)
  {
    Stripe &stripe = stripe_table_[stripe_id];
    int n = stripe.ec->k + stripe.ec->m;
    for (unsigned int i = 0; i < n; i++) {
      blocks2nodes.push_back(i);
    }

    // place each partition into a seperate cluster
    size_t free_clusters_num = free_clusters.size();
    int num_of_partitions = int(stripe.ec->partition_plan.size());
    for (int i = 0; i < split_idx; i++) {
      my_assert(free_clusters_num);
      // randomly select a cluster
      int cluster_idx = random_index(free_clusters_num);
      unsigned int cluster_id = free_clusters[cluster_idx];
      Cluster &cluster = cluster_table_[cluster_id];
      std::vector<unsigned int> free_nodes;
      for (int j = 0; j < num_of_nodes_per_cluster_; j++) {
        free_nodes.push_back(cluster.nodes[j]);
      }
      int partition_size = int(stripe.ec->partition_plan[i].size());
      size_t free_nodes_num = int(free_nodes.size());
      for (int j = 0; j < partition_size; j++) {
        my_assert(free_nodes_num);
        // randomly select a node
        int node_idx = random_index(free_nodes_num);
        unsigned int node_id = free_nodes[node_idx];
        int block_idx = stripe.ec->partition_plan[i][j];
        blocks2nodes[block_idx] = node_id;
        // remove the chosen node from the free list
        auto it_n = std::find(free_nodes.begin(), free_nodes.end(), node_id);
        free_nodes.erase(it_n);
        free_nodes_num--;
      }
      // remove the chosen cluster from the free list
      auto it_r = std::find(free_clusters.begin(), free_clusters.end(), cluster_id);
      free_clusters.erase(it_r);
      free_clusters_num--;
    }

    for (int i = split_idx; i < num_of_partitions; i++) {
      unsigned int cluster_id = lucky_cid_;
      Cluster& cluster = cluster_table_[cluster_id];
      std::vector<unsigned int> free_nodes;
      for (int j = 0; j < num_of_nodes_per_cluster_; j++) {
        free_nodes.push_back(cluster.nodes[j]);
      }
      int partition_size = int(stripe.ec->partition_plan[i].size());
      size_t free_nodes_num = free_nodes.size();
      for (int j = 0; j < partition_size; j++) {
        my_assert(free_nodes_num);
        // randomly select a node
        int node_idx = random_index(free_nodes_num);
        unsigned int node_id = free_nodes[node_idx];
        int block_idx = stripe.ec->partition_plan[i][j];
        blocks2nodes[block_idx] = node_id;
        // remove the chosen node from the free list
        auto it_n = std::find(free_nodes.begin(), free_nodes.end(), node_id);
        free_nodes.erase(it_n);
        free_nodes_num--;
      }
    }
  }

  void Coordinator::select_nodes_in_order(std::vector<unsigned int>& blocks2nodes, unsigned int stripe_id)
  {
    Stripe &stripe = stripe_table_[stripe_id];
    int n = stripe.ec->k + stripe.ec->m;
    for (unsigned int i = 0; i < n; i++) {
      blocks2nodes.push_back(i);
    }

    // place each partition into a seperate cluster
    int num_of_partitions = int(stripe.ec->partition_plan.size());
    for (int i = 0; i < num_of_partitions; i++) {
      unsigned int cluster_id = (lucky_cid_ + i) % num_of_clusters_;
      Cluster &cluster = cluster_table_[cluster_id];
      std::vector<unsigned int> free_nodes;
      for (int j = 0; j < num_of_nodes_per_cluster_; j++) {
        free_nodes.push_back(cluster.nodes[j]);
      }
      int partition_size = int(stripe.ec->partition_plan[i].size());
      size_t free_nodes_num = free_nodes.size();
      for (int j = 0; j < partition_size; j++) {
        my_assert(free_nodes_num);
        // randomly select a node
        int node_idx = random_index(free_nodes_num);
        unsigned int node_id = free_nodes[node_idx];
        int block_idx = stripe.ec->partition_plan[i][j];
        blocks2nodes[block_idx] = node_id;
        // remove the chosen node from the free list
        auto it_n = std::find(free_nodes.begin(), free_nodes.end(), node_id);
        free_nodes.erase(it_n);
        free_nodes_num--;
      }
    }
  }

  void Coordinator::select_nodes_by_random_for_replica(std::vector<unsigned int>& free_clusters,
                  std::vector<unsigned int>& blocks2nodes, unsigned int stripe_id)
  {
    Stripe &stripe = stripe_table_[stripe_id];
    for (unsigned int i = 0; i < stripe.ec->k; i++) {
      blocks2nodes.push_back(i);
    }

    // place each partition into a seperate cluster
    size_t free_clusters_num = free_clusters.size();
    int num_of_partitions = int(stripe.ec->partition_plan.size());
    for (int i = 0; i < num_of_partitions; i++) {
      my_assert(free_clusters_num);
      // randomly select a cluster
      int cluster_idx = random_index(free_clusters_num);
      unsigned int cluster_id = free_clusters[cluster_idx];
      Cluster &cluster = cluster_table_[cluster_id];
      std::vector<unsigned int> free_nodes;
      for (int j = 0; j < num_of_nodes_per_cluster_; j++) {
        free_nodes.push_back(cluster.nodes[j]);
      }
      int partition_size = int(stripe.ec->partition_plan[i].size());
      size_t free_nodes_num = int(free_nodes.size());
      for (int j = 0; j < partition_size; j++) {
        int block_idx = stripe.ec->partition_plan[i][j];
        if (block_idx >= stripe.ec->k) {
          continue;
        }

        my_assert(free_nodes_num);
        // randomly select a node
        int node_idx = random_index(free_nodes_num);
        unsigned int node_id = free_nodes[node_idx];
        blocks2nodes[block_idx] = node_id;
        // remove the chosen node from the free list
        auto it_n = std::find(free_nodes.begin(), free_nodes.end(), node_id);
        free_nodes.erase(it_n);
        free_nodes_num--;
      }
      // remove the chosen cluster from the free list
      auto it_r = std::find(free_clusters.begin(), free_clusters.end(), cluster_id);
      free_clusters.erase(it_r);
      free_clusters_num--;
    }
  }

  void Coordinator::print_placement_result(std::string msg)
  {
    std::string message = "\n" + msg + "\n";
    for (auto& kv : stripe_table_) {
      find_out_stripe_partitions(kv.first);
      message += "Stripe " + std::to_string(kv.first) + " block placement:\n";
      for (auto& vec : kv.second.ec->partition_plan) {
        unsigned int node_id = kv.second.blocks2nodes[vec[0]];
        unsigned int cluster_id = node_table_[node_id].map2cluster;
        message += "Cluster " + std::to_string(cluster_id) + ": ";
        for (int ele : vec) {
          message += "B" + std::to_string(ele) + "N" + std::to_string(kv.second.blocks2nodes[ele]) + " ";
        }
        message += "\n";
      }
    }
    message += "Merge Group: ";
    for (auto it1 = merge_groups_.begin(); it1 != merge_groups_.end(); it1++) {
      message += "[ ";
      for (auto it2 = (*it1).begin(); it2 != (*it1).end(); it2++) {
        message += std::to_string(*it2) + " ";
      }
      message += "] ";
    }
    message += "\n\n";
    write_logs(Logger::LogLevel::DEBUG, message);
  }

  void Coordinator::print_placement_result_replica(std::string msg, unsigned int stripe_id)
  {
    std::string message = "\n" + msg + "\n";
    find_out_stripe_partitions(stripe_id);
    Stripe& stripe = stripe_table_[stripe_id];
    message += "Stripe " + std::to_string(stripe_id) + " block placement:\n";
    for (auto& vec : stripe.ec->partition_plan) {
      unsigned int node_id = stripe.blocks2nodes[vec[0]];
      unsigned int cluster_id = node_table_[node_id].map2cluster;
      message += "Cluster " + std::to_string(cluster_id) + ": ";
      for (int ele : vec) {
        message += "B" + std::to_string(ele) + "N" + std::to_string(stripe.blocks2nodes[ele]) + " ";
      }
      message += "\n";
    }
    message += "Replica 2: ";
    int idx = 0;
    for (auto& ele : stripe.replicas2) {
      message += "B" + std::to_string(idx++) + 
                 "N" + std::to_string(ele) + 
                 "C" + std::to_string(node_table_[ele].map2cluster) +
                 " ";
    }
    if (!ec_schema_.from_two_replica) {
      message += "\nReplica 3: ";
      idx = 0;
      for (auto& ele : stripe.replicas3) {
        message += "B" + std::to_string(idx++) + 
                  "N" + std::to_string(ele) + 
                  "C" + std::to_string(node_table_[ele].map2cluster) +
                  " ";
      }
    }
    message += "\n";
    write_logs(Logger::LogLevel::DEBUG, message);
  }
}