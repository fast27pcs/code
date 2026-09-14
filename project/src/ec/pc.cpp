#include "pc.h"
#include <map>

using namespace ECProject;

void ProductCode::init_coding_parameters(CodingParameters cp)
{
	k1 = cp.k1;
	m1 = cp.m1;
	k2 = cp.k2;
	m2 = cp.m2;
	k = k1 * k2;
	m = (k1 + m1) * (k2 + m2) - k;
	row_code.k = k1;
	row_code.m = m1;
	col_code.k = k2;
	col_code.m = m2;
	local_or_column = cp.local_or_column;
	placement_rule = cp.placement_rule;
}

void ProductCode::get_coding_parameters(CodingParameters& cp)
{
	cp.k1 = k1;
	cp.m1 = m1;
	cp.k2 = k2;
	cp.m2 = m2;
	cp.k = k;
	cp.m = m;
	cp.local_or_column = local_or_column;
	cp.placement_rule = placement_rule;
}

/*
	PC: (k2 + m2) rows * (k1 + m1) cols
	encode_PC:
	data_ptrs: ordered by row
	D(0) ... D(k1*k2-1)
	coding_ptrs: ordered by row
	R(0) ... R(k2*m1-1) C(0) ...  C(m2*k1-1) G(0) ... G(m2*m1-1)
*/
void ProductCode::encode(char **data_ptrs, char **coding_ptrs, int block_size)
{
	// encode row parities
	for (int i = 0; i < k2; i++) {
		std::vector<char *> t_coding(m1);
		char **coding = (char **)t_coding.data();
		for (int j = 0; j < m1; j++) {
			coding[j] = coding_ptrs[i * m1 + j];
		}
		row_code.encode(&data_ptrs[i * k1], coding, block_size);
	}
	// encode column parities
	for (int i = 0; i < k1 + m1; i++) {
		std::vector<char *> t_data(k2);
		char **data = (char **)t_data.data();
		if (i < k1) {
			for (int j = 0; j < k2; j++) {
				data[j] = data_ptrs[j * k1 + i];
			}
		} else {
			for (int j = 0; j < k2; j++) {
				data[j] = coding_ptrs[j * m1 + i - k1];
			}
		}
		std::vector<char *> t_coding(m2);
		char **coding = (char **)t_coding.data();
		if (i < k1) {
			for (int j = 0; j < m2; j++) {
				coding[j] = coding_ptrs[k2 * m1 + j * k1 + i];
			}
		} else {
			for (int j = 0; j < m2; j++) {
				coding[j] = coding_ptrs[k2 * m1 + k1 * m2 + j * m1 + i - k1];
			}
		}
		col_code.encode(data, coding, block_size);
	}
}

// the index order is the same as shown above
void ProductCode::decode(char **data_ptrs, char **coding_ptrs, int block_size,
												 int *erasures, int failed_num)
{
	std::vector<std::vector<int>> failed_map(k2 + m2, std::vector<int>(k1 + m1, 0));
	std::vector<std::vector<char *>> blocks_map(k2 + m2, std::vector<char *>(k1 + m1, nullptr));
	std::vector<int> fb_row_cnt(k2 + m2, 0);
	std::vector<int> fb_col_cnt(k1 + m1, 0);

	int data_idx = 0;
	int parity_idx = 0;
	for (int i = 0; i < k2; i++) {
		for (int j = 0; j < k1 + m1; j++) {
			if (j < k1) {
				blocks_map[i][j] = data_ptrs[data_idx++];
			} else {
				blocks_map[i][j] = coding_ptrs[parity_idx++];
			}
		}
	}
	int g_parity_idx = k2 * m1 + m2 * k1;
	for (int i = k2; i < k2 + m2; i++) {
		for (int j = 0; j < k1 + m1; j++) {
			if (j < k1) {
				blocks_map[i][j] = coding_ptrs[parity_idx++];
			} else {
				blocks_map[i][j] = coding_ptrs[g_parity_idx++];
			}
		}
	}

	for (int i = 0; i < failed_num; i++) {
		int row = -1, col = -1;
		int failed_idx = erasures[i];
		bid2rowcol(failed_idx, row, col);
		failed_map[row][col] = 1;
		fb_row_cnt[row]++;
		fb_col_cnt[col]++;
	}

	while (failed_num > 0) {
		// part one
		for (int i = 0; i < k1 + m1; i++) {
			if (fb_col_cnt[i] <= m2 && fb_col_cnt[i] > 0) {	// repair on column
				std::vector<char *> data(k2, nullptr);
				std::vector<char *> coding(m2, nullptr);
				int *erasure = new int[fb_col_cnt[i] + 1];
				erasure[fb_col_cnt[i]] = -1;
				int cnt = 0;
				for (int jj = 0; jj < k2; jj++) {
					if (failed_map[jj][i]) {
						erasure[cnt++] = jj;
					}
					data[jj] = blocks_map[jj][i];
				}
				for (int jj = 0; jj < m2; jj++) {
					if (failed_map[jj + k2][i]) {
						erasure[cnt++] = jj + k2;
					}
					coding[jj] = blocks_map[jj + k2][i];
				}
				col_code.decode(data.data(), coding.data(), block_size, erasure, fb_col_cnt[i]);
				// update failed_map
				for (int jj = 0; jj < k2 + m2; jj++) {
					if (failed_map[jj][i]) {
						failed_map[jj][i] = 0;
						failed_num -= 1;
						fb_row_cnt[jj] -= 1;
						fb_col_cnt[i] -= 1;
					}
				}
				delete erasure;
			}
		}
		if (failed_num == 0) {
			break;
		}
		// part two
		int max_row = -1;
		for (int i = 0; i < k2 + m2; i++) {
			if (fb_row_cnt[i] <= m1 && fb_row_cnt[i] > 0) { // repair on row
				max_row = i;
				std::vector<char *> data(k1, nullptr);
				std::vector<char *> coding(m1, nullptr);
				int *erasure = new int[fb_row_cnt[i] + 1];
				erasure[fb_row_cnt[i]] = -1;
				int cnt = 0;
				for (int jj = 0; jj < k1; jj++) {
					if (failed_map[i][jj]) {
						erasure[cnt++] = jj;
					}
					data[jj] = blocks_map[i][jj];
				}
				for (int jj = 0; jj < m1; jj++) {
					if (failed_map[i][jj + k1]) {
						erasure[cnt++] = jj + k1;
					}
					coding[jj] = blocks_map[i][jj + k1];
				}
				row_code.decode(data.data(), coding.data(), block_size, erasure, fb_row_cnt[i]);
				// update failed_map
				for (int jj = 0; jj < k1 + m1; jj++) {
					if (failed_map[i][jj]) {
						failed_map[i][jj] = 0;
						failed_num -= 1;
						fb_row_cnt[i] -= 1;
						fb_col_cnt[jj] -= 1;
					}
				}
				delete erasure;
			}
		}
		if (max_row == -1) {
			std::cout << "Undecodable!!" << std::endl;
			return;
		}
	}
}

// the index order is the same as shown above
bool ProductCode::check_if_decodable(const std::vector<int>& failure_idxs)
{
	int failed_num = (int)failure_idxs.size();
	std::vector<std::vector<int>> failed_map(k2 + m2, std::vector<int>(k1 + m1, 0));
	std::vector<int> fb_row_cnt(k2 + m2, 0);
	std::vector<int> fb_col_cnt(k1 + m1, 0);

	for (int i = 0; i < failed_num; i++)
	{
		int row = -1, col = -1;
		int failed_idx = failure_idxs[i];
		bid2rowcol(failed_idx, row, col);
		failed_map[row][col] = 1;
		fb_row_cnt[row]++;
		fb_col_cnt[col]++;
	}

	while (failed_num > 0)
	{
		// part one
		for (int i = 0; i < k1 + m1; i++) {
			if (fb_col_cnt[i] <= m2 && fb_col_cnt[i] > 0) { // repair on column
				// update failed_map
				for (int jj = 0; jj < k2 + m2; jj++) {
					if (failed_map[jj][i]) {
						failed_map[jj][i] = 0;
						failed_num -= 1;
						fb_row_cnt[jj] -= 1;
						fb_col_cnt[i] -= 1;
					}
				}
			}
		}
		if (failed_num == 0) {
			break;
		}
		// part two
		int max_row = -1;
		for (int i = 0; i < k2 + m2; i++) {
			if (fb_row_cnt[i] <= m1 && fb_row_cnt[i] > 0) {// repair on row
				max_row = i;
				// update failed_map
				for (int jj = 0; jj < k1 + m1; jj++) {
					if (failed_map[i][jj]) {
						failed_map[i][jj] = 0;
						failed_num -= 1;
						fb_row_cnt[i] -= 1;
						fb_col_cnt[jj] -= 1;
					}
				}
			}
		}
		if (max_row == -1) {
			return false;
		}
	}
	return true;
}

void ProductCode::encode_partial_blocks(
				char **data_ptrs, char **coding_ptrs, int block_size,
				const std::vector<int>& data_idxs, const std::vector<int>& parity_idxs,
				const std::vector<int>& failure_idxs, const std::vector<int>& live_idxs,
				std::vector<bool>& partial_flags, bool partial_scheme)
{
	int row = -1, col = -1;
	auto data_idxs_copy = data_idxs;
	auto parity_idxs_copy = parity_idxs;
	auto failure_idxs_copy = failure_idxs;
	if (local_or_column) {
		for (auto& idx : data_idxs_copy) {
			bid2rowcol(idx, row, col);
			idx = row;
		}
		for (auto& idx : parity_idxs_copy) {
			bid2rowcol(idx, row, col);
			idx = row;
		}
		for (auto& idx : failure_idxs_copy) {
			bid2rowcol(idx, row, col);
			idx = row;
		}
		col_code.encode_partial_blocks(data_ptrs, coding_ptrs,
				block_size, data_idxs_copy, parity_idxs_copy, failure_idxs_copy,
				live_idxs, partial_flags, partial_scheme);
	} else {
		for (auto& idx : data_idxs_copy) {
			bid2rowcol(idx, row, col);
			idx = col;
		}
		for (auto& idx : parity_idxs_copy) {
			bid2rowcol(idx, row, col);
			idx = col;
		}
		for (auto& idx : failure_idxs_copy) {
			bid2rowcol(idx, row, col);
			idx = col;
		}
		row_code.encode_partial_blocks(data_ptrs, coding_ptrs,
				block_size, data_idxs_copy, parity_idxs_copy, failure_idxs_copy,
				live_idxs, partial_flags, partial_scheme);
	}
}

