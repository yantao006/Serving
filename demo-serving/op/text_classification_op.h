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

#pragma once
#include <vector>
#include "paddle_inference_api.h"  // NOLINT
#include "demo-serving/text_classification.pb.h"

namespace baidu {
namespace paddle_serving {
namespace serving {

static const char* TEXT_CLASSIFICATION_MODEL_NAME = "text_classification_bow";

class TextClassificationOp
    : public baidu::paddle_serving::predictor::OpWithChannel<
          baidu::paddle_serving::predictor::text_classification::Response> {
 public:
  typedef std::vector<paddle::PaddleTensor> TensorVector;

  DECLARE_OP(TextClassificationOp);

  int inference();
};

}  // namespace serving
}  // namespace paddle_serving
}  // namespace baidu