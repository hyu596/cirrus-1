#ifndef _OPTMETHOD_H_
#define _OPTMETHOD_H_

#include <vector>
#include <utility>
#include <Model.h>
#include <ModelGradient.h>
#include <Utils.h>
#include <MlUtils.h>

namespace cirrus {
  class OptimizationMethod {
    public:
      OptimizationMethod(double lr);
      virtual void sgd_update(
          std::vector<FEATURE_TYPE>& weights, const ModelGradient* gradient, std::vector<FEATURE_TYPE>& weights_hist_) = 0;
      virtual void edit_weight(double& weight) = 0;
    public:
      double learning_rate;
  };
}

#endif