void ProductCode::decode_with_partial_blocks(
				char **data_ptrs, char **coding_ptrs, int block_size,
				const std::vector<int>& failure_idxs, const std::vector<int>& parity_idxs)
{
	int row = -1, col = -1;
	auto failure_idxs_copy = failure_idxs;
	auto parity_idxs_copy = parity_idxs;
	if (local_or_column) {
		for (auto& idx : failure_idxs_copy) {
			bid2rowcol(idx, row, col);
			idx = row;
		}
		for (auto& idx : parity_idxs_copy) {
			bid2rowcol(idx, row, col);
			idx = row;
		}
		col_code.decode_with_partial_blocks(data_ptrs, coding_ptrs,
				block_size, failure_idxs_copy, parity_idxs_copy);
	} else {
		for (auto& idx : failure_idxs_copy) {
			bid2rowcol(idx, row, col);
			idx = col;
		}
		for (auto& idx : parity_idxs_copy) {
			bid2rowcol(idx, row, col);
			idx = col;
		}
		row_code.decode_with_partial_blocks(data_ptrs, coding_ptrs,
				block_size, failure_idxs_copy, parity_idxs_copy);
	}
}

int ProductCode::num_of_partial_blocks_to_transfer(
				const std::vector<int>& data_idxs, const std::vector<int>& parity_idxs)
{
	int row = -1, col = -1;
	auto data_idxs_copy = data_idxs;
	auto parity_idxs_copy = parity_idxs;
	if (local_or_column) {
		for (auto& idx : data_idxs_copy) {
			bid2rowcol(idx, row, col);
			idx = row;
		}
		for (auto& idx : parity_idxs_copy) {
			bid2rowcol(idx, row, col);
			idx = row;
		}
		return col_code.num_of_partial_blocks_to_transfer(data_idxs_copy, parity_idxs_copy);
	} else {
		for (auto& idx : data_idxs_copy) {
			bid2rowcol(idx, row, col);
			idx = col;
		}
		for (auto& idx : parity_idxs_copy) {
			bid2rowcol(idx, row, col);
			idx = col;
		}
		return row_code.num_of_partial_blocks_to_transfer(data_idxs_copy, parity_idxs_copy);
	}
}

int ProductCode::rowcol2bid(int row, int col)
{
	int bid = -1;
	if (row < k2 && col < k1) {	 // data block
		bid = row * k1 + col;
	} else if (row < k2 && col >= k1) {		// row parity block
		bid = k1 * k2 + row * m1 + (col - k1);
	} else if (row >= k2 && col < k1) {		// column parity block
		bid = (k1 + m1) * k2 + (row - k2) * k1 + col;
	} else {	// global parity block
		bid = (k1 + m1) * k2 + k1 * m2 + (row - k2) * m1 + (col - k1);
	}
	return bid;
}

void ProductCode::bid2rowcol(int bid, int &row, int &col)
{
	if (bid < k1 * k2) {	// data block
		row = bid / k1;
		col = bid % k1;
	} else if (bid >= k1 * k2 && bid < (k1 + m1) * k2) {	// row parity block
		int tmp_id = bid - k1 * k2;
		row = tmp_id / m1;
		col = tmp_id % m1 + k1;
	} else if (bid >= (k1 + m1) * k2 && bid < (k1 + m1) * k2 + k1 * m2) {	// column parity block
		int tmp_id = bid - (k1 + m1) * k2;
		row = tmp_id / k1 + k2;
		col = tmp_id % k1;
	} else {	// global parity block
		int tmp_id = bid - (k1 + m1) * k2 - k1 * m2;
		row = tmp_id / m1 + k2;
		col = tmp_id % m1 + k1;
	}
}

int ProductCode::oldbid2newbid_for_merge(int old_block_id, int x, int seri_num, bool isvertical)
{
	int new_block_id = -1;
	int row = -1, col = -1;
	bid2rowcol(old_block_id, row, col);
	if (isvertical) {	// for data blocks and row parity blocks
		row += seri_num * k2;
		ProductCode pc(k1, m1, x * k2, m2);
		new_block_id = pc.rowcol2bid(row, col);
	} else {	// for data blocks and column parity blocks
		col += seri_num * k1;
		ProductCode pc(x * k1, m1, k2, m2);
		new_block_id = pc.rowcol2bid(row, col);
	}
	return new_block_id;
}

// 
void ProductCode::generate_partition()
{
	row_code.partition_plan.clear();
    for (int i = 0; i < k1 + m1; i++) {
        row_code.partition_plan.push_back({i});
    }

    // 
    col_code.partition_plan.clear();
    for (int i = 0; i < k2 + m2; i++) {
        col_code.partition_plan.push_back({i});
    }

    partition_plan.clear();
    
//    if (placement_rule == FLAT) {
//        partition_flat();
//    } else if (placement_rule == RANDOM) {
//        partition_random();
//    } else if (placement_rule == OPTIMAL) {
//        partition_optimal();
//    }
    // === 2D-PC ===
    if (placement_rule == ABLOCK) {
        partition_ablock();
    } else if (placement_rule == ACOL) {
        partition_acol();
    } else if (placement_rule == AROW) {
        partition_arow();
    } else if (placement_rule == M1COL) {
        partition_m1col();
    } else if (placement_rule == M2ROW) {
        partition_m2row();
    } else if (placement_rule == TILE) {
        partition_tile();
    } else if (placement_rule == DIAGONAL) {
        partition_diagonal();
    }
}

void ProductCode::partition_flat()
{
	row_code.partition_plan.clear();
	int n = k + m;
  for (int i = 0; i < n; i++) {
    partition_plan.push_back({i});
  }
	for (int i = 0; i < k1 + m1; i++) {
		row_code.partition_plan.push_back({i});
	}
}

void ProductCode::partition_random()
{
	row_code.partition_plan.clear();
	int n = k1 + m1;
	std::vector<int> columns;
  for (int i = 0; i < n; i++) {
    columns.push_back(i);
  }

	int cnt = 0;
  int cnt_left = n;
  while (cnt < n) {
		// at most every m1 columns of blocks in a partition
    int random_columns_num = random_range(1, m1);  
    int columns_num = std::min(random_columns_num, n - cnt);
    std::vector<int> partition;
		std::vector<int> row_partition;
    for (int i = 0; i < columns_num; i++, cnt++) {
      int ran_idx = random_index(n - cnt);
      int col = columns[ran_idx];
      for (int row = 0; row < k2 + m2; row++) {
				int block_idx = rowcol2bid(row, col);
				partition.push_back(block_idx);
			}
			row_partition.push_back(col);
      auto it = std::find(columns.begin(), columns.end(), col);
      columns.erase(it);
    }
    partition_plan.push_back(partition);
		row_code.partition_plan.push_back(row_partition);
  }
}

void ProductCode::partition_optimal()
{
	row_code.partition_plan.clear();
	int n = k1 + m1;
	int cnt = 0;
	while (cnt < n) {
		// every m columns of blocks in a partition
		int columns_num = std::min(m1, n - cnt);
		std::vector<int> partition;
		std::vector<int> row_partition;
    for(int i = 0; i < columns_num; i++, cnt++) {
			for (int row = 0; row < k2 + m2; row++) {
				int block_idx = rowcol2bid(row, cnt);
				partition.push_back(block_idx);
			}
			row_partition.push_back(cnt);
		}
		partition_plan.push_back(partition);
		row_code.partition_plan.push_back(row_partition);
	}
}

// 1: A_BLOCK 
void ProductCode::partition_ablock() {
    partition_plan.clear();
    int rows = k2 + m2;
    int cols = k1 + m1;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            std::vector<int> rack;
            rack.push_back(rowcol2bid(r, c));
            partition_plan.push_back(rack);
        }
    }
}

// 2: A_COL
void ProductCode::partition_acol() {
    partition_plan.clear();
    int rows = k2 + m2;
    int cols = k1 + m1;
    for (int c = 0; c < cols; ++c) {
        std::vector<int> rack;
        for (int r = 0; r < rows; ++r) {
            rack.push_back(rowcol2bid(r, c));
        }
        partition_plan.push_back(rack);
    }
}

// 3: A_ROW 
void ProductCode::partition_arow() {
    partition_plan.clear();
    int rows = k2 + m2;
    int cols = k1 + m1;
    for (int r = 0; r < rows; ++r) {
        std::vector<int> rack;
        for (int c = 0; c < cols; ++c) {
            rack.push_back(rowcol2bid(r, c));
        }
        partition_plan.push_back(rack);
    }
}

// 4: M1_COL 
void ProductCode::partition_m1col() {
    partition_plan.clear();
    int rows = k2 + m2;
    int cols = k1 + m1;
    int rack_count = (cols + m1 - 1) / m1; 
    for (int i = 0; i < rack_count; ++i) {
        std::vector<int> rack;
        int cstart = i * m1;
        int cend = std::min(cols, cstart + m1) - 1;
        for (int c = cstart; c <= cend; ++c) {
            for (int r = 0; r < rows; ++r) {
                rack.push_back(rowcol2bid(r, c));
            }
        }
        partition_plan.push_back(rack);
    }
}

// 5: M2_ROW 
void ProductCode::partition_m2row() {
    partition_plan.clear();
    int rows = k2 + m2;
    int cols = k1 + m1;
    int rack_count = (rows + m2 - 1) / m2; 
    for (int i = 0; i < rack_count; ++i) {
        std::vector<int> rack;
        int rstart = i * m2;
        int rend = std::min(rows, rstart + m2) - 1;
        for (int r = rstart; r <= rend; ++r) {
            for (int c = 0; c < cols; ++c) {
                rack.push_back(rowcol2bid(r, c));
            }
        }
        partition_plan.push_back(rack);
    }
}

// 6: PSEUDO-MATRIX
void ProductCode::partition_tile() {
    partition_plan.clear();
    int rows = k2 + m2;
    int cols = k1 + m1;
    
    
    int h = m2 + 1;
    int w = m1 + 1;
    

    int rack_rows = (rows + h - 1) / h;
    int rack_cols = (cols + w - 1) / w;

    int rack_count = rack_rows * rack_cols + 1;
   
    std::vector<std::vector<int>> racks(rack_count);

   
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int I = r / h;
            int J = c / w; 
            int i = r % h; 
            int j = c % w;

           
            int actual_h = std::min(h, rows - I * h);
            int actual_w = std::min(w, cols - J * w);
          
            bool is_full_submatrix = (actual_h == h && actual_w == w);

            // Calculate dynamic offset to avoid dimension blocking
            int i_carve = J % h;
            int j_carve = I % w;

            int target_rack_idx = 0;
            
            // 
            if (is_full_submatrix && i == i_carve && j == j_carve) {
                target_rack_idx = rack_count - 1;
            } else {
                // Otherwise, store in the corresponding sub-matrix rack
                target_rack_idx = I * rack_cols + J;
                if (target_rack_idx >= rack_count - 1) {
                    target_rack_idx = rack_count - 2; // Safeguard boundary
                }
            }
            
            //
            racks[target_rack_idx].push_back(rowcol2bid(r, c));
        }
    }

    //
    for (int idx = 0; idx < rack_count; ++idx) {
        if (!racks[idx].empty()) {
            partition_plan.push_back(racks[idx]);
        }
    }
}

