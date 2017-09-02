// TODO (sergeyk): effect should not be dependent on phase. wasted memcpy.

#include <vector>

#include "caffe/layers/dropout_layer.hpp"
#include "caffe/util/math_functions.hpp"

namespace caffe {

template <typename Dtype>
void DropoutLayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  NeuronLayer<Dtype>::LayerSetUp(bottom, top);
  threshold_ = this->layer_param_.dropout_param().dropout_ratio();
  DCHECK(threshold_ > 0.);
  DCHECK(threshold_ < 1.);
  scale_ = 1. / (1. - threshold_);
  uint_thres_ = static_cast<unsigned int>(UINT_MAX * threshold_);
  scale_train_ = this->layer_param_.dropout_param().scale_train();
}

template <typename Dtype>
void DropoutLayer<Dtype>::Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  NeuronLayer<Dtype>::Reshape(bottom, top);
  // Set up the cache for random number generation
  // ReshapeLike does not work because rand_vec_ is of Dtype uint
  rand_vec_.Reshape(bottom[0]->shape());
}

template <typename Dtype>
void DropoutLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  const Dtype* bottom_data = bottom[0]->cpu_data();
  Dtype* top_data = top[0]->mutable_cpu_data();
  unsigned int* mask = rand_vec_.mutable_cpu_data();
  const int count = bottom[0]->count();
  if (this->phase_ == TRAIN) {
	  //均值采样，采样的概率是留下的概率，根据这个概率来断开连接，但是保留下的数据除以（1-threshold_）,这样在test/predict的时候
       //就不需要像上面描述的那样乘以（1-threshold_）
    // Create random numbers
    caffe_rng_bernoulli(count, 1. - threshold_, mask);
    if (scale_train_) {
      for (int i = 0; i < count; ++i) {
        top_data[i] = bottom_data[i] * mask[i] * scale_;
      }
    } else {
      for (int i = 0; i < count; ++i) {
        top_data[i] = bottom_data[i] * mask[i];
      }
    }
  } else {
    caffe_copy(bottom[0]->count(), bottom_data, top_data);
    if (!scale_train_) {
      caffe_scal<Dtype>(count, 1. / scale_, top_data);
    }
  }
}

template <typename Dtype>
void DropoutLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down,
    const vector<Blob<Dtype>*>& bottom) {
  if (propagate_down[0]) {
    const Dtype* top_diff = top[0]->cpu_diff();
    Dtype* bottom_diff = bottom[0]->mutable_cpu_diff();
    if (this->phase_ == TRAIN) {
      const unsigned int* mask = rand_vec_.cpu_data();
      const int count = bottom[0]->count();
      if (scale_train_) {
        for (int i = 0; i < count; ++i) {
          bottom_diff[i] = top_diff[i] * mask[i] * scale_;
        }
      } else {
        for (int i = 0; i < count; ++i) {
          bottom_diff[i] = top_diff[i] * mask[i];
        }
      }
    } else {
      caffe_copy(top[0]->count(), top_diff, bottom_diff);
      if (!scale_train_) {
        caffe_scal<Dtype>(top[0]->count(), 1. / scale_, bottom_diff);
      }
    }
  }
}


#ifdef CPU_ONLY
STUB_GPU(DropoutLayer);
#endif

INSTANTIATE_CLASS(DropoutLayer);
REGISTER_LAYER_CLASS(Dropout);

}  // namespace caffe
