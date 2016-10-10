
#include <boost/make_shared.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "caffe/multi_node/param_helper.hpp"
#include "caffe/multi_node/ps_thread.hpp"

namespace caffe {

template <typename Dtype>
int PSThread<Dtype>::UpdateParam(shared_ptr<Msg> m) {
  map<int, int>::iterator map_iter = client_idx_map_.find(m->src());

  if (map_iter == client_idx_map_.end()) {
    LOG(WARNING) << "update from unregistered node id: " << m->src();
    return 0;
  }

  CHECK_EQ(m->num_blobs(), 1) << "expect 1 blob";
  const string& layer_name = m->blob_info(0).blob_name();
  int layer_id = this->GetLayerId(layer_name);

  int client_idx = map_iter->second;
  client_msgs_[layer_id][client_idx] = m;

  if ((msg_clocks_[layer_id][client_idx] + 1) != m->clock()) {
    LOG(WARNING) << "unmatched clock between ps and client " << m->src();
  }

  MLOG(INFO) << "Recv gradients from: " << m->src()
             << ", layer: " << layer_name
             << ", clock: " << m->clock();

  // update clock
  msg_clocks_[layer_id][client_idx] = m->clock();

  return SendUpdates(layer_id);
}

template <typename Dtype>
int PSThread<Dtype>::UpdateBN(shared_ptr<Msg> m) {

  CHECK_EQ(m->num_blobs(), 1) << "expect 1 blob";

  ParamHelper<Dtype>::AddDataFromMsg(ps_solver_->net(), m);
  // do not scale here

  // check mean and var

  const BlobInfo& bi = m->blob_info(0);
  const string& layer_name = bi.blob_name();
  LOG(INFO) << "BN State 4......PS recv, before avg client, layer name: " << layer_name;
//  LOG(INFO) << "num of blobs from msg:" << m->num_blobs() << ";should be 1";
  ParamHelper<Dtype>::PrintBN(ps_solver_->net(), layer_name);

  return 0;
}

template <typename Dtype>
void PSThread<Dtype>::AvgBN(const shared_ptr<Net<Dtype> > net,
                               int num, int layer_id) {
  shared_ptr<Layer<Dtype> > layer = net->layers()[layer_id];
  const std::string& type = layer->type();
  
  if (type == "BatchNorm") { // || type == "MKLBatchNorm"
    vector<shared_ptr<Blob<Dtype> > >& bn_blobs = layer->blobs();
    
    // check bn mean data
    LOG(INFO) << "before avg client, layer idx:" << layer_id;
    ParamHelper<Dtype>::PrintBN(net, layer_id);
    
    for (int i = 0; i < bn_blobs.size(); i++) {
      caffe_scal(bn_blobs[i]->count(), (Dtype)(1.0 / (Dtype)num),
                 bn_blobs[i]->mutable_cpu_data());
    }

    // check bn mean dat
    LOG(INFO) << "after avg client, layer idx:" << layer_id;
    ParamHelper<Dtype>::PrintBN(net, layer_id);
    
  }
}

template <typename Dtype>
void PSThread<Dtype>::SendParam(shared_ptr<Net<Dtype> > net,
                                const vector<string>& layer_names,
                                int dst, int clock) {
  shared_ptr<Msg> r(new Msg());
  r->set_type(PUT_PARAM);
  r->set_dst(dst);
  r->set_src(NodeEnv::Instance()->ID());
  r->set_clock(clock);

  // shared_ptr<Net<Dtype> > net = ps_solver_->net();
  ParamHelper<Dtype>::CopyParamDataToMsg(net, layer_names, r);

  this->SendMsg(r);
}

template <typename Dtype>
void PSThread<Dtype>::UpdateLayer(int layer_id) {
  CHECK(this->IsLearnable(layer_id));

  const vector<int>& learnable_indices = this->GetLearnableIndices(layer_id);
  for (int i = 0; i < learnable_indices.size(); i++) {
    ps_solver_->CommitGradient(learnable_indices[i]);
  }
}

template <typename Dtype>
void PSThread<Dtype>::BroadcastLayer(int layer_id) {
  vector<string> layer_names;
  layer_names.push_back(ps_solver_->net()->layer_names()[layer_id]);

  for (int i = 0; i < client_ids_.size(); i++) {
    SendParam(ps_solver_->net(), layer_names, client_ids_[i], iter_ + 1);
  }
}

template <typename Dtype>
int PSThread<Dtype>::SendUpdates(int layer_id) {
  CHECK_EQ(staleness_, 0) << "only support sync mode";

  const vector<int>& clock_vec = msg_clocks_[layer_id];
  int clock_bound = *std::min_element(clock_vec.begin(), clock_vec.end());

  vector<shared_ptr<Msg> > *pmsg_vec = &client_msgs_[layer_id];
  int num_updates = 0;

  // update parameters from clients
  for (int i = 0; i < pmsg_vec->size(); i++) {
    if (pmsg_vec->at(i) != NULL && clock_vec[i] <= clock_bound) {
      ParamHelper<Dtype>::AddDiffFromMsg(ps_solver_->net(), pmsg_vec->at(i));
      pmsg_vec->at(i).reset();
      num_updates++;
    }
  }
//  LOG(INFO) << "layer: " << layer_id <<", num update: " << num_updates << ", total size:" << pmsg_vec->size();
  if (num_updates > 0) {
    AvgBN(ps_solver_->net(), num_updates, layer_id);
    
    ParamHelper<Dtype>::ScalDiff(ps_solver_->net(),
                                 (Dtype)(1.0 / (Dtype)num_updates),
                                 layer_id);
    UpdateLayer(layer_id);
    
    // TODO: here send bn blobs to convs
    
    BroadcastLayer(layer_id);
    updated_layers_++;
  }

  if (updated_layers_ >= num_learnable_layers_) {
    #if 0
    LOG(INFO) << "iter: " << iter_;
    ParamHelper<Dtype>::PrintDiff(ps_solver_->net());
    LOG(INFO) << "param:";
    ParamHelper<Dtype>::PrintParam(ps_solver_->net());
    #endif

    ps_solver_->net()->ClearParamDiffs();

    // TODO: clear bn blobs
    updated_layers_ = 0;
    iter_++;
    ps_solver_->IncreaseIter();
  }

  if (iter_ >= max_iter_ && test_node_ < 0) {
    return -1;
  }

  return 0;
}

template <typename Dtype>
void PSThread<Dtype>::RegisterNode(shared_ptr<Msg> m) {
  LOG(INFO) << "registering node: " << m->src();

  int client_idx = client_ids_.size();

  int node_id = m->src();
  client_ids_.push_back(node_id);
  client_idx_map_[node_id] = client_idx;

  // wait for all the conv. clients
  LOG(INFO) << "total workers: " << num_workers_
            << ", current workers: " << client_ids_.size();
  if (client_ids_.size() >= num_workers_) {
    shared_ptr<Net<Dtype> > ps_net = ps_solver_->net();
    for (int i = 0; i < client_ids_.size(); i++) {
      // send the expected clock to clients
      SendParam(ps_net, ps_net->layer_names(), client_ids_[i], iter_);
    }
  }
}


template <typename Dtype>
void PSThread<Dtype>::Run() {
  Caffe::set_root_solver(true);
  // use root solver as ps solver
  ps_solver_ = (SGDSolver<Dtype> *)NodeEnv::Instance()->GetRootSolver();
  shared_ptr<Net<Dtype> > ps_net = ps_solver_->net();

  // init internal states
  const vector<shared_ptr<Layer<Dtype> > >& layer_vec = ps_net->layers();
  client_msgs_.resize(layer_vec.size());
  msg_clocks_.resize(layer_vec.size());

  for (int i = 0; i < layer_vec.size(); i++) {
    client_msgs_[i].resize(num_workers_);
    msg_clocks_[i].resize(num_workers_);

    for (int j = 0; j < msg_clocks_[i].size(); j++) {
      msg_clocks_[i][j] = iter_;
    }
  }

  num_learnable_layers_ = this->InitParamMap(ps_net);

  while (!this->must_stop()) {
    shared_ptr<Msg> m = this->RecvMsg(true);

    if (m->type() == PUT_GRADIENT) {
      if (UpdateParam(m) < 0) {
        this->SendExit();
        return;
      }
    } else if (m->type() == PUT_BN) {
      UpdateBN(m);
    } else if (m->type() == REGISTER_NODE) {
      RegisterNode(m);
    } else if (m->type() == GET_PARAM) {
      test_node_ = m->src();
      SendParam(ps_net, ps_net->layer_names(), m->src(), iter_);
      if (iter_ >= max_iter_) {
        this->SendExit();
        return;
      }
    } else if (m->type() == EXIT_TRAIN) {
      // exit training
      this->SendExit();
      return;
    } else {
      LOG(ERROR) << "Cannot deal with message type: " << m->type()
                 << " from: " << m->src();
    }
  }
}

INSTANTIATE_CLASS(PSThread);

}  // end namespace caffe