// 7: DIAGONAL 
void ProductCode::partition_diagonal() {
    partition_plan.clear();
    int rows = k2 + m2;
    int cols = k1 + m1;
    int rack_count = std::max(rows, cols);
    
    
    std::vector<std::vector<int>> racks(rack_count);
    
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int rack_id = (r + c) % rack_count;
            racks[rack_id].push_back(rowcol2bid(r, c));
        }
    }
    
    
    for (int i = 0; i < rack_count; ++i) {
        if (!racks[i].empty()) {
            partition_plan.push_back(racks[i]);
        }
    }
}


std::string ProductCode::self_information()
{
	return "PC(" + std::to_string(k1) + "," + std::to_string(m1) + "," + \
				 std::to_string(k2) + "," + std::to_string(m2) + ")";
}

std::string ProductCode::type()
{
	return "PC";
}

bool ProductCode::generate_repair_plan(const std::vector<int>& failure_idxs,
                                       std::vector<RepairPlan>& plans,
                                       bool partial_scheme,
                                       bool repair_priority,
                                       bool repair_method)
{
	if (placement_rule == ABLOCK) {
        return repair_ablock(failure_idxs, plans, partial_scheme);
    }
    else if (placement_rule == ACOL) {        
        return repair_acol(failure_idxs, plans, partial_scheme);
    } 
    else if (placement_rule == AROW) {
        return repair_arow(failure_idxs, plans, partial_scheme);
    }
	else if (placement_rule == M1COL) {        
        return repair_m1col(failure_idxs, plans, partial_scheme);
    }
	else if (placement_rule == M2ROW) {        
        return repair_m2row(failure_idxs, plans, partial_scheme);
    }
	else if (placement_rule == TILE) {        
        return repair_tile(failure_idxs, plans, partial_scheme);
    }   
    else if (placement_rule == DIAGONAL) {
        return repair_diagonal(failure_idxs, plans, partial_scheme);
    }
  return false;
}


bool ProductCode::repair_ablock(const std::vector<int>& failure_idxs,
                                std::vector<RepairPlan>& plans,
                                bool partial_scheme)
{
    int failed_num = (int)failure_idxs.size();
    std::vector<std::vector<int>> failed_map(k2 + m2, std::vector<int>(k1 + m1, 0));
    std::vector<int> fb_row_cnt(k2 + m2, 0);
    std::vector<int> fb_col_cnt(k1 + m1, 0);

    // 1.
    for (int i = 0; i < failed_num; i++) {
        int row = -1, col = -1;
        bid2rowcol(failure_idxs[i], row, col);
        failed_map[row][col] = 1;
        fb_row_cnt[row]++;
        fb_col_cnt[col]++;
    }

    int max_bid = (k1 + m1) * (k2 + m2);
    std::vector<int> block_to_rack(max_bid, -1);
    for (size_t rk = 0; rk < partition_plan.size(); ++rk) {
        for (int bid : partition_plan[rk]) {
            if (bid >= 0 && bid < max_bid) {
                block_to_rack[bid] = rk;
            }
        }
    }    

    // 2. 
    while (failed_num > 0) {
        double best_ratio = std::numeric_limits<double>::max();
        int best_line = -1;
        bool is_row = true;
        int best_e = 0;

        //  (k1+e-1 / e)
        for (int i = 0; i < k2 + m2; i++) {
            int e = fb_row_cnt[i];
            if (e > 0 && e <= m1) {
                double ratio = (double)(k1+e-1) / e; // A_BLOCK 的跨架流量固定为 k1
                if (ratio < best_ratio || (abs(ratio - best_ratio) < 1e-6 && e > best_e)) {
                    best_ratio = ratio; best_line = i; is_row = true; best_e = e;
                }
            }
        }

        //  (k2+e-1 / e)
        for (int j = 0; j < k1 + m1; j++) {
            int e = fb_col_cnt[j];
            if (e > 0 && e <= m2) {
                double ratio = (double)(k2+e-1) / e; // A_BLOCK 的跨架流量固定为 k2
                if (ratio < best_ratio || (abs(ratio - best_ratio) < 1e-6 && e > best_e)) {
                    best_ratio = ratio; best_line = j; is_row = false; best_e = e;
                }
            }
        }

    
        if (best_line == -1) {
            return false; 
        }

        // 3. 
        RepairPlan plan;
        plan.local_or_column = !is_row;
        std::vector<int> tmp_failure_idxs;

        if (is_row) {
            for (int c = 0; c < k1 + m1; c++) {
                if (failed_map[best_line][c]) tmp_failure_idxs.push_back(c);
            }
            
            std::map<int, std::vector<int>> rack_to_cols;
            for (int c = 0; c < k1 + m1; ++c) {
                int bid = rowcol2bid(best_line, c);
                rack_to_cols[block_to_rack[bid]].push_back(c);
            }
            std::vector<std::vector<int>> dynamic_1d_plan;
            for (auto const& pair : rack_to_cols) dynamic_1d_plan.push_back(pair.second);
            
            row_code.partition_plan = dynamic_1d_plan;
            row_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
                    plan.parity_idxs, plan.help_blocks, partial_scheme);

            row_code.partition_plan.clear();
            for (int c = 0; c < k1 + m1; c++) row_code.partition_plan.push_back({c});

            for (auto& p_idx : plan.parity_idxs) p_idx = rowcol2bid(best_line, p_idx);
            for (auto& vec : plan.help_blocks) {
                for (auto& h_idx : vec) h_idx = rowcol2bid(best_line, h_idx);
            }
            for (int c : tmp_failure_idxs) {
                plan.failure_idxs.push_back(rowcol2bid(best_line, c));
                failed_map[best_line][c] = 0;
                failed_num--; fb_row_cnt[best_line]--; fb_col_cnt[c]--;
            }
        } else {
            for (int r = 0; r < k2 + m2; r++) {
                if (failed_map[r][best_line]) tmp_failure_idxs.push_back(r);
            }
            
            std::map<int, std::vector<int>> rack_to_rows;
            for (int r = 0; r < k2 + m2; ++r) {
                int bid = rowcol2bid(r, best_line);
                rack_to_rows[block_to_rack[bid]].push_back(r);
            }
            std::vector<std::vector<int>> dynamic_1d_plan;
            for (auto const& pair : rack_to_rows) dynamic_1d_plan.push_back(pair.second);
            
            col_code.partition_plan = dynamic_1d_plan;
            col_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
                    plan.parity_idxs, plan.help_blocks, partial_scheme);
            col_code.partition_plan.clear();
            for (int r = 0; r < k2 + m2; r++) col_code.partition_plan.push_back({r});
        

            for (auto& p_idx : plan.parity_idxs) p_idx = rowcol2bid(p_idx, best_line);
            for (auto& vec : plan.help_blocks) {
                for (auto& h_idx : vec) h_idx = rowcol2bid(h_idx, best_line);
            }
            for (int r : tmp_failure_idxs) {
                plan.failure_idxs.push_back(rowcol2bid(r, best_line));
                failed_map[r][best_line] = 0;
                failed_num--; fb_row_cnt[r]--; fb_col_cnt[best_line]--;
            }
        }

        // 4. 
        reorganize_help_blocks_by_rack(plan.help_blocks);
        plans.push_back(plan);
    }
    return true;
}

/*
bool ProductCode::repair_acol(const std::vector<int>& failure_idxs,
                              std::vector<RepairPlan>& plans,
                              bool partial_scheme)
{
    int failed_num = (int)failure_idxs.size();
 
    std::vector<std::vector<int>> failed_map(k2 + m2, std::vector<int>(k1 + m1, 0));
    std::vector<int> fb_row_cnt(k2 + m2, 0);
    std::vector<int> fb_col_cnt(k1 + m1, 0);

    for (int i = 0; i < failed_num; i++) {
        int row = -1, col = -1;
        int failed_idx = failure_idxs[i];
        bid2rowcol(failed_idx, row, col);
        failed_map[row][col] = 1;
        fb_row_cnt[row]++;
        fb_col_cnt[col]++;
    }

    while (failed_num > 0) {

		// part one
		for (int i = 0; i < k1 + m1; i++) {
			if (fb_col_cnt[i] <= m2 && fb_col_cnt[i] > 0) {	// repair on column
				RepairPlan plan;
				plan.local_or_column = true;
				int cnt = 0;
				std::vector<int> help_block;
				for (int jj = k2; jj < k2 + m2; jj++) {
					if (failed_map[jj][i]) {
						plan.parity_idxs.push_back(rowcol2bid(jj, i));
					}
				}
				for (int jj = 0; jj < k2 + m2; jj++) {
					if (cnt == k2) {
						break;
					}
					if (!failed_map[jj][i]) {
						help_block.push_back(rowcol2bid(jj, i));
						cnt++;
						if (jj >= k2) {
							plan.parity_idxs.push_back(rowcol2bid(jj, i));
						}
					}
				}
				plan.help_blocks.push_back(help_block);

				// update failed_map
				for (int jj = 0; jj < k2 + m2; jj++) {
					if (failed_map[jj][i]) {
						plan.failure_idxs.push_back(rowcol2bid(jj, i));
						failed_map[jj][i] = 0;
						failed_num -= 1;
						fb_row_cnt[jj] -= 1;
						fb_col_cnt[i] -= 1;
					}
				}
				plans.push_back(plan);
			}
		}
		if (failed_num == 0) {
			break;
		}


        // Part Two: 
        int best_row = -1;
        int max_e = 0;
        
        // 
        for (int i = 0; i < k2 + m2; i++) {
            int e = fb_row_cnt[i];
            if (e > 0 && e <= m1 && e > max_e) { 
                best_row = i;
                max_e = e; 
            }
        }

        if (best_row != -1) {
            RepairPlan plan;
            plan.local_or_column = false; 
            std::vector<int> tmp_failure_idxs;
            
            for (int jj = 0; jj < k1 + m1; jj++) {
                if (failed_map[best_row][jj]) {
                    tmp_failure_idxs.push_back(jj);
                }
            }
			////////////////////////////////////////
			
			row_code.partition_plan.clear();
			for (int i = 0; i < k1 + m1; i++) {
				row_code.partition_plan.push_back({i});
			}
			///////////////////////////////////////
            
            // 1. 
            row_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
                    plan.parity_idxs, plan.help_blocks, partial_scheme);
            
			int par_num = (int)plan.help_blocks.size();
			for (int ii = 0; ii < par_num; ii++) {
				for (auto it = plan.help_blocks[ii].begin(); it != plan.help_blocks[ii].end(); it++) {
					*it = rowcol2bid(best_row, *it);
				}
			}
			for (auto it = plan.parity_idxs.begin(); it != plan.parity_idxs.end(); it++) {
				*it = rowcol2bid(best_row, *it);
			}

			// update failed_map
			for (int jj = 0; jj < k1 + m1; jj++) {
				if (failed_map[best_row][jj]) {
					plan.failure_idxs.push_back(rowcol2bid(best_row, jj));
					failed_map[best_row][jj] = 0;
					failed_num -= 1;
					fb_row_cnt[best_row] -= 1;
					fb_col_cnt[jj] -= 1;
				}
			}
			plans.push_back(plan);
        }

    
        if (best_row == -1) {
			std::cout << "Undecodable!!" << std::endl;
			return false;
        }
    }
    return true;
}
*/

