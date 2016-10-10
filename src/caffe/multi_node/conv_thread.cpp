
#include <map>
#include <string>
#include <vector>

#include "caffe/multi_node/conv_thread.hpp"
#include "caffe/multi_node/param_helper.hpp"

namespace caffe {

template <typename Dtype>
boost::mutex ConvThread<Dtype>::conv_id_mutex_;

template <typename Dtype>
int ConvThread<Dtype>::conv_id_ = 1;

template <typename Dtype>
boost::barrier *ConvThread<Dtype>::pconv_barrier_ = NULL;

template <typename Dtype>
SGDSolver<Dtype> *ConvThread<Dtype>::full_solver_ = NULL;


template <typename Dtype>
void ConvThread<Dtype>::ForwardLayer(shared_ptr<Net<Dtype> > conv_net,
                                     int layer_id) {
  shared_ptr<Layer<Dtype> > l = conv_net->layers()[layer_id];
  const vector<Blob<Dtype>*>& bottom = conv_net->bottom_vecs()[layer_id];
  const vector<Blob<Dtype>*>& top = conv_net->top_vecs()[layer_id];

  string skip_layer("LRN");

  if (skip_layer == l->type()) {
    shared_ptr<Net<Dtype> > full_net = full_solver_->net();
    const vector<Blob<Dtype>*>& full_bottom = full_net->bottom_vecs()[layer_id];
    const vector<Blob<Dtype>*>& full_top = full_net->top_vecs()[layer_id];

    // copy bottom blobs to full solver
    ParamHelper<Dtype>::CopyBlobData(bottom, full_bottom, this->GetWorkerId());

    pconv_barrier_->wait();
    if (this->GetWorkerId() == 0) {
      #ifdef USE_MKL
      int orig_thread = mkl_set_num_threads_local(this->GetWorkerNum());
      #endif

      shared_ptr<Layer<Dtype> > full_layer = full_net->layers()[layer_id];
      full_layer->Forward(full_bottom, full_top);

      #ifdef USE_MKL
      mkl_set_num_threads_local(orig_thread);
      #endif
    }

    pconv_barrier_->wait();
    ParamHelper<Dtype>::CopyBlobData(full_top, top, this->GetWorkerId());
  } else {
    l->Forward(bottom, top);
  }
}


template <typename Dtype>
void ConvThread<Dtype>::ConvForward() {
  Caffe::set_root_solver(false);
  WorkerSolver<Dtype> *pconv = NULL;
  pconv = (WorkerSolver<Dtype> *)NodeEnv::Instance()->PopFreeSolver();

  
  Solver<Dtype> *root_solver = NULL;
  root_solver = (Solver<Dtype> *)NodeEnv::Instance()->GetRootSolver();
  LOG(INFO) << "bn root_solver blob.......0";
  ParamHelper<Dtype>::PrintBN(root_solver->net(), 315);

  

  if (NULL == pconv) {
    Solver<Dtype> *root_solver = NULL;
    root_solver = (Solver<Dtype> *)NodeEnv::Instance()->GetRootSolver();
    const SolverParameter& solver_param = NodeEnv::Instance()->SolverParam();
    pconv = (WorkerSolver<Dtype> *)this->NewSolver(root_solver, solver_param);
  }
  shared_ptr<Net<Dtype> > conv_net = pconv->net();
  conv_net->ClearParamDiffs();

  LOG(INFO) << "BN_Forward init bn param_solver blob.......";
  ParamHelper<Dtype>::PrintBN(param_solver_->net(), bn_layers_idx_[bn_layers_idx_.size()-1]);
    
  // set conv_net with new BN blobs
  for (int i = 0; i < bn_layers_idx_.size(); ++i) {
    ParamHelper<Dtype>::CopyDataFromNet(conv_net, param_solver_->net(), bn_layers_idx_[i]);
//    LOG(INFO) << "idx: " << bn_layers_idx_[i]<< ", type: " << conv_net->layers()[bn_layers_idx_[i]]->type();
  }

  LOG(INFO) << "BN_Forward init bn conv blob.......";
  ParamHelper<Dtype>::PrintBN(conv_net, bn_layers_idx_[bn_layers_idx_.size()-1]);
  
  conv_net->ForwardPrefilled();

  // notify the param thread
  shared_ptr<Msg> m(new Msg());
  int conv_id = NewConvId();
  m->set_conv_id(conv_id);
  m->set_src(this->GetWorkerId());
  m->set_type(FORWARD);
  m->set_dst(ROOT_THREAD_ID);

  // append the solver pointer
  m->AppendData(&pconv, sizeof(pconv));

  this->SendMsg(m);
  NodeEnv::Instance()->PutSolver(conv_id, pconv);
//  LOG(INFO) << "test0..................";
LOG(INFO) << "bn root_solver blob.......1";
ParamHelper<Dtype>::PrintBN(root_solver->net(), 315);

}


template <typename Dtype>
WorkerSolver<Dtype> *ConvThread<Dtype>::PrepareBwdSolver(shared_ptr<Msg> m) {
  int conv_id = m->conv_id();
  WorkerSolver<Dtype> *pconv = NULL;
  pconv = (WorkerSolver<Dtype> *)NodeEnv::Instance()->FindSolver(conv_id);
  CHECK(pconv != NULL) << "cannot find conv_id: " << conv_id
                       << ", msg type: " << m->type();

  return pconv;
}

template <typename Dtype>
void ConvThread<Dtype>::SendLayer(int layer_id) {
  shared_ptr<Msg> m(new Msg());
  m->set_src(this->GetWorkerId());
  m->set_dst(ROOT_THREAD_ID);
  m->set_type(PUT_GRADIENT);

  m->AppendData(&layer_id, sizeof(layer_id));

  const string& layer_name = param_solver_->net()->layer_names()[layer_id];
  vector<string> name_vec;
  name_vec.push_back(layer_name);

  ParamHelper<Dtype>::CopyParamDiffToMsg(param_solver_->net(), name_vec, m);
  // m->AppendData(&param_solver_, sizeof(param_solver_));

  this->SendMsg(m);
}

template <typename Dtype>
void ConvThread<Dtype>::SendBN(int layer_id) {
  shared_ptr<Msg> m(new Msg());
  m->set_src(this->GetWorkerId());
  m->set_dst(ROOT_THREAD_ID);
  m->set_type(PUT_BN);

  m->AppendData(&layer_id, sizeof(layer_id));

  const string& layer_name = param_solver_->net()->layer_names()[layer_id];
  vector<string> name_vec;
  name_vec.push_back(layer_name);

  ParamHelper<Dtype>::CopyParamDataToMsg(param_solver_->net(), name_vec, m);

  // check bn mean data
  LOG(INFO) << "BN State 1......";
  LOG(INFO) << "send to param conv, after avg solver, layer idx: " << layer_id ;
  ParamHelper<Dtype>::PrintBN(param_solver_->net(), layer_id);
  
  this->SendMsg(m);
}

template <typename Dtype>
void ConvThread<Dtype>::BackwardLayer(WorkerSolver<Dtype> *psolver,
                                      int layer_id) {
  shared_ptr<Net<Dtype> > conv_net = psolver->net();
  conv_net->BackwardFromTo(layer_id, layer_id);
  const std::string& type = conv_net->layers()[layer_id]->type();
  if (type == "BatchNorm") { // || type == "MKLBatchNorm"
      ParamHelper<Dtype>::AddDataFromNet(param_solver_->net(), conv_net, layer_id);
  }
  ParamHelper<Dtype>::AddDiffFromNet(param_solver_->net(), conv_net, layer_id);
}

template <typename Dtype>
void ConvThread<Dtype>::SyncedBackward(WorkerSolver<Dtype> *prev_solver,
                                       int prev_idx,
                                       shared_ptr<Msg> m) {
  WorkerSolver<Dtype> * pconv = PrepareBwdSolver(m);
  shared_ptr<Net<Dtype> > conv_net = pconv->net();
  const vector<shared_ptr<Layer<Dtype> > >& layers = conv_net->layers();

  shared_ptr<Net<Dtype> > param_net;
  param_net = param_solver_->net();

  // backward and sync per layer
  for (int i = layers.size() - 1; i >= prev_idx; i--) {
    conv_net->BackwardFromTo(i, i);
    const std::string& type = layers[i]->type();
    if (type == "BatchNorm") { // || type == "MKLBatchNorm"
      ParamHelper<Dtype>::AddDataFromNet(param_net, conv_net, i);

      // check mean and var
      LOG(INFO) << "BN State 0......";
      LOG(INFO) << "backward compute, before avg solver, layer idx:" << i;
      ParamHelper<Dtype>::PrintBN(param_net, i);

      ParamHelper<Dtype>::ScalData(param_net, (Dtype)(1.0 / num_sub_solvers_), i);

      LOG(INFO) << "backward compute, after avg solver, layer idx:" << i;
      ParamHelper<Dtype>::PrintBN(param_net, i);

      SendBN(i);
    }

    ParamHelper<Dtype>::AddDiffFromNet(param_net, conv_net, i);
    ParamHelper<Dtype>::ScalDiff(param_net, (Dtype)(1.0 / num_sub_solvers_), i);

    SendLayer(i);
  }

  for (int i = prev_idx - 1; i >= 0; i--) {
    const std::string& type = layers[i]->type();
    if (prev_solver != NULL) {
      prev_solver->net()->BackwardFromTo(i, i);
      if (type == "BatchNorm") { // || type == "MKLBatchNorm"
        ParamHelper<Dtype>::AddDataFromNet(param_net, prev_solver->net(), i);
      }
      ParamHelper<Dtype>::AddDiffFromNet(param_net, prev_solver->net(), i);
    }

    conv_net->BackwardFromTo(i, i);
    if (type == "BatchNorm") { // || type == "MKLBatchNorm"
      ParamHelper<Dtype>::AddDataFromNet(param_net, conv_net, i);

      // check mean and var
      LOG(INFO) << "BN State 0......";
      LOG(INFO) << "backward compute, before avg solver, layer idx:" << i;
      ParamHelper<Dtype>::PrintBN(param_net, i);

      ParamHelper<Dtype>::ScalData(param_net, (Dtype)(1.0 / num_sub_solvers_), i);

      LOG(INFO) << "backward compute, after avg solver, layer idx:" << i;
      ParamHelper<Dtype>::PrintBN(param_net, i);


      SendBN(i);
    }
    
    ParamHelper<Dtype>::AddDiffFromNet(param_net, conv_net, i);
    ParamHelper<Dtype>::ScalDiff(param_net, (Dtype)(1.0 / num_sub_solvers_), i);

    SendLayer(i);
  }

  NodeEnv::Instance()->DeleteSolver(m->conv_id());
  NodeEnv::Instance()->PushFreeSolver(pconv);
}


template <typename Dtype>
void ConvThread<Dtype>::ConvBackward(shared_ptr<Msg> m) {
  WorkerSolver<Dtype> *pconv = PrepareBwdSolver(m);
  shared_ptr<Net<Dtype> > conv_net = pconv->net();

  const vector<shared_ptr<Layer<Dtype> > >& layers = conv_net->layers();

  // do backward from layer to layer
  for (int i = layers.size() - 1; i >= 0; i--) {
    conv_net->BackwardFromTo(i, i);
    ParamHelper<Dtype>::AddDiffFromNet(param_solver_->net(), conv_net, i);
  }
  // add BN blobs 
  for (int i = 0; i < bn_layers_idx_.size(); ++i) {
    ParamHelper<Dtype>::AddDataFromNet(param_solver_->net(), conv_net, bn_layers_idx_[i]);
  }

  NodeEnv::Instance()->DeleteSolver(m->conv_id());
  NodeEnv::Instance()->PushFreeSolver(pconv);
}


template <typename Dtype>
void ConvThread<Dtype>::Run() {
  #ifdef USE_MKL
  int n = mkl_get_max_threads();
  LOG(INFO) << "max mkl threads: " << n;
  mkl_set_dynamic(false);
  #endif

  Solver<Dtype> *root_solver = NULL;
  root_solver = (Solver<Dtype> *)NodeEnv::Instance()->GetRootSolver();
  const SolverParameter& solver_param = NodeEnv::Instance()->SolverParam();
  param_solver_ = (WorkerSolver<Dtype> *)this->NewSolver(root_solver,
                                                         solver_param);
  
  const vector<shared_ptr<Layer<Dtype> > >& layers = param_solver_->net()->layers();
  for (int i = 0; i < layers.size(); i++) {
    const std::string& type = layers[i]->type();
  //  LOG(INFO) << "layer " << type <<", blob size:" << layers[i]->blobs().size();
    if (type == "BatchNorm") { // || type == "MKLBatchNorm"
        bn_layers_idx_.push_back(i);
    }
  }
  LOG(INFO) << "bn layer size:" << bn_layers_idx_.size();

  while (!this->must_stop()) {
       
    for (int i = 0; i < num_sub_solvers_; i++) {
      ConvForward();
    }

    WorkerSolver<Dtype> *prev_solver = NULL;
    int prev_conv_id = 0;

    int num_layers = root_solver->net()->layers().size();
    // next layer to be processed for iter num_sub_solvers_ - 1
    int layer_idx = num_layers - 1;

    bool blocked_recv = true;
    int num_bwd = 0;
    while (num_bwd < num_sub_solvers_) {
      shared_ptr<Msg> r = this->RecvMsg(blocked_recv);

      // exit training
      if (r != NULL && r->type() == EXIT_TRAIN) {
        return;
      }

      if (num_bwd < num_sub_solvers_ - 2) {
        ConvBackward(r);
        blocked_recv = true;
        LOG(INFO) << "------------------------0, conv_id" << r->conv_id() << ","<<num_bwd;
        num_bwd++;
      } else if (num_sub_solvers_ >= 2 && num_bwd == num_sub_solvers_ - 2) {
        if (layer_idx >= num_layers - 1) {
          // init solver
          prev_solver = PrepareBwdSolver(r);
          prev_conv_id = r->conv_id();
          LOG(INFO) << "***********prev_conv_id: " << prev_conv_id; 
          BackwardLayer(prev_solver, layer_idx);

          // blocked receive to check the opportunity to overlap
          blocked_recv = false;
          layer_idx--;
        } else if (layer_idx < num_layers - 1 && layer_idx >= 0) {
          if (r != NULL) {
            LOG(INFO) << "------------------------1, conv_id" << r->conv_id() << ","<<num_bwd<<","<< layer_idx;
            BackwardLayer(prev_solver, layer_idx);
            SyncedBackward(prev_solver, layer_idx, r);
            break;
          } else {
            BackwardLayer(prev_solver, layer_idx);
            layer_idx--;
            blocked_recv = false;
          }
        } else {
          num_bwd++;

          // block receive for the last sub-solver
          blocked_recv = true;
        }
      } else if (num_bwd == num_sub_solvers_ - 1) {
      LOG(INFO) << "------------------------2, conv_id" << r->conv_id() << ","<< layer_idx;
        SyncedBackward(prev_solver, 0, r);
        num_bwd++;
      } else {
        // shouldn't be here...
        LOG(FATAL) << "Unepxected status";
      }
    }

    if (prev_solver != NULL) {
      NodeEnv::Instance()->DeleteSolver(prev_conv_id);
      NodeEnv::Instance()->PushFreeSolver(prev_solver);
    }

    // ParamHelper<Dtype>::ScalDiff(param_solver_->net(), (Dtype)0.25);
    // ParamHelper<Dtype>::PrintDiff(param_solver_->net());

    param_solver_->net()->ClearParamDiffs();
    
    // clear bn blob here?

    LOG(INFO) << "bn param_solver before update.......";
    ParamHelper<Dtype>::PrintBN(param_solver_->net(), bn_layers_idx_[bn_layers_idx_.size()-1]);
    LOG(INFO) << "bn root_solver before update.......";
    ParamHelper<Dtype>::PrintBN(root_solver->net(), bn_layers_idx_[bn_layers_idx_.size()-1]);

    // waiting for parameter update
    shared_ptr<Msg> m = this->RecvMsg(true);
    while (m->type() != PUT_PARAM) {
      if (m->type() == EXIT_TRAIN) {
        return;
      }
      m = this->RecvMsg(true);
    }

    LOG(INFO) << "bn param_solver after update.......";
    ParamHelper<Dtype>::PrintBN(param_solver_->net(), bn_layers_idx_[bn_layers_idx_.size()-1]);
    LOG(INFO) << "bn root_solver after update.......";
    ParamHelper<Dtype>::PrintBN(root_solver->net(), bn_layers_idx_[bn_layers_idx_.size()-1]);
    // TODO: set BN blobs to param_solver
  }
}

template <typename Dtype>
void ConvParamThread<Dtype>::SyncLayer(int layer_id) {
  SGDSolver<Dtype> *root_solver = NULL;
  root_solver = (SGDSolver<Dtype> *) NodeEnv::Instance()->GetRootSolver();
  shared_ptr<Net<Dtype> > root_net = root_solver->net();
  const string& layer_name = root_net->layer_names()[layer_id];

  // check whether the PS can be updated
  map<string, int>::iterator iter = layer_to_ps_id_.find(layer_name);

  // there are split layers in caffe which are not in the layer database
  if (iter == layer_to_ps_id_.end()) {
    return;
  }

  int ps_id = iter->second;

  // Put gradient to PS
  shared_ptr<Msg> ps_msg(new Msg());
  ps_msg->set_type(PUT_GRADIENT);
  ps_msg->set_dst(ps_id);
  ps_msg->set_src(NodeEnv::Instance()->ID());

  int ps_local_index = ps_id_map_[ps_id];
  int next_clock = ps_clocks_[ps_local_index] + 1;
  ps_msg->set_clock(next_clock);

  vector<string> layer_vec;
  layer_vec.push_back(layer_name);
  ParamHelper<Dtype>::CopyParamDiffToMsg(root_net, layer_vec, ps_msg);

  this->SendMsg(ps_msg);
  MLOG(INFO) << "Send Gradients for layer: " << layer_name
             << ", clock: " << ps_msg->clock();
}

template <typename Dtype>
void ConvParamThread<Dtype>::SyncBN(int layer_id) {
  SGDSolver<Dtype> *root_solver = NULL;
  root_solver = (SGDSolver<Dtype> *) NodeEnv::Instance()->GetRootSolver();
  shared_ptr<Net<Dtype> > root_net = root_solver->net();
  const string& layer_name = root_net->layer_names()[layer_id];

  // check whether the PS can be updated
  map<string, int>::iterator iter = layer_to_ps_id_.find(layer_name);

  // there are split layers in caffe which are not in the layer database
//  if (iter == layer_to_ps_id_.end()) {
//    return;
//  }

  int ps_id = iter->second;

  // Put BN to PS
  shared_ptr<Msg> ps_msg(new Msg());
  ps_msg->set_type(PUT_BN);
  ps_msg->set_dst(ps_id);
  ps_msg->set_src(NodeEnv::Instance()->ID());

  vector<string> layer_vec;
  layer_vec.push_back(layer_name);
  ParamHelper<Dtype>::CopyParamDataToMsg(root_net, layer_vec, ps_msg);
  
  // check bn mean data
  LOG(INFO) << "BN State 3......";
  LOG(INFO) << "Send to PS, after avg solver, layer name: " << layer_name;
  ParamHelper<Dtype>::PrintBN(root_net, layer_name);

  this->SendMsg(ps_msg);
}

template <typename Dtype>
void ConvParamThread<Dtype>::SyncWithPS() {
  SGDSolver<Dtype> *root_solver = NULL;
  root_solver = (SGDSolver<Dtype> *) NodeEnv::Instance()->GetRootSolver();
  shared_ptr<Net<Dtype> > conv_net = root_solver->net();

  /// send the gradient to parameter servers
  for (int i = 0; i < ps_ids_.size(); i++) {
    shared_ptr<Msg> ps_msg(new Msg());
    ps_msg->set_type(PUT_GRADIENT);
    ps_msg->set_dst(ps_ids_[i]);
    ps_msg->set_src(NodeEnv::Instance()->ID());
    ps_msg->set_clock(ps_clocks_[i]);

    const vector<string>& ps_layers =
                              NodeEnv::Instance()->FindPSLayer(ps_ids_[i]);
    ParamHelper<Dtype>::CopyParamDiffToMsg(conv_net, ps_layers, ps_msg);

    this->SendMsg(ps_msg);
  }
}


template <typename Dtype>
void ConvParamThread<Dtype>::SendActivations() {
  vector<shared_ptr<Net<Dtype> > > net_vec;
  Solver<Dtype> *root_solver = NULL;
  root_solver = (Solver<Dtype> *)NodeEnv::Instance()->GetRootSolver();
  LOG(INFO) << "bn root_solver blob.......2";
  ParamHelper<Dtype>::PrintBN(root_solver->net(), 315);

  

  // prepare the forward nets from conv_threads
  for (int i = 0; i < fwd_msgs_.size(); i++) {
    SGDSolver<Dtype> *pconv = NULL;
    pconv = ((SGDSolver<Dtype> **)fwd_msgs_[i]->ZmsgData(0))[0];
    net_vec.push_back(pconv->net());
  }
  LOG(INFO) << "test1.........." << NodeEnv::Instance()->ID() << ", ID: " << fwd_msgs_[0]->conv_id() << ", worker num:"<< this->GetWorkerNum();

  shared_ptr<Msg> m(new Msg());

  ParamHelper<Dtype>::CopyOutputDataToMsg(net_vec, m);

  // m is a template message for forwarding
  m->set_src(NodeEnv::Instance()->ID());
  int conv_id = fwd_msgs_[0]->conv_id();
  m->set_type(FORWARD);
  m->set_conv_id(conv_id);

  // always use the 0th clock
  m->set_clock(ps_clocks_[0]);
  m->set_dst(-1);

  for (int i = 0; i < gateway_ids_.size(); i++) {
    shared_ptr<Msg> f(new Msg(m));
    f->set_dst(gateway_ids_[i]);
    ParamHelper<Dtype>::CopyBlobDataToMsg(net_vec, gateway_blobs_[i], f);
    this->SendMsg(f);
  }

  for (int i = 0; i < fwd_blobs_.size(); i++) {
    shared_ptr<Msg> f(new Msg(m));
    f->set_dst(fwd_ids_[i]);
    ParamHelper<Dtype>::CopyBlobDataToMsg(net_vec, fwd_blobs_[i], f);
    this->SendMsg(f);
  }

  MLOG(INFO) << "Send Forward from: " << m->src() << ", ID: " << m->conv_id();

  // backup the messages to vector
  shared_ptr<vector<shared_ptr<Msg> > > pvec;
  pvec.reset(new vector<shared_ptr<Msg> >(fwd_msgs_));
  conv_id_to_vec_[conv_id] = pvec;


  LOG(INFO) << "bn root_solver blob.......3";
  ParamHelper<Dtype>::PrintBN(root_solver->net(), 315);

  
}

template <typename Dtype>
void ConvParamThread<Dtype>::ProcessForward(shared_ptr<Msg> m) {
  fwd_msgs_.push_back(m);
  
  if (fwd_msgs_.size() == this->GetWorkerNum()) {
    SendActivations();
    fwd_msgs_.clear();
  }
}

template <typename Dtype>
void ConvParamThread<Dtype>::ProcessBackward(shared_ptr<Msg> m) {
  int conv_id = m->conv_id();

  shared_ptr<vector<shared_ptr<Msg> > > bwd_msgs;

  ConvIdMap::iterator back_iter = bwd_id_to_vec_.find(conv_id);

  if (back_iter == bwd_id_to_vec_.end()) {
    bwd_msgs.reset(new vector<shared_ptr<Msg> >());
    bwd_msgs->push_back(m);
    bwd_id_to_vec_[conv_id] = bwd_msgs;
  } else {
    bwd_msgs = back_iter->second;
    bwd_msgs->push_back(m);
  }

  if (bwd_msgs->size() < gateway_ids_.size()) {
    return;
  }

  ConvIdMap::iterator iter = conv_id_to_vec_.find(conv_id);

  CHECK(iter != conv_id_to_vec_.end());
  shared_ptr<vector<shared_ptr<Msg> > > pfwd_msg = iter->second;

  vector<shared_ptr<Net<Dtype> > > net_vec;
  for (int i = 0; i < pfwd_msg->size(); i++) {
    WorkerSolver<Dtype> *pconv = NULL;
    pconv = ((WorkerSolver<Dtype> **)pfwd_msg->at(i)->ZmsgData(0))[0];
    net_vec.push_back(pconv->net());
  }

  for (int i = 0; i < bwd_msgs->size(); i++) {
    shared_ptr<Msg> bmsg = bwd_msgs->at(i);
    ParamHelper<Dtype>::CopyOutputDiffFromMsg(net_vec, bmsg);
  }

  // swap and send back the buffered messages
  for (int i = 0; i < pfwd_msg->size(); i++) {
    shared_ptr<Msg> m = pfwd_msg->at(i);
    m->set_type(BACKWARD);
    int src = m->src();
    m->set_src(m->dst());
    m->set_dst(src);

    this->SendMsg(m);
  }

  MLOG(INFO) << "Recv Backward src: " << m->src() << ", ID: " << m->conv_id();

  conv_id_to_vec_.erase(iter);

  back_iter = bwd_id_to_vec_.find(conv_id);
  CHECK(back_iter != bwd_id_to_vec_.end());
  bwd_id_to_vec_.erase(back_iter);
}

template <typename Dtype>
int ConvParamThread<Dtype>::PutGradient(shared_ptr<Msg> m) {
  SGDSolver<Dtype> *root_solver = NULL;
  root_solver = (SGDSolver<Dtype> *) NodeEnv::Instance()->GetRootSolver();
  shared_ptr<Net<Dtype> > root_net = root_solver->net();

  int layer_id = (reinterpret_cast<int *>(m->ZmsgData(0)))[0];


  if (this->IsLearnable(layer_id)) {
    ParamHelper<Dtype>::AddDiffFromMsg(root_net, m);
  }

  layer_updates_[layer_id]++;
  if (layer_updates_[layer_id] == this->GetWorkerNum()) {
    layer_updates_[layer_id] = 0;

    if (this->IsLearnable(layer_id)) {
      SyncLayer(layer_id);
    }

    if (layer_id == 0) {
      // check whether we need to exit
      for (int i = 0; i < ps_clocks_.size(); i++) {
        if (ps_clocks_[i] < max_iter_) {
          return 0;
        }
      }

      // notify the param thread to exit
      this->SendExit();
      return -1;
    }
  }

  return 0;
}

template <typename Dtype>
int ConvParamThread<Dtype>::PutBN(shared_ptr<Msg> m) {
  SGDSolver<Dtype> *root_solver = NULL;
  root_solver = (SGDSolver<Dtype> *) NodeEnv::Instance()->GetRootSolver();
  shared_ptr<Net<Dtype> > root_net = root_solver->net();

  int layer_id = (reinterpret_cast<int *>(m->ZmsgData(0)))[0];

//  ParamHelper<Dtype>::CopyDataFromMsg(root_net, m);

  ParamHelper<Dtype>::AddDataFromMsg(root_net, m);
    
  // check bn mean data
  LOG(INFO) << "BN State 2......";
  LOG(INFO) << "conv recv, after avg solver, layer idx: " << layer_id;
  ParamHelper<Dtype>::PrintBN(root_solver->net(), layer_id);

  SyncBN(layer_id);

  return 0;
}

template <typename Dtype>
int ConvParamThread<Dtype>::UpdateParam(shared_ptr<Msg> m) {
  map<int, int>::iterator map_iter = ps_id_map_.find(m->src());
  CHECK(map_iter != ps_id_map_.end());

  // increase clock since we've finished a iteration of compuation
  int ps_idx = map_iter->second;

  SGDSolver<Dtype> *root_solver = NULL;
  root_solver = (SGDSolver<Dtype> *) NodeEnv::Instance()->GetRootSolver();
  shared_ptr<Net<Dtype> > root_net = root_solver->net();

  ParamHelper<Dtype>::CopyParamDataFromMsg(root_net, m);
  ps_updates_[ps_idx]++;

  if (ps_updates_[ps_idx] >= num_learnable_layers_) {
    ps_clocks_[ps_idx]++;
    num_param_update_++;
    ps_updates_[ps_idx] = 0;
  }

  if (num_param_update_ < ps_ids_.size()) {
    return -1;
  }

  MLOG(INFO) << "All params Updated";

  // we have got all the responces from parameter servers
  // and reset the counter
  num_param_update_ = 0;

  // clear the gradients of root net
  root_net->ClearParamDiffs();

  // TODO: clear the bn blobs ?
  

  // notify the worker threads
  shared_ptr<Msg> notify(new Msg());
  notify->set_type(PUT_PARAM);
  notify->set_src(ROOT_THREAD_ID);
  notify->set_dst(WORKER_BCAST);

  notify->AppendData(&num_param_update_, sizeof(num_param_update_));
  this->SendMsg(notify);

  return 0;
}

template <typename Dtype>
void ConvParamThread<Dtype>::Run() {
  #ifdef USE_MKL
  mkl_set_dynamic(false);
  // only use 1 mkl thread to avoid contention
  mkl_set_num_threads_local(1);
  #endif
  Caffe::set_root_solver(true);

  SGDSolver<Dtype> *root_solver = NULL;
  root_solver = (SGDSolver<Dtype> *) NodeEnv::Instance()->GetRootSolver();
  shared_ptr<Net<Dtype> > root_net = root_solver->net();
  root_net->ClearParamDiffs();

  num_learnable_layers_ = this->InitParamMap(root_net);
  // reset the update map
  layer_updates_.resize(root_net->layers().size());
  for (int i = 0; i < layer_updates_.size(); i++) {
    layer_updates_[i] = 0;
  }

  while (!this->must_stop()) {
    shared_ptr<Msg> m = this->RecvMsg(true);

    if (m->type() == PUT_GRADIENT) {
      if (PutGradient(m) < 0) {
        return;
      }
    } else if (m->type() == PUT_BN) {
      PutBN(m);
    } else if (m->type() == PUT_PARAM) {
      UpdateParam(m);
    } else if (m->type() == FORWARD) {
//    LOG(INFO) << "test1..................";
      ProcessForward(m);
    } else if (m->type() == BACKWARD) {
      ProcessBackward(m);
    } else if (m->type() == EXIT_TRAIN) {
      // exit training
      this->SendExit();
      return;
    } else {
      LOG(ERROR) << "PS client: unknown type " << m->type();
    }
  }
}

INSTANTIATE_CLASS(ConvThread);
INSTANTIATE_CLASS(ConvParamThread);

}  // end namespace caffe


