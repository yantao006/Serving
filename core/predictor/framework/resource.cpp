// Copyright (c) 2019 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "core/predictor/framework/resource.h"
#include <sstream>
#include <string>
#include "core/predictor/common/inner_common.h"
#include "core/predictor/framework/kv_manager.h"
namespace baidu {
namespace paddle_serving {
namespace predictor {

using configure::ResourceConf;
using configure::GeneralModelConfig;
using configure::Shape;
using rec::mcube::CubeAPI;
// __thread bool p_thread_initialized = false;

static void dynamic_resource_deleter(void* d) {
#if 1
  LOG(INFO) << "dynamic_resource_delete on " << bthread_self();
#endif
  delete static_cast<DynamicResource*>(d);
}

DynamicResource::DynamicResource() {}

DynamicResource::~DynamicResource() {}

int DynamicResource::initialize() { return 0; }

std::shared_ptr<RocksDBWrapper> Resource::getDB() { return db; }

std::shared_ptr<PaddleGeneralModelConfig> Resource::get_general_model_config() {
  return _config;
}

void Resource::print_general_model_config(
    const std::shared_ptr<PaddleGeneralModelConfig> & config) {
  if (config == nullptr) {
    LOG(INFO) << "paddle general model config is not set";
    return;
  }
  LOG(INFO) << "Number of Feed Tensor: " << config->_feed_type.size();
  std::ostringstream oss;
  LOG(INFO) << "Feed Type Info";
  for (auto & feed_type : config->_feed_type) {
    oss << feed_type << " ";
  }
  LOG(INFO) << oss.str();
  oss.clear();
  oss.str("");
  LOG(INFO) << "Lod Type Info";

  for (auto is_lod : config->_is_lod_feed) {
    oss << is_lod << " ";
  }

  LOG(INFO) << oss.str();
  oss.clear();
  oss.str("");
  LOG(INFO) << "Capacity Info";
  for (auto & cap : config->_capacity) {
    oss << cap << " ";
  }
  LOG(INFO) << oss.str();
  oss.clear();
  oss.str("");
  LOG(INFO) << "Feed Shape Info";
  int tensor_idx = 0;
  for (auto & shape : config->_feed_shape) {
    for (auto & dim : shape) {
      oss << dim << " ";
    }
    LOG(INFO) << "Tensor[" << tensor_idx++ << "].shape: " << oss.str();
    oss.clear();
    oss.str("");
  }
}

int DynamicResource::clear() { return 0; }

int Resource::initialize(const std::string& path, const std::string& file) {
  ResourceConf resource_conf;
  if (configure::read_proto_conf(path, file, &resource_conf) != 0) {
    LOG(ERROR) << "Failed initialize resource from: " << path << "/" << file;
    return -1;
  }

  // mempool
  if (MempoolWrapper::instance().initialize() != 0) {
    LOG(ERROR) << "Failed proc initialized mempool wrapper";
    return -1;
  }
  LOG(WARNING) << "Successfully proc initialized mempool wrapper";

  if (FLAGS_enable_model_toolkit) {
    int err = 0;
    std::string model_toolkit_path = resource_conf.model_toolkit_path();
    if (err != 0) {
      LOG(ERROR) << "read model_toolkit_path failed, path[" << path
                 << "], file[" << file << "]";
      return -1;
    }
    std::string model_toolkit_file = resource_conf.model_toolkit_file();
    if (err != 0) {
      LOG(ERROR) << "read model_toolkit_file failed, path[" << path
                 << "], file[" << file << "]";
      return -1;
    }
    if (InferManager::instance().proc_initialize(
            model_toolkit_path.c_str(), model_toolkit_file.c_str()) != 0) {
      LOG(ERROR) << "failed proc initialize modeltoolkit, config: "
                 << model_toolkit_path << "/" << model_toolkit_file;
      return -1;
    }

    if (KVManager::instance().proc_initialize(
            model_toolkit_path.c_str(), model_toolkit_file.c_str()) != 0) {
      LOG(ERROR) << "Failed proc initialize kvmanager, config: "
                 << model_toolkit_path << "/" << model_toolkit_file;
    }
  }

  if (THREAD_KEY_CREATE(&_tls_bspec_key, dynamic_resource_deleter) != 0) {
    LOG(ERROR) << "unable to create tls_bthread_key of thrd_data";
    return -1;
  }
  // init rocksDB instance
  if (db.get() == nullptr) {
    db = RocksDBWrapper::RocksDBWrapperFactory("kvdb");
  }

  THREAD_SETSPECIFIC(_tls_bspec_key, NULL);
  return 0;
}

int Resource::general_model_initialize(
    const std::string& path, const std::string & file) {
  if (!FLAGS_enable_general_model) {
    return 0;
  }

  GeneralModelConfig model_config;
  if (configure::read_proto_conf(path, file, &model_config) != 0) {
    LOG(ERROR) << "Failed initialize resource from: " << path << "/" << file;
    return -1;
  }

  _config.reset(new PaddleGeneralModelConfig());
  _config->_feed_type.resize(model_config.feed_type_size());
  _config->_is_lod_feed.resize(model_config.is_lod_feed_size());
  _config->_capacity.resize(model_config.feed_shape_size());
  _config->_feed_shape.resize(model_config.feed_shape_size());
  for (int i = 0; i < model_config.is_lod_feed_size(); ++i) {
    _config->_feed_type[i] = model_config.feed_type(i);
    if (model_config.is_lod_feed(i)) {
      _config->_feed_shape[i] = {-1};
      _config->_is_lod_feed[i] = true;
    } else {
      _config->_capacity[i] = 1;
      _config->_is_lod_feed[i] = false;
      for (int j = 0; j < model_config.feed_shape(i).shape_size(); ++j) {
        int dim = model_config.feed_shape(i).shape(j);
        _config->_feed_shape[i].push_back(dim);
        _config->_capacity[i] *= dim;
      }
    }
  }
  return 0;
}

int Resource::cube_initialize(const std::string& path,
                              const std::string& file) {
  // cube
  if (!FLAGS_enable_cube) {
    return 0;
  }

  ResourceConf resource_conf;
  if (configure::read_proto_conf(path, file, &resource_conf) != 0) {
    LOG(ERROR) << "Failed initialize resource from: " << path << "/" << file;
    return -1;
  }

  int err = 0;
  std::string cube_config_file = resource_conf.cube_config_file();
  if (err != 0) {
    LOG(ERROR) << "reade cube_config_file failed, path[" << path << "], file["
               << cube_config_file << "]";
    return -1;
  }
  err = CubeAPI::instance()->init(cube_config_file.c_str());
  if (err != 0) {
    LOG(ERROR) << "failed initialize cube, config: " << cube_config_file
               << " error code : " << err;
    return -1;
  }

  LOG(INFO) << "Successfully initialize cube";

  return 0;
}

int Resource::thread_initialize() {
  // mempool
  if (MempoolWrapper::instance().thread_initialize() != 0) {
    LOG(ERROR) << "Failed thread initialized mempool wrapper";
    return -1;
  }
  LOG(WARNING) << "Successfully thread initialized mempool wrapper";

  // infer manager
  if (FLAGS_enable_model_toolkit &&
      InferManager::instance().thrd_initialize() != 0) {
    LOG(ERROR) << "Failed thrd initialized infer manager";
    return -1;
  }

  DynamicResource* p_dynamic_resource =
      reinterpret_cast<DynamicResource*>(THREAD_GETSPECIFIC(_tls_bspec_key));
  if (p_dynamic_resource == NULL) {
    p_dynamic_resource = new (std::nothrow) DynamicResource;
    if (p_dynamic_resource == NULL) {
      LOG(ERROR) << "failed to create tls DynamicResource";
      return -1;
    }
    if (p_dynamic_resource->initialize() != 0) {
      LOG(ERROR) << "DynamicResource initialize failed.";
      delete p_dynamic_resource;
      p_dynamic_resource = NULL;
      return -1;
    }

    if (THREAD_SETSPECIFIC(_tls_bspec_key, p_dynamic_resource) != 0) {
      LOG(ERROR) << "unable to set tls DynamicResource";
      delete p_dynamic_resource;
      p_dynamic_resource = NULL;
      return -1;
    }
  }
#if 0
    LOG(INFO) << "Successfully thread initialized dynamic resource";
#else
  LOG(INFO) << bthread_self()
            << ": Successfully thread initialized dynamic resource "
            << p_dynamic_resource;

#endif
  return 0;
}

int Resource::thread_clear() {
  // mempool
  if (MempoolWrapper::instance().thread_clear() != 0) {
    LOG(ERROR) << "Failed thread clear mempool wrapper";
    return -1;
  }

  // infer manager
  if (FLAGS_enable_model_toolkit &&
      InferManager::instance().thrd_clear() != 0) {
    LOG(ERROR) << "Failed thrd clear infer manager";
    return -1;
  }

  DynamicResource* p_dynamic_resource =
      reinterpret_cast<DynamicResource*>(THREAD_GETSPECIFIC(_tls_bspec_key));
  if (p_dynamic_resource == NULL) {
#if 0
    LOG(ERROR) << "tls dynamic resource shouldn't be null after "
        << "thread_initialize";
#else
    LOG(ERROR)
        << bthread_self()
        << ": tls dynamic resource shouldn't be null after thread_initialize";
#endif
    return -1;
  }
  if (p_dynamic_resource->clear() != 0) {
    LOG(ERROR) << "Failed to invoke dynamic resource clear";
    return -1;
  }

  // ...
  return 0;
}

int Resource::reload() {
  if (FLAGS_enable_model_toolkit && InferManager::instance().reload() != 0) {
    LOG(ERROR) << "Failed reload infer manager";
    return -1;
  }

  // other resource reload here...
  return 0;
}

int Resource::finalize() {
  if (FLAGS_enable_model_toolkit &&
      InferManager::instance().proc_finalize() != 0) {
    LOG(ERROR) << "Failed proc finalize infer manager";
    return -1;
  }
  if (CubeAPI::instance()->destroy() != 0) {
    LOG(ERROR) << "Destory cube api failed ";
    return -1;
  }
  THREAD_KEY_DELETE(_tls_bspec_key);

  return 0;
}

}  // namespace predictor
}  // namespace paddle_serving
}  // namespace baidu