bool ProductCode::repair_acol(const std::vector<int>& failure_idxs,
                              std::vector<RepairPlan>& plans,
                              bool partial_scheme)
{
    int failed_num = (int)failure_idxs.size();
    // 
    std::vector<std::vector<int>> failed_map(k2 + m2, std::vector<int>(k1 + m1, 0));
    std::vector<int> fb_row_cnt(k2 + m2, 0);
    std::vector<int> fb_col_cnt(k1 + m1, 0);

    for (int i = 0; i < failed_num; i++) {
        int row = -1, col = -1;
        int failed_idx = failure_idxs[i];
        bid2rowcol(failed_idx, row, col);
        failed_map[row][col] = 1;
        fb_row_cnt[row]++;
        fb_col_cnt[col]++;
    }

    int max_bid = (k1 + m1) * (k2 + m2);
    std::vector<int> block_to_rack(max_bid, -1);
    for (size_t rk = 0; rk < partition_plan.size(); ++rk) {
        for (int bid : partition_plan[rk]) {
            if (bid >= 0 && bid < max_bid) {
                block_to_rack[bid] = rk;
            }
        }
    }

    while (failed_num > 0) {
        bool progressed = false;

        // =========================================================
        // Part One: 
        // =========================================================
        for (int i = 0; i < k1 + m1; i++) {
            if (fb_col_cnt[i] <= m2 && fb_col_cnt[i] > 0) { 
                RepairPlan plan;
                plan.local_or_column = true;
                std::vector<int> tmp_failure_idxs;
                
                for (int jj = 0; jj < k2 + m2; jj++) {
                    if (failed_map[jj][i]) {
                        tmp_failure_idxs.push_back(jj);
                    }
                }
                
              
                col_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
                        plan.parity_idxs, plan.help_blocks, partial_scheme);
                
               
                for (auto& p_idx : plan.parity_idxs) p_idx = rowcol2bid(p_idx, i);
                for (auto& vec : plan.help_blocks) {
                    for (auto& h_idx : vec) h_idx = rowcol2bid(h_idx, i);
                }

                
                reorganize_help_blocks_by_rack(plan.help_blocks);

                
                for (int r : tmp_failure_idxs) {
                    plan.failure_idxs.push_back(rowcol2bid(r, i));
                    failed_map[r][i] = 0;
                    failed_num -= 1;
                    fb_row_cnt[r] -= 1;
                    fb_col_cnt[i] -= 1;
                }
                plans.push_back(plan);
                progressed = true;
            }
        }

        
		if (failed_num == 0) {
            break; 
        } 

        // =========================================================
        // Part Two: 
        // =========================================================
        int best_row = -1;
        int max_e = 0;
        
        
        for (int i = 0; i < k2 + m2; i++) {
            int e = fb_row_cnt[i];
            if (e > 0 && e <= m1 && e > max_e) { 
                best_row = i;
                max_e = e; 
            }
        }

        if (best_row != -1) {
            RepairPlan plan;
            plan.local_or_column = false; 
            std::vector<int> tmp_failure_idxs;
            
            for (int jj = 0; jj < k1 + m1; jj++) {
                if (failed_map[best_row][jj]) {
                    tmp_failure_idxs.push_back(jj);
                }
            }
            
            std::map<int, std::vector<int>> rack_to_cols;
            for (int c = 0; c < k1 + m1; ++c) {
                int bid = rowcol2bid(best_row, c);
                rack_to_cols[block_to_rack[bid]].push_back(c);
            }
            std::vector<std::vector<int>> dynamic_1d_plan;
            for (auto const& pair : rack_to_cols) {
                dynamic_1d_plan.push_back(pair.second);
            }
            
            
            row_code.partition_plan = dynamic_1d_plan;
            
           
            row_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
                    plan.parity_idxs, plan.help_blocks, partial_scheme);
                    
           
            row_code.partition_plan.clear();
            for (int c = 0; c < k1 + m1; c++) {
                row_code.partition_plan.push_back({c});
            }
            
          
            for (auto& p_idx : plan.parity_idxs) p_idx = rowcol2bid(best_row, p_idx);
            for (auto& vec : plan.help_blocks) {
                for (auto& h_idx : vec) h_idx = rowcol2bid(best_row, h_idx);
            }

           
            reorganize_help_blocks_by_rack(plan.help_blocks);

          
            for (int c : tmp_failure_idxs) {
                plan.failure_idxs.push_back(rowcol2bid(best_row, c));
                failed_map[best_row][c] = 0;
                failed_num -= 1;
                fb_row_cnt[best_row] -= 1;
                fb_col_cnt[c] -= 1;
            }
            plans.push_back(plan);
            progressed = true;
        }

        
        if (!progressed) {
            std::cout << "Undecodable!!" << std::endl;
            return false;
        }
    }
    return true;
}

bool ProductCode::repair_arow(const std::vector<int>& failure_idxs,
                              std::vector<RepairPlan>& plans,
                              bool partial_scheme)
{
    int failed_num = (int)failure_idxs.size();
    
    std::vector<std::vector<int>> failed_map(k2 + m2, std::vector<int>(k1 + m1, 0));
    std::vector<int> fb_row_cnt(k2 + m2, 0);
    std::vector<int> fb_col_cnt(k1 + m1, 0);

    for (int i = 0; i < failed_num; i++) {
        int row = -1, col = -1;
        int failed_idx = failure_idxs[i];
        bid2rowcol(failed_idx, row, col);
        failed_map[row][col] = 1;
        fb_row_cnt[row]++;
        fb_col_cnt[col]++;
    }

    int max_bid = (k1 + m1) * (k2 + m2);
    std::vector<int> block_to_rack(max_bid, -1);
    for (size_t rk = 0; rk < partition_plan.size(); ++rk) {
        for (int bid : partition_plan[rk]) {
            if (bid >= 0 && bid < max_bid) {
                block_to_rack[bid] = rk;
            }
        }
    }

    while (failed_num > 0) {
        bool progressed = false;

        // =========================================================
        // Part One: 
        // =========================================================
        for (int i = 0; i < k2 + m2; i++) {
            if (fb_row_cnt[i] <= m1 && fb_row_cnt[i] > 0) { 
                RepairPlan plan;
                plan.local_or_column = false; // false 代表行修复
                std::vector<int> tmp_failure_idxs;
                
                for (int jj = 0; jj < k1 + m1; jj++) {
                    if (failed_map[i][jj]) {
                        tmp_failure_idxs.push_back(jj);
                    }
                }
                
              
                row_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
                        plan.parity_idxs, plan.help_blocks, partial_scheme);
                
                
                for (auto& p_idx : plan.parity_idxs) {
                    p_idx = rowcol2bid(i, p_idx); 
                }
                for (auto& vec : plan.help_blocks) {
                    for (auto& h_idx : vec) h_idx = rowcol2bid(i, h_idx);
                }

                
                reorganize_help_blocks_by_rack(plan.help_blocks);

               
                for (int c : tmp_failure_idxs) {
                    plan.failure_idxs.push_back(rowcol2bid(i, c));
                    failed_map[i][c] = 0;
                    failed_num -= 1;
                    fb_row_cnt[i] -= 1;
                    fb_col_cnt[c] -= 1;
                }
                plans.push_back(plan);
                progressed = true;
            }
        }

		if (failed_num == 0) {
            break; 
        } 

        // =========================================================
        // Part Two: 
        // =========================================================
        int best_col = -1;
        int max_e = 0;
        
        
        for (int j = 0; j < k1 + m1; j++) {
            int e = fb_col_cnt[j];
            if (e > 0 && e <= m2 && e > max_e) { 
                best_col = j;
                max_e = e; 
            }
        }

        if (best_col != -1) {
            RepairPlan plan;
            plan.local_or_column = true; 
            std::vector<int> tmp_failure_idxs;
            
            for (int r = 0; r < k2 + m2; r++) {
                if (failed_map[r][best_col]) {
                    tmp_failure_idxs.push_back(r);
                }
            }
            
            std::map<int, std::vector<int>> rack_to_rows;
            for (int r = 0; r < k2 + m2; ++r) {
                int bid = rowcol2bid(r, best_col);
                rack_to_rows[block_to_rack[bid]].push_back(r);
            }
            std::vector<std::vector<int>> dynamic_1d_plan;
            for (auto const& pair : rack_to_rows) {
                dynamic_1d_plan.push_back(pair.second);
            }
            
         
            col_code.partition_plan = dynamic_1d_plan;
            
         
            col_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
                    plan.parity_idxs, plan.help_blocks, partial_scheme);
                    
            col_code.partition_plan.clear();
            for (int r = 0; r < k2 + m2; r++) {
                col_code.partition_plan.push_back({r});
            }
            
          
            for (auto& p_idx : plan.parity_idxs) {
                p_idx = rowcol2bid(p_idx, best_col);
            }
            for (auto& vec : plan.help_blocks) {
                for (auto& h_idx : vec) h_idx = rowcol2bid(h_idx, best_col);
            }

          
            reorganize_help_blocks_by_rack(plan.help_blocks);

          
            for (int r : tmp_failure_idxs) {
                plan.failure_idxs.push_back(rowcol2bid(r, best_col));
                failed_map[r][best_col] = 0;
                failed_num -= 1;
                fb_row_cnt[r] -= 1;
                fb_col_cnt[best_col] -= 1;
            }
            plans.push_back(plan);
            progressed = true;
        }

        if (!progressed) {
            return false;
        }
    }
    return true;
}

