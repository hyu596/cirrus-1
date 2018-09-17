#include <Tasks.h>

#include "Serializers.h"
#include "InputReader.h"
#include "S3.h"
#include "Utils.h"
#include "config.h"
#include <random>
#include <set>
#include <array>

namespace cirrus {

#define READ_INPUT_THREADS (10)

std::array<int, 1000000> lookup_map;
std::array<int, 1000000> lindex_map;

int idx = 0;

LDADataset LoadingLDATaskS3::read_dataset(const Configuration& config) {
  InputReader input;
  return input.read_lda_input(config.get_doc_path(), config.get_vocab_path(),
                              ",", config);
}

LDAStatistics LoadingLDATaskS3::count_dataset(
    const std::vector<std::vector<std::pair<int, int>>>& docs,
    std::vector<int>& nvt,
    std::vector<int>& nt,
    int K,
    std::vector<int>& global_vocab) {
  std::vector<int> t, d, w;
  std::vector<std::vector<int>> ndt;
  std::vector<int> local_vocab;

  lindex_map.fill(-1);

  for (const auto& doc : docs) {
    std::vector<int> ndt_row(K);

    for (const auto& feat : doc) {
      int gindex = feat.first, count = feat.second;
      if (lindex_map[gindex] == -1) {
        // local_vocab.insert(local_vocab.begin(), gindex);
        local_vocab.push_back(gindex);
        lindex_map[gindex] = 1;
      }
      if (lookup_map[gindex] == -1){
        // global_vocab.insert(global_vocab.end(), gindex);
        global_vocab.push_back(gindex);
        lookup_map[gindex] = idx;
        idx++;
      }
      for (int i = 0; i < count; ++i) {
        int top = rand() % K;

        t.push_back(top);
        d.push_back(ndt.size());
        // w.push_back(vocab_map[gindex]);
        w.push_back(gindex);
        ndt_row[top] += 1;

        // nvt[gindex * K + top] += 1;
        nvt[lookup_map.at(gindex) * K + top] += 1;
        nt[top] += 1;
      }
    }
    ndt.push_back(ndt_row);
  }

  // for (auto& i: global_vocab) {
  //   std::cout << i << " ";
  // }
  // std::cout << std::endl;
  //
  // for (auto& i: global_vocab) {
  //   std::cout << lookup_map[i]<< " ";
  // }
  // std::cout << std::endl;

  // std::vector<int> local_vocab_vec(local_vocab.begin(), local_vocab.end());

  return LDAStatistics(K, ndt, local_vocab, t, d, w);
}

void LoadingLDATaskS3::run(const Configuration& config) {
  std::cout << "[LOADER] "
            << "Read lda input..." << std::endl;

  lookup_map.fill(-1);

  uint64_t s3_obj_num_samples = config.get_s3_size();
  s3_initialize_aws();
  std::shared_ptr<S3Client> s3_client = std::make_shared<S3Client>();

  int K = config.get_k();

  LDADataset dataset = read_dataset(config);
  dataset.check();
  dataset.set_sample_size(s3_obj_num_samples);

  uint64_t num_s3_objs = dataset.num_docs() / s3_obj_num_samples;
  if (num_s3_objs * s3_obj_num_samples != dataset.num_docs()) {
    num_s3_objs += 1;
  }
  std::cout << "[LOADER-SPARSE] "
            << "Adding " << dataset.num_docs()
            << " #s3 objs: " << num_s3_objs + 1
            << " bucket: " << config.get_s3_bucket() << std::endl;

  std::vector<int> nvt, nt;
  nvt.resize(dataset.num_vocabs() * K);
  nt.resize(K);

  std::vector<int> global_vocab;
  uint64_t to_send_size;

  // Storing local variables (LDAStatistics)
  for (unsigned int i = 1; i < num_s3_objs + 1; ++i) {
    std::cout << "[LOADER] Building s3 batch #" << i << std::endl;

    // Only get corpus of size s3_obj_num_samples
    std::vector<std::vector<std::pair<int, int>>> partial_docs;
    dataset.get_some_docs(partial_docs);
    LDAStatistics to_save =
        count_dataset(partial_docs, nvt, nt, K, global_vocab);

    char* msg = to_save.serialize(to_send_size);

    std::cout << "Putting object(LDAStatistics) in S3 with size: "
              << to_send_size << std::endl;
    std::string obj_id =
        std::to_string(hash_f(std::to_string(SAMPLE_BASE + i).c_str())) +
        "-LDA";
    s3_client->s3_put_object(
        obj_id, config.get_s3_bucket(),
        std::string(msg, to_send_size));
    // delete[] msg;
  }

  // check_loading(config, s3_client);
  //
  // Storing global variables
  // std::vector<int> global_vocab_vec(global_vocab.begin(), global_vocab.end());

  std::shared_ptr<LDAUpdates> initial_global_var;
  initial_global_var.reset(new LDAUpdates());
  initial_global_var->slice = global_vocab;
  initial_global_var->change_nvt_ptr.reset(new std::vector<int>(nvt));
  initial_global_var->change_nt_ptr.reset(new std::vector<int>(nt));

  // LDAUpdates initial_global_var(nvt, nt, global_vocab_vec);
  std::cout << "Putting object(initial global var) in S3 with size: "
            << initial_global_var->getSerializedSize() << std::endl;
  std::string obj_id =
      std::to_string(hash_f(std::to_string(SAMPLE_BASE).c_str())) + "-LDA";

  uint32_t len;
  std::shared_ptr<char> s3_obj = initial_global_var->serialize(&len);

  s3_client->s3_put_object(obj_id, config.get_s3_bucket(),
                           std::string(s3_obj.get(), len));

  s3_shutdown_aws();

  std::cout << "LOADER-LDA terminated successfully" << std::endl;
}

}  // namespace cirrus