bool ProductCode::repair_m1col(const std::vector<int>& failure_idxs,
                               std::vector<RepairPlan>& plans,
                               bool partial_scheme)
{
    int failed_num = (int)failure_idxs.size();
    std::vector<std::vector<int>> failed_map(k2 + m2, std::vector<int>(k1 + m1, 0));
    std::vector<int> fb_row_cnt(k2 + m2, 0);
    std::vector<int> fb_col_cnt(k1 + m1, 0);


    for (int i = 0; i < failed_num; i++) {
        int row = -1, col = -1;
        bid2rowcol(failure_idxs[i], row, col);
        failed_map[row][col] = 1;
        fb_row_cnt[row]++;
        fb_col_cnt[col]++;
    }


    int max_bid = (k1 + m1) * (k2 + m2);
    std::vector<int> block_to_rack(max_bid, -1);
    for (size_t rk = 0; rk < partition_plan.size(); ++rk) {
        for (int bid : partition_plan[rk]) {
            if (bid >= 0 && bid < max_bid) {
                block_to_rack[bid] = rk;
            }
        }
    }

    while (failed_num > 0) {
        bool progressed = false;

        // =========================================================
        // Step 1: 
        // =========================================================
        for (int j = 0; j < k1 + m1; j++) {
            if (fb_col_cnt[j] <= m2 && fb_col_cnt[j] > 0) {
                RepairPlan plan;
                plan.local_or_column = true; // true 代表列修复
                std::vector<int> tmp_failure_idxs;
                
                for (int r = 0; r < k2 + m2; r++) {
                    if (failed_map[r][j]) tmp_failure_idxs.push_back(r);
                }
                
               
                col_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
                        plan.parity_idxs, plan.help_blocks, partial_scheme);
                
           
                for (auto& p_idx : plan.parity_idxs) p_idx = rowcol2bid(p_idx, j);
                for (auto& vec : plan.help_blocks) {
                    for (auto& h_idx : vec) h_idx = rowcol2bid(h_idx, j);
                }

              
                reorganize_help_blocks_by_rack(plan.help_blocks);

             
                for (int r : tmp_failure_idxs) {
                    plan.failure_idxs.push_back(rowcol2bid(r, j));
                    failed_map[r][j] = 0;
                    failed_num -= 1;
                    fb_row_cnt[r] -= 1;
                    fb_col_cnt[j] -= 1;
                }
                plans.push_back(plan);
                progressed = true;
            }
        }

		if (failed_num == 0) {
            break; 
        } 

        // =========================================================
        // Step 2:
        // =========================================================
        int best_row = -1;
        int max_e = 0;
        
       
        for (int i = 0; i < k2 + m2; i++) {
            int e = fb_row_cnt[i];
            if (e > 0 && e <= m1 && e > max_e) { 
                best_row = i;
                max_e = e; 
            }
        }

        if (best_row != -1) {
            RepairPlan plan;
            plan.local_or_column = false;
            std::vector<int> tmp_failure_idxs;
            
            for (int c = 0; c < k1 + m1; c++) {
                if (failed_map[best_row][c]) tmp_failure_idxs.push_back(c);
            }
            

            std::map<int, std::vector<int>> rack_to_cols;
            for (int c = 0; c < k1 + m1; ++c) {
                int bid = rowcol2bid(best_row, c);
                rack_to_cols[block_to_rack[bid]].push_back(c);
            }
            std::vector<std::vector<int>> dynamic_1d_plan;
            for (auto const& pair : rack_to_cols) {
                dynamic_1d_plan.push_back(pair.second);
            }
            
           
            row_code.partition_plan = dynamic_1d_plan;
            
      
            row_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
                    plan.parity_idxs, plan.help_blocks, partial_scheme);
            
        
            row_code.partition_plan.clear();
            for (int c = 0; c < k1 + m1; c++) {
                row_code.partition_plan.push_back({c});
            }
            // -----------------------------------------------------

            //
            for (auto& p_idx : plan.parity_idxs) p_idx = rowcol2bid(best_row, p_idx);
            for (auto& vec : plan.help_blocks) {
                for (auto& h_idx : vec) h_idx = rowcol2bid(best_row, h_idx);
            }

            //
            reorganize_help_blocks_by_rack(plan.help_blocks);

            for (int c : tmp_failure_idxs) {
                plan.failure_idxs.push_back(rowcol2bid(best_row, c));
                failed_map[best_row][c] = 0;
                failed_num -= 1;
                fb_row_cnt[best_row] -= 1;
                fb_col_cnt[c] -= 1;
            }
            plans.push_back(plan);
            progressed = true;
        }

        // 
        if (!progressed) {
            return false;
        }
    }
    return true;
}

bool ProductCode::repair_m2row(const std::vector<int>& failure_idxs,
                               std::vector<RepairPlan>& plans,
                               bool partial_scheme)
{
    int failed_num = (int)failure_idxs.size();
    std::vector<std::vector<int>> failed_map(k2 + m2, std::vector<int>(k1 + m1, 0));
    std::vector<int> fb_row_cnt(k2 + m2, 0);
    std::vector<int> fb_col_cnt(k1 + m1, 0);

    // 
    for (int i = 0; i < failed_num; i++) {
        int row = -1, col = -1;
        bid2rowcol(failure_idxs[i], row, col);
        failed_map[row][col] = 1;
        fb_row_cnt[row]++;
        fb_col_cnt[col]++;
    }


    int max_bid = (k1 + m1) * (k2 + m2);
    std::vector<int> block_to_rack(max_bid, -1);
    for (size_t rk = 0; rk < partition_plan.size(); ++rk) {
        for (int bid : partition_plan[rk]) {
            if (bid >= 0 && bid < max_bid) {
                block_to_rack[bid] = rk;
            }
        }
    }

    while (failed_num > 0) {
        bool progressed = false;

        // =========================================================
        // Step 1: 
        // =========================================================
        for (int i = 0; i < k2 + m2; i++) {
            if (fb_row_cnt[i] <= m1 && fb_row_cnt[i] > 0) {
                RepairPlan plan;
                plan.local_or_column = false; 
                std::vector<int> tmp_failure_idxs;
                
                for (int c = 0; c < k1 + m1; c++) {
                    if (failed_map[i][c]) tmp_failure_idxs.push_back(c);
                }
                
               
                row_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
                        plan.parity_idxs, plan.help_blocks, partial_scheme);
                
                
                for (auto& p_idx : plan.parity_idxs) p_idx = rowcol2bid(i, p_idx);
                for (auto& vec : plan.help_blocks) {
                    for (auto& h_idx : vec) h_idx = rowcol2bid(i, h_idx);
                }

               
                reorganize_help_blocks_by_rack(plan.help_blocks);

                
                for (int c : tmp_failure_idxs) {
                    plan.failure_idxs.push_back(rowcol2bid(i, c));
                    failed_map[i][c] = 0;
                    failed_num -= 1;
                    fb_row_cnt[i] -= 1;
                    fb_col_cnt[c] -= 1;
                }
                plans.push_back(plan);
                progressed = true;
            }
        }

		if (failed_num == 0) {
            break; 
        } 

        // =========================================================
        // Step 2: 
        // =========================================================
        int best_col = -1;
        int max_e = 0;
        
       
        for (int j = 0; j < k1 + m1; j++) {
            int e = fb_col_cnt[j];
            if (e > 0 && e <= m2 && e > max_e) { 
                best_col = j;
                max_e = e; 
            }
        }

        if (best_col != -1) {
            RepairPlan plan;
            plan.local_or_column = true; 
            std::vector<int> tmp_failure_idxs;
            
            for (int r = 0; r < k2 + m2; r++) {
                if (failed_map[r][best_col]) tmp_failure_idxs.push_back(r);
            }
            

            std::map<int, std::vector<int>> rack_to_rows;
            for (int r = 0; r < k2 + m2; ++r) {
                int bid = rowcol2bid(r, best_col);
                rack_to_rows[block_to_rack[bid]].push_back(r);
            }
            std::vector<std::vector<int>> dynamic_1d_plan;
            for (auto const& pair : rack_to_rows) {
                dynamic_1d_plan.push_back(pair.second);
            }
            
          
            col_code.partition_plan = dynamic_1d_plan;
            
          
            col_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
                    plan.parity_idxs, plan.help_blocks, partial_scheme);
            
            col_code.partition_plan.clear();
            for (int r = 0; r < k2 + m2; r++) {
                col_code.partition_plan.push_back({r});
            }
            // -----------------------------------------------------

            // 
            for (auto& p_idx : plan.parity_idxs) p_idx = rowcol2bid(p_idx, best_col);
            for (auto& vec : plan.help_blocks) {
                for (auto& h_idx : vec) h_idx = rowcol2bid(h_idx, best_col);
            }

            // 
            reorganize_help_blocks_by_rack(plan.help_blocks);

            for (int r : tmp_failure_idxs) {
                plan.failure_idxs.push_back(rowcol2bid(r, best_col));
                failed_map[r][best_col] = 0;
                failed_num -= 1;
                fb_row_cnt[r] -= 1;
                fb_col_cnt[best_col] -= 1;
            }
            plans.push_back(plan);
            progressed = true;
        }

        // 
        if (!progressed) {
            return false;
        }
    }
    return true;
}


bool ProductCode::repair_tile(const std::vector<int>& failure_idxs,
                              std::vector<RepairPlan>& plans,
                              bool partial_scheme)
{
    int failed_num = (int)failure_idxs.size();
    std::vector<std::vector<int>> failed_map(k2 + m2, std::vector<int>(k1 + m1, 0));
    std::vector<int> fb_row_cnt(k2 + m2, 0);
    std::vector<int> fb_col_cnt(k1 + m1, 0);

    for (int i = 0; i < failed_num; i++) {
        int row = -1, col = -1;
        bid2rowcol(failure_idxs[i], row, col);
        failed_map[row][col] = 1;
        fb_row_cnt[row]++;
        fb_col_cnt[col]++;
    }


    int max_bid = (k1 + m1) * (k2 + m2);
    std::vector<int> block_to_rack(max_bid, -1);
    for (size_t rk = 0; rk < partition_plan.size(); ++rk) {
        for (int bid : partition_plan[rk]) {
            block_to_rack[bid] = rk;
        }
    }


    while (failed_num > 0) {
        double best_ratio = std::numeric_limits<double>::max();
        int best_line = -1;
        bool is_row = true;
        int best_e = 0;

		
        auto evaluate_cost = [&](bool row_dir, int line_idx, int e) -> int {
            int line_len = row_dir ? (k1 + m1) : (k2 + m2);
            int k_target = row_dir ? k1 : k2;
            
            std::map<int, int> rack_survivors;
            std::map<int, int> rack_failures; 

           
            for (int idx = 0; idx < line_len; ++idx) {
                int r = row_dir ? line_idx : idx;
                int c = row_dir ? idx : line_idx;
                int bid = rowcol2bid(r, c);
                int rack_id = block_to_rack[bid];
                
                if (failed_map[r][c] == 0) {
                    rack_survivors[rack_id]++;
                } else {
                    rack_failures[rack_id]++;
                }
            }

          
            int main_cid = -1;
            int max_fail = 0;
            for (auto const& pair : rack_failures) {
                if (pair.second > max_fail) {
                    max_fail = pair.second;
                    main_cid = pair.first;
                }
            }

           
            int fetched = 0;
            int network_cost = 0;

           
            if (rack_survivors.count(main_cid)) {
                int take = std::min(rack_survivors[main_cid], k_target - fetched);
                fetched += take;
                // network_cost += 0; 
            }

           
            std::vector<int> s_counts;
            for (auto const& pair : rack_survivors) {
                if (pair.first != main_cid) {
                    s_counts.push_back(pair.second);
                }
            }
            std::sort(s_counts.rbegin(), s_counts.rend());

            for (int s_i : s_counts) {
                if (fetched >= k_target) break;
                int take = std::min(s_i, k_target - fetched);
                // Partial Scheme 
                network_cost += partial_scheme ? std::min(take, e) : take;
                fetched += take;
            }

            if (fetched < k_target) return -1; 

          
            int write_back_cost = e - max_fail;
            network_cost += write_back_cost;

            return network_cost;
        };

      
        for (int i = 0; i < k2 + m2; i++) {
            int e = fb_row_cnt[i];
            if (e > 0 && e <= m1) {
                int cost = evaluate_cost(true, i, e);
                if (cost != -1) {
                    double ratio = (double)cost / e; 
                    if (ratio < best_ratio || (abs(ratio - best_ratio) < 1e-6 && e > best_e)) {
                        best_ratio = ratio; best_line = i; is_row = true; best_e = e;
                    }
                }
            }
        }

       
        for (int j = 0; j < k1 + m1; j++) {
            int e = fb_col_cnt[j];
            if (e > 0 && e <= m2) {
                int cost = evaluate_cost(false, j, e);
                if (cost != -1) {
                    double ratio = (double)cost / e; 
                    if (ratio < best_ratio || (abs(ratio - best_ratio) < 1e-6 && e > best_e)) {
                        best_ratio = ratio; best_line = j; is_row = false; best_e = e;
                    }
                }
            }
        }

        
        if (best_line == -1) {
            return false; 
        }


        RepairPlan plan;
        plan.local_or_column = !is_row;
        std::vector<int> tmp_failure_idxs;

        if (is_row) {
            for (int c = 0; c < k1 + m1; c++) {
                if (failed_map[best_line][c]) tmp_failure_idxs.push_back(c);
            }
            
            
            std::map<int, std::vector<int>> rack_to_cols;
            for (int c = 0; c < k1 + m1; ++c) {
                int bid = rowcol2bid(best_line, c);
                rack_to_cols[block_to_rack[bid]].push_back(c);
            }
            std::vector<std::vector<int>> dynamic_1d_plan;
            for (auto const& pair : rack_to_cols) dynamic_1d_plan.push_back(pair.second);
            
            
            row_code.partition_plan = dynamic_1d_plan;
            
            
            row_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
                    plan.parity_idxs, plan.help_blocks, partial_scheme);
                    
            
            row_code.partition_plan.clear();
            for (int c = 0; c < k1 + m1; c++) row_code.partition_plan.push_back({c});

           
            for (auto& p_idx : plan.parity_idxs) p_idx = rowcol2bid(best_line, p_idx);
            for (auto& vec : plan.help_blocks) {
                for (auto& h_idx : vec) h_idx = rowcol2bid(best_line, h_idx);
            }

           
            for (int c : tmp_failure_idxs) {
                plan.failure_idxs.push_back(rowcol2bid(best_line, c));
                failed_map[best_line][c] = 0;
                failed_num--; fb_row_cnt[best_line]--; fb_col_cnt[c]--;
            }

        } else {
            
            for (int r = 0; r < k2 + m2; r++) {
                if (failed_map[r][best_line]) tmp_failure_idxs.push_back(r);
            }
            
          
            std::map<int, std::vector<int>> rack_to_rows;
            for (int r = 0; r < k2 + m2; ++r) {
                int bid = rowcol2bid(r, best_line);
                rack_to_rows[block_to_rack[bid]].push_back(r);
            }
            std::vector<std::vector<int>> dynamic_1d_plan;
            for (auto const& pair : rack_to_rows) dynamic_1d_plan.push_back(pair.second);
            
            col_code.partition_plan = dynamic_1d_plan;
            col_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
                    plan.parity_idxs, plan.help_blocks, partial_scheme);
                    
            col_code.partition_plan.clear();
            for (int r = 0; r < k2 + m2; r++) col_code.partition_plan.push_back({r});

          
            for (auto& p_idx : plan.parity_idxs) p_idx = rowcol2bid(p_idx, best_line);
            for (auto& vec : plan.help_blocks) {
                for (auto& h_idx : vec) h_idx = rowcol2bid(h_idx, best_line);
            }

            
            for (int r : tmp_failure_idxs) {
                plan.failure_idxs.push_back(rowcol2bid(r, best_line));
                failed_map[r][best_line] = 0;
                failed_num--; fb_row_cnt[r]--; fb_col_cnt[best_line]--;
            }
        }

        
        reorganize_help_blocks_by_rack(plan.help_blocks);
        plans.push_back(plan);
    }
    return true;
}

bool ProductCode::repair_diagonal(const std::vector<int>& failure_idxs,
                                  std::vector<RepairPlan>& plans,
                                  bool partial_scheme)
{
    int failed_num = (int)failure_idxs.size();
    std::vector<std::vector<int>> failed_map(k2 + m2, std::vector<int>(k1 + m1, 0));
    std::vector<int> fb_row_cnt(k2 + m2, 0);
    std::vector<int> fb_col_cnt(k1 + m1, 0);

    for (int i = 0; i < failed_num; i++) {
        int row = -1, col = -1;
        bid2rowcol(failure_idxs[i], row, col);
        failed_map[row][col] = 1;
        fb_row_cnt[row]++;
        fb_col_cnt[col]++;
    }


    int max_bid = (k1 + m1) * (k2 + m2);
    std::vector<int> block_to_rack(max_bid, -1);
    for (size_t rk = 0; rk < partition_plan.size(); ++rk) {
        for (int bid : partition_plan[rk]) {
            if (bid >= 0 && bid < max_bid) {
                block_to_rack[bid] = rk;
            }
        }
    }


    while (failed_num > 0) {
        double best_ratio = std::numeric_limits<double>::max();
        int best_line = -1;
        bool is_row = true;
        int best_e = 0;

		/*
        // 
        auto evaluate_cost = [&](bool row_dir, int line_idx, int e) -> int {
            int line_len = row_dir ? (k1 + m1) : (k2 + m2);
            int k_target = row_dir ? k1 : k2;
            
            // 
            std::map<int, int> rack_survivors;
            for (int idx = 0; idx < line_len; ++idx) {
                int r = row_dir ? line_idx : idx;
                int c = row_dir ? idx : line_idx;
                if (failed_map[r][c] == 0) { // 
                    int bid = rowcol2bid(r, c);
                    rack_survivors[block_to_rack[bid]]++;
                }
            }

            
            std::vector<int> s_counts;
            for (auto const& pair : rack_survivors) s_counts.push_back(pair.second);
            std::sort(s_counts.rbegin(), s_counts.rend()); // 

            int fetched = 0;
            int network_cost = 0;
            for (int s_i : s_counts) {
                if (fetched >= k_target) break;
                int take = std::min(s_i, k_target - fetched);
                // Partial Scheme 
                network_cost += partial_scheme ? std::min(take, e) : take;
                fetched += take;
            }

            if (fetched < k_target) return -1; 
            return network_cost;
        };
		*/

        auto evaluate_cost = [&](bool row_dir, int line_idx, int e) -> int {
            int line_len = row_dir ? (k1 + m1) : (k2 + m2);
            int k_target = row_dir ? k1 : k2;
            
            std::map<int, int> rack_survivors;
            std::map<int, int> rack_failures; 

           
            for (int idx = 0; idx < line_len; ++idx) {
                int r = row_dir ? line_idx : idx;
                int c = row_dir ? idx : line_idx;
                int bid = rowcol2bid(r, c);
                int rack_id = block_to_rack[bid];
                
                if (failed_map[r][c] == 0) {
                    rack_survivors[rack_id]++;
                } else {
                    rack_failures[rack_id]++;
                }
            }

            
            int main_cid = -1;
            int max_fail = 0;
            for (auto const& pair : rack_failures) {
                if (pair.second > max_fail) {
                    max_fail = pair.second;
                    main_cid = pair.first;
                }
            }

            
            int fetched = 0;
            int network_cost = 0;

          
            if (rack_survivors.count(main_cid)) {
                int take = std::min(rack_survivors[main_cid], k_target - fetched);
                fetched += take;
                // network_cost += 0; 
            }

           
            std::vector<int> s_counts;
            for (auto const& pair : rack_survivors) {
                if (pair.first != main_cid) {
                    s_counts.push_back(pair.second);
                }
            }
            std::sort(s_counts.rbegin(), s_counts.rend());

            for (int s_i : s_counts) {
                if (fetched >= k_target) break;
                int take = std::min(s_i, k_target - fetched);
                // Partial Scheme 
                network_cost += partial_scheme ? std::min(take, e) : take;
                fetched += take;
            }

            if (fetched < k_target) return -1; 

           
            int write_back_cost = e - max_fail;
            network_cost += write_back_cost;

            return network_cost;
        };

       
        for (int i = 0; i < k2 + m2; i++) {
            int e = fb_row_cnt[i];
            if (e > 0 && e <= m1) {
                int cost = evaluate_cost(true, i, e);
                if (cost != -1) {
                    double ratio = (double)cost / e; // 边际成本
                    if (ratio < best_ratio || (abs(ratio - best_ratio) < 1e-6 && e > best_e)) {
                        best_ratio = ratio; best_line = i; is_row = true; best_e = e;
                    }
                }
            }
        }

        for (int j = 0; j < k1 + m1; j++) {
            int e = fb_col_cnt[j];
            if (e > 0 && e <= m2) {
                int cost = evaluate_cost(false, j, e);
                if (cost != -1) {
                    double ratio = (double)cost / e; // 边际成本
                    if (ratio < best_ratio || (abs(ratio - best_ratio) < 1e-6 && e > best_e)) {
                        best_ratio = ratio; best_line = j; is_row = false; best_e = e;
                    }
                }
            }
        }

       
        if (best_line == -1) {
            return false; 
        }


        RepairPlan plan;
        plan.local_or_column = !is_row;
        std::vector<int> tmp_failure_idxs;

        if (is_row) {
            for (int c = 0; c < k1 + m1; c++) {
                if (failed_map[best_line][c]) tmp_failure_idxs.push_back(c);
            }
            
            // 
            std::map<int, std::vector<int>> rack_to_cols;
            for (int c = 0; c < k1 + m1; ++c) {
                int bid = rowcol2bid(best_line, c);
                rack_to_cols[block_to_rack[bid]].push_back(c);
            }
            std::vector<std::vector<int>> dynamic_1d_plan;
            for (auto const& pair : rack_to_cols) dynamic_1d_plan.push_back(pair.second);
            
            // 
            row_code.partition_plan = dynamic_1d_plan;
            row_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
                    plan.parity_idxs, plan.help_blocks, partial_scheme);
                    
            //
            row_code.partition_plan.clear();
            for (int c = 0; c < k1 + m1; c++) row_code.partition_plan.push_back({c});

            // 
            for (auto& p_idx : plan.parity_idxs) p_idx = rowcol2bid(best_line, p_idx);
            for (auto& vec : plan.help_blocks) {
                for (auto& h_idx : vec) h_idx = rowcol2bid(best_line, h_idx);
            }

            // 
            for (int c : tmp_failure_idxs) {
                plan.failure_idxs.push_back(rowcol2bid(best_line, c));
                failed_map[best_line][c] = 0;
                failed_num--; fb_row_cnt[best_line]--; fb_col_cnt[c]--;
            }

        } else {
            // 
            for (int r = 0; r < k2 + m2; r++) {
                if (failed_map[r][best_line]) tmp_failure_idxs.push_back(r);
            }
            
            //
            std::map<int, std::vector<int>> rack_to_rows;
            for (int r = 0; r < k2 + m2; ++r) {
                int bid = rowcol2bid(r, best_line);
                rack_to_rows[block_to_rack[bid]].push_back(r);
            }
            std::vector<std::vector<int>> dynamic_1d_plan;
            for (auto const& pair : rack_to_rows) dynamic_1d_plan.push_back(pair.second);
            
            col_code.partition_plan = dynamic_1d_plan;
            col_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
                    plan.parity_idxs, plan.help_blocks, partial_scheme);
                    
            col_code.partition_plan.clear();
            for (int r = 0; r < k2 + m2; r++) col_code.partition_plan.push_back({r});

            for (auto& p_idx : plan.parity_idxs) p_idx = rowcol2bid(p_idx, best_line);
            for (auto& vec : plan.help_blocks) {
                for (auto& h_idx : vec) h_idx = rowcol2bid(h_idx, best_line);
            }

            for (int r : tmp_failure_idxs) {
                plan.failure_idxs.push_back(rowcol2bid(r, best_line));
                failed_map[r][best_line] = 0;
                failed_num--; fb_row_cnt[r]--; fb_col_cnt[best_line]--;
            }
        }

       
        reorganize_help_blocks_by_rack(plan.help_blocks);
        plans.push_back(plan);
    }
    return true;
}

void ProductCode::reorganize_help_blocks_by_rack(std::vector<std::vector<int>>& help_blocks) {
    
    int total_blocks = (k1 + m1) * (k2 + m2);
    int rack_count = (int)partition_plan.size();
    if (rack_count == 0) {
        return;
    }

    
    std::vector<int> block_to_rack(total_blocks, -1);
    for (int rk = 0; rk < rack_count; ++rk) {
        for (int bid : partition_plan[rk]) {
            if (bid >= 0 && bid < total_blocks) {
                block_to_rack[bid] = rk;
            }
        }
    }

    
    std::vector<std::vector<int>> reorganized_racks(rack_count);

   
    for (const auto& sub_vec : help_blocks) {
        for (int bid : sub_vec) {
            if (bid >= 0 && bid < total_blocks) {
                int rack_id = block_to_rack[bid];
                if (rack_id != -1) {
                    reorganized_racks[rack_id].push_back(bid);
                }
            }
        }
    }

    
    help_blocks.clear();
    for (int rk = 0; rk < rack_count; ++rk) {
        if (!reorganized_racks[rk].empty()) {

            std::sort(reorganized_racks[rk].begin(), reorganized_racks[rk].end());
            reorganized_racks[rk].erase(
                std::unique(reorganized_racks[rk].begin(), reorganized_racks[rk].end()), 
                reorganized_racks[rk].end()
            );

           
            help_blocks.push_back(reorganized_racks[rk]);
        }
    }
}

/*
bool ProductCode::generate_repair_plan(const std::vector<int>& failure_idxs,
																			 std::vector<RepairPlan>& plans,
																			 bool partial_scheme,
																			 bool repair_priority,
																			 bool repair_method)
{
	int failed_num = (int)failure_idxs.size();
	std::vector<std::vector<int>> failed_map(k2 + m2, std::vector<int>(k1 + m1, 0));
	std::vector<int> fb_row_cnt(k2 + m2, 0);
	std::vector<int> fb_col_cnt(k1 + m1, 0);

	for (int i = 0; i < failed_num; i++) {
		int row = -1, col = -1;
		int failed_idx = failure_idxs[i];
		bid2rowcol(failed_idx, row, col);
		failed_map[row][col] = 1;
		fb_row_cnt[row]++;
		fb_col_cnt[col]++;
	}

	while (failed_num > 0) {
		// part one
		for (int i = 0; i < k1 + m1; i++) {
			if (fb_col_cnt[i] <= m2 && fb_col_cnt[i] > 0) {	// repair on column
				RepairPlan plan;
				plan.local_or_column = true;
				int cnt = 0;
				std::vector<int> help_block;
				for (int jj = k2; jj < k2 + m2; jj++) {
					if (failed_map[jj][i]) {
						plan.parity_idxs.push_back(rowcol2bid(jj, i));
					}
				}
				for (int jj = 0; jj < k2 + m2; jj++) {
					if (cnt == k2) {
						break;
					}
					if (!failed_map[jj][i]) {
						help_block.push_back(rowcol2bid(jj, i));
						cnt++;
						if (jj >= k2) {
							plan.parity_idxs.push_back(rowcol2bid(jj, i));
						}
					}
				}
				if (placement_rule == FLAT) {
					for (auto block : help_block) {
						plan.help_blocks.push_back({block});
					}
				} else {
					plan.help_blocks.push_back(help_block);
				}

				// update failed_map
				for (int jj = 0; jj < k2 + m2; jj++) {
					if (failed_map[jj][i]) {
						plan.failure_idxs.push_back(rowcol2bid(jj, i));
						failed_map[jj][i] = 0;
						failed_num -= 1;
						fb_row_cnt[jj] -= 1;
						fb_col_cnt[i] -= 1;
					}
				}
				plans.push_back(plan);
			}
		}
		if (failed_num == 0) {
			break;
		}
		// part two
		int max_row = -1;
		for (int i = 0; i < k2 + m2; i++) {
			if (fb_row_cnt[i] <= m1 && fb_row_cnt[i] > 0) { // repair on row
				max_row = i;
				RepairPlan plan;
				plan.local_or_column = false;
				std::vector<int> tmp_failure_idxs;
				for (int jj = 0; jj < k1 + m1; jj++) {
					if (failed_map[i][jj]) {
						tmp_failure_idxs.push_back(jj);
					}
				}
				row_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
						plan.parity_idxs, plan.help_blocks, partial_scheme);
				int par_num = (int)plan.help_blocks.size();
				for (int ii = 0; ii < par_num; ii++) {
					for (auto it = plan.help_blocks[ii].begin(); it != plan.help_blocks[ii].end(); it++) {
						*it = rowcol2bid(i, *it);
					}
				}
				for (auto it = plan.parity_idxs.begin(); it != plan.parity_idxs.end(); it++) {
					*it = rowcol2bid(i, *it);
				}

				// update failed_map
				for (int jj = 0; jj < k1 + m1; jj++) {
					if (failed_map[i][jj]) {
						plan.failure_idxs.push_back(rowcol2bid(i, jj));
						failed_map[i][jj] = 0;
						failed_num -= 1;
						fb_row_cnt[i] -= 1;
						fb_col_cnt[jj] -= 1;
					}
				}
				plans.push_back(plan);
				break;
			}
		}
		if (max_row == -1) {
			std::cout << "Undecodable!!" << std::endl;
			return false;
		}
	}
	return true;
}
*/
void HVPC::init_coding_parameters(CodingParameters cp)
{
	k1 = cp.k1;
	m1 = cp.m1;
	k2 = cp.k2;
	m2 = cp.m2;
	k = k1 * k2;
	m = k1 * m2 + k2 * m1;
	row_code.k = k1;
	row_code.m = m1;
	col_code.k = k2;
	col_code.m = m2;
	local_or_column = cp.local_or_column;
}

/*
	data_ptrs: ordered by row
	D(0) ... D(k1*k2-1)
	coding_ptrs: ordered by row
	R(0) ... R(k2*m1-1) C(0) ...  C(m2*k1-1)
*/
void HVPC::encode(char **data_ptrs, char **coding_ptrs, int block_size)
{
	// encode row parities
	for (int i = 0; i < k2; i++) {
		std::vector<char *> t_coding(m1);
		char **coding = (char **)t_coding.data();
		for (int j = 0; j < m1; j++) {
			coding[j] = coding_ptrs[i * m1 + j];
		}
		row_code.encode(&data_ptrs[i * k1], coding, block_size);
	}
	// encode column parities
	for (int i = 0; i < k1; i++) {
		std::vector<char *> t_data(k2);
		char **data = (char **)t_data.data();
		for (int j = 0; j < k2; j++) {
			data[j] = data_ptrs[j * k1 + i];
		}
		std::vector<char *> t_coding(m2);
		char **coding = (char **)t_coding.data();
		for (int j = 0; j < m2; j++) {
			coding[j] = coding_ptrs[k2 * m1 + j * k1 + i];
		}
		col_code.encode(data, coding, block_size);
	}
}

// the index order is the same as shown above
void HVPC::decode(char **data_ptrs, char **coding_ptrs, int block_size,
								 int *erasures, int failed_num)
{
	std::vector<std::vector<int>> failed_map(k2 + m2, std::vector<int>(k1 + m1, 0));
	std::vector<std::vector<char *>> blocks_map(k2 + m2, std::vector<char *>(k1 + m1, nullptr));
	std::vector<int> fb_row_cnt(k2 + m2, 0);
	std::vector<int> fb_col_cnt(k1 + m1, 0);

	int data_idx = 0;
	int parity_idx = 0;
	for (int i = 0; i < k2; i++) {
		for (int j = 0; j < k1 + m1; j++) {
			if (j < k1) {
				blocks_map[i][j] = data_ptrs[data_idx++];
			} else {
				blocks_map[i][j] = coding_ptrs[parity_idx++];
			}
		}
	}
	for (int i = k2; i < k2 + m2; i++) {
		for (int j = 0; j < k1; j++) {
			blocks_map[i][j] = coding_ptrs[parity_idx++];
		}
	}

	for (int i = 0; i < failed_num; i++) {
		int row = -1, col = -1;
		int failed_idx = erasures[i];
		bid2rowcol(failed_idx, row, col);
		failed_map[row][col] = 1;
		fb_row_cnt[row]++;
		fb_col_cnt[col]++;
	}

	while (failed_num > 0) {
		// part one
		for (int i = 0; i < k1; i++) {
			if (fb_col_cnt[i] <= m2 && fb_col_cnt[i] > 0) {	// repair on column
				std::vector<char *> data(k2, nullptr);
				std::vector<char *> coding(m2, nullptr);
				int *erasure = new int[fb_col_cnt[i] + 1];
				erasure[fb_col_cnt[i]] = -1;
				int cnt = 0;
				for (int jj = 0; jj < k2; jj++) {
					if (failed_map[jj][i]) {
						erasure[cnt++] = jj;
					}
					data[jj] = blocks_map[jj][i];
				}
				for (int jj = 0; jj < m2; jj++) {
					if (failed_map[jj + k2][i]) {
						erasure[cnt++] = jj + k2;
					}
					coding[jj] = blocks_map[jj + k2][i];
				}
				col_code.decode(data.data(), coding.data(), block_size, erasure, fb_col_cnt[i]);
				// update failed_map
				for (int jj = 0; jj < k2 + m2; jj++) {
					if (failed_map[jj][i]) {
						failed_map[jj][i] = 0;
						failed_num -= 1;
						fb_row_cnt[jj] -= 1;
						fb_col_cnt[i] -= 1;
					}
				}
				delete erasure;
			}
		}
		if (failed_num == 0) {
			break;
		}
		// part two
		int max_row = -1;
		for (int i = 0; i < k2; i++) {
			if (fb_row_cnt[i] <= m1 && fb_row_cnt[i] > 0) { // repair on row
				max_row = i;
				std::vector<char *> data(k1, nullptr);
				std::vector<char *> coding(m1, nullptr);
				int *erasure = new int[fb_row_cnt[i] + 1];
				erasure[fb_row_cnt[i]] = -1;
				int cnt = 0;
				for (int jj = 0; jj < k1; jj++) {
					if (failed_map[i][jj]) {
						erasure[cnt++] = jj;
					}
					data[jj] = blocks_map[i][jj];
				}
				for (int jj = 0; jj < m1; jj++) {
					if (failed_map[i][jj + k1]) {
						erasure[cnt++] = jj + k1;
					}
					coding[jj] = blocks_map[i][jj + k1];
				}
				row_code.decode(data.data(), coding.data(), block_size, erasure, fb_row_cnt[i]);
				// update failed_map
				for (int jj = 0; jj < k1 + m1; jj++) {
					if (failed_map[i][jj]) {
						failed_map[i][jj] = 0;
						failed_num -= 1;
						fb_row_cnt[i] -= 1;
						fb_col_cnt[jj] -= 1;
					}
				}
				delete erasure;
			}
		}
		if (max_row == -1) {
			std::cout << "Undecodable!!" << std::endl;
			return;
		}
	}
}

// the index order is the same as shown above
bool HVPC::check_if_decodable(const std::vector<int>& failure_idxs)
{
	int failed_num = (int)failure_idxs.size();
	std::vector<std::vector<int>> failed_map(k2 + m2, std::vector<int>(k1 + m1, 0));
	std::vector<int> fb_row_cnt(k2 + m2, 0);
	std::vector<int> fb_col_cnt(k1 + m1, 0);

	for (int i = 0; i < failed_num; i++)
	{
		int row = -1, col = -1;
		int failed_idx = failure_idxs[i];
		bid2rowcol(failed_idx, row, col);
		failed_map[row][col] = 1;
		fb_row_cnt[row]++;
		fb_col_cnt[col]++;
	}

	while (failed_num > 0)
	{
		// part one
		for (int i = 0; i < k1; i++) {
			if (fb_col_cnt[i] <= m2 && fb_col_cnt[i] > 0) { // repair on column
				// update failed_map
				for (int jj = 0; jj < k2 + m2; jj++) {
					if (failed_map[jj][i]) {
						failed_map[jj][i] = 0;
						failed_num -= 1;
						fb_row_cnt[jj] -= 1;
						fb_col_cnt[i] -= 1;
					}
				}
			}
		}
		if (failed_num == 0) {
			break;
		}
		// part two
		int max_row = -1;
		for (int i = 0; i < k2; i++) {
			if (fb_row_cnt[i] <= m1 && fb_row_cnt[i] > 0) {// repair on row
				max_row = i;
				// update failed_map
				for (int jj = 0; jj < k1 + m1; jj++) {
					if (failed_map[i][jj]) {
						failed_map[i][jj] = 0;
						failed_num -= 1;
						fb_row_cnt[i] -= 1;
						fb_col_cnt[jj] -= 1;
					}
				}
			}
		}
		if (max_row == -1) {
			return false;
		}
	}
	return true;
}

void HVPC::partition_random()
{
	row_code.partition_plan.clear();
	int n = k1 + m1;
	std::vector<int> columns;
  for (int i = 0; i < n; i++) {
    columns.push_back(i);
  }

	int cnt = 0;
  int cnt_left = n;
  while (cnt < n) {
		// at most every m1 columns of blocks in a partition
    int random_columns_num = random_range(1, m1);  
    int columns_num = std::min(random_columns_num, n - cnt);
    std::vector<int> partition;
		std::vector<int> row_partition;
    for (int i = 0; i < columns_num; i++, cnt++) {
      int ran_idx = random_index(n - cnt);
      int col = columns[ran_idx];
			if (col < k1) {
				for (int row = 0; row < k2 + m2; row++) {
					int block_idx = rowcol2bid(row, col);
					partition.push_back(block_idx);
				}
			} else {
				for (int row = 0; row < k2; row++) {
					int block_idx = rowcol2bid(row, col);
					partition.push_back(block_idx);
				}
			}
			row_partition.push_back(col);
      auto it = std::find(columns.begin(), columns.end(), col);
      columns.erase(it);
    }
    partition_plan.push_back(partition);
		row_code.partition_plan.push_back(row_partition);
  }
}

void HVPC::partition_optimal()
{
	row_code.partition_plan.clear();
	int n = k1 + m1;
	int cnt = 0;
	while (cnt < n) {
		// every m columns of blocks in a partition
		int columns_num = std::min(m1, n - cnt);
		std::vector<int> partition;
		std::vector<int> row_partition;
    for(int i = 0; i < columns_num; i++, cnt++) {
			if (cnt < k1) {
				for (int row = 0; row < k2 + m2; row++) {
					int block_idx = rowcol2bid(row, cnt);
					partition.push_back(block_idx);
				}
			} else {
				for (int row = 0; row < k2; row++) {
					int block_idx = rowcol2bid(row, cnt);
					partition.push_back(block_idx);
				}
			}
			row_partition.push_back(cnt);
		}
		partition_plan.push_back(partition);
		row_code.partition_plan.push_back(row_partition);
	}
}

std::string HVPC::self_information()
{
	return "HVPC(" + std::to_string(k1) + "," + std::to_string(m1) + "," + \
				 std::to_string(k2) + "," + std::to_string(m2) + ")";
}

std::string HVPC::type()
{
	return "HVPC";
}

bool HVPC::generate_repair_plan(const std::vector<int>& failure_idxs,
																std::vector<RepairPlan>& plans,
																bool partial_scheme,
																bool repair_priority,
																bool repair_method)
{
	int failed_num = (int)failure_idxs.size();
	std::vector<std::vector<int>> failed_map(k2 + m2, std::vector<int>(k1 + m1, 0));
	std::vector<int> fb_row_cnt(k2 + m2, 0);
	std::vector<int> fb_col_cnt(k1 + m1, 0);

	for (int i = 0; i < failed_num; i++) {
		int row = -1, col = -1;
		int failed_idx = failure_idxs[i];
		bid2rowcol(failed_idx, row, col);
		failed_map[row][col] = 1;
		fb_row_cnt[row]++;
		fb_col_cnt[col]++;
	}

	while (failed_num > 0) {
		// part one
		for (int i = 0; i < k1; i++) {
			if (fb_col_cnt[i] <= m2 && fb_col_cnt[i] > 0) {	// repair on column
				RepairPlan plan;
				plan.local_or_column = true;
				int cnt = 0;
				std::vector<int> help_block;
				for (int jj = k2; jj < k2 + m2; jj++) {
					if (failed_map[jj][i]) {
						plan.parity_idxs.push_back(rowcol2bid(jj, i));
					}
				}
				for (int jj = 0; jj < k2 + m2; jj++) {
					if (cnt == k2) {
						break;
					}

					if (!failed_map[jj][i]) {
						help_block.push_back(rowcol2bid(jj, i));
						cnt++;
						if (jj >= k2) {
							plan.parity_idxs.push_back(rowcol2bid(jj, i));
						}
					}
				}
				if (placement_rule == FLAT) {
					for (auto block : help_block) {
						plan.help_blocks.push_back({block});
					}
				} else {
					plan.help_blocks.push_back(help_block);
				}

				// update failed_map
				for (int jj = 0; jj < k2 + m2; jj++) {
					if (failed_map[jj][i]) {
						plan.failure_idxs.push_back(rowcol2bid(jj, i));
						failed_map[jj][i] = 0;
						failed_num -= 1;
						fb_row_cnt[jj] -= 1;
						fb_col_cnt[i] -= 1;
					}
				}
				plans.push_back(plan);
			}
		}
		if (failed_num == 0) {
			break;
		}
		// part two
		int max_row = -1;
		for (int i = 0; i < k2; i++) {
			if (fb_row_cnt[i] <= m1 && fb_row_cnt[i] > 0) { // repair on row
				max_row = i;
				RepairPlan plan;
				plan.local_or_column = false;
				std::vector<int> tmp_failure_idxs;
				for (int jj = 0; jj < k1 + m1; jj++) {
					if (failed_map[i][jj]) {
						tmp_failure_idxs.push_back(jj);
					}
				}
				row_code.help_blocks_for_multi_blocks_repair(tmp_failure_idxs,
						plan.parity_idxs, plan.help_blocks, partial_scheme);
				int par_num = (int)plan.help_blocks.size();
				for (int ii = 0; ii < par_num; ii++) {
					for (auto it = plan.help_blocks[ii].begin(); it != plan.help_blocks[ii].end(); it++) {
						*it = rowcol2bid(i, *it);
					}
				}
				for (auto it = plan.parity_idxs.begin(); it != plan.parity_idxs.end(); it++) {
					*it = rowcol2bid(i, *it);
				}

				// update failed_map
				for (int jj = 0; jj < k1 + m1; jj++) {
					if (failed_map[i][jj]) {
						plan.failure_idxs.push_back(rowcol2bid(i, jj));
						failed_map[i][jj] = 0;
						failed_num -= 1;
						fb_row_cnt[i] -= 1;
						fb_col_cnt[jj] -= 1;
					}
				}
				plans.push_back(plan);
				break;
			}
		}
		if (max_row == -1) {
			std::cout << "Undecodable!!" << std::endl;
			return false;
		}
	}
	return true;
}
