#ifndef CAFFE_LAYER_H_
#define CAFFE_LAYER_H_

#include <algorithm>
#include <string>
#include <vector>

#include "caffe/blob.hpp"
#include "caffe/common.hpp"
#include "caffe/layer_factory.hpp"
#include "caffe/proto/caffe.pb.h"
#include "caffe/util/math_functions.hpp"

/**
 Forward declare boost::thread instead of including boost/thread.hpp
 to avoid a boost/NVCC issues (#1009, #1010) on OSX.
 */
// 前向声明boost的互斥类：boost::mutex 
namespace boost { class mutex; }

namespace caffe {

/**
 * @brief An interface for the units of computation which can be composed into a
 *        Net.
 *
 * Layer%s must implement a Forward function, in which they take their input
 * (bottom) Blob%s (if any) and compute their output Blob%s (if any).
 * They may also implement a Backward function, in which they compute the error
 * gradients with respect to their input Blob%s, given the error gradients with
 * their output Blob%s.
 */
template <typename Dtype>
class Layer {// 抽象基类，有纯虚函数，不能实例化，定义了所有layer的基本接口
 public:
  /**
   * You should not implement your own constructor. Any set up code should go
   * to SetUp(), where the dimensions of the bottom blobs are provided to the
   * layer.
   */
	 // 显式构造函数，不需要重写，获得成员变量layer_param_、phase_、blobs_的值
  explicit Layer(const LayerParameter& param)
    : layer_param_(param), is_shared_(false) {
      // Set phase and copy blobs (if there are any).
      phase_ = param.phase();
      if (layer_param_.blobs_size() > 0) {
        blobs_.resize(layer_param_.blobs_size());
        for (int i = 0; i < layer_param_.blobs_size(); ++i) {
          blobs_[i].reset(new Blob<Dtype>());
          blobs_[i]->FromProto(layer_param_.blobs(i));
        }
      }
    }
  // 虚析构函数
  virtual ~Layer() {}

  /**
   * @brief Implements common layer setup functionality.
   *
   * @param bottom the preshaped input blobs
   * @param top
   *     the allocated but unshaped output blobs, to be shaped by Reshape
   *
   * Checks that the number of bottom and top blobs is correct.
   * Calls LayerSetUp to do special layer setup for individual layer types,
   * followed by Reshape to set up sizes of top blobs and internal buffers.
   * Sets up the loss weight multiplier blobs for any non-zero loss weights.
   * This method may not be overridden.
   */
  // layer初始化,此方法不需要重写
  void SetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
    InitMutex();
    CheckBlobCounts(bottom, top);
    LayerSetUp(bottom, top);
    Reshape(bottom, top);
    SetLossWeights(top);
  }

  /**
   * @brief Does layer-specific setup: your layer should implement this function
   *        as well as Reshape.
   *
   * @param bottom
   *     the preshaped input blobs, whose data fields store the input data for
   *     this layer
   * @param top
   *     the allocated but unshaped output blobs
   *
   * This method should do one-time layer specific setup. This includes reading
   * and processing relevent parameters from the <code>layer_param_</code>.
   * Setting up the shapes of top blobs and internal buffers should be done in
   * <code>Reshape</code>, which will be called before the forward pass to
   * adjust the top blob sizes.
   */
  // 通过Layer参数即LayerParameter类获得layer中某些成员变量的值
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {}

  /**
   * @brief Whether a layer should be shared by multiple nets during data
   *        parallelism. By default, all layers except for data layers should
   *        not be shared. data layers should be shared to ensure each worker
   *        solver access data sequentially during data parallelism.
   */
  // 获得layer data共享状态:一个layer的data是否被多个net共享
  virtual inline bool ShareInParallel() const { return false; }

  /** @brief Return whether this layer is actually shared by other nets.
   *         If ShareInParallel() is true and using more than one GPU and the
   *         net has TRAIN phase, then this function is expected return true.
   */
  // 获得layer是否被其它net共享
  inline bool IsShared() const { return is_shared_; }

  /** @brief Set whether this layer is actually shared by other nets
   *         If ShareInParallel() is true and using more than one GPU and the
   *         net has TRAIN phase, then is_shared should be set true.
   */
  // 设置layer是否被其它net共享
  inline void SetShared(bool is_shared) {
    CHECK(ShareInParallel() || !is_shared)
        << type() << "Layer does not support sharing.";
    is_shared_ = is_shared;
  }

  /**
   * @brief Adjust the shapes of top blobs and internal buffers to accommodate
   *        the shapes of the bottom blobs.
   *
   * @param bottom the input blobs, with the requested input shapes
   * @param top the top blobs, which should be reshaped as needed
   *
   * This method should reshape top blobs as needed according to the shapes
   * of the bottom (input) blobs, as well as reshaping any internal buffers
   * and making any other necessary adjustments so that the layer can
   * accommodate the bottom blobs.
   */
  // 调整top blobs的shape,纯虚函数，在子类中必须实现
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) = 0;

  /**
   * @brief Given the bottom blobs, compute the top blobs and the loss.
   *
   * @param bottom
   *     the input blobs, whose data fields store the input data for this layer
   * @param top
   *     the preshaped output blobs, whose data fields will store this layers'
   *     outputs
   * \return The total loss from the layer.
   *
   * The Forward wrapper calls the relevant device wrapper function
   * (Forward_cpu or Forward_gpu) to compute the top blob values given the
   * bottom blobs.  If the layer has any non-zero loss_weights, the wrapper
   * then computes and returns the loss.
   *
   * Your layer should implement Forward_cpu and (optionally) Forward_gpu.
   */
  // 前向传播，通过输入bottom blobs，计算输出top blobs和返回loss和
  inline Dtype Forward(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  /**
   * @brief Given the top blob error gradients, compute the bottom blob error
   *        gradients.
   *
   * @param top
   *     the output blobs, whose diff fields store the gradient of the error
   *     with respect to themselves
   * @param propagate_down
   *     a vector with equal length to bottom, with each index indicating
   *     whether to propagate the error gradients down to the bottom blob at
   *     the corresponding index
   * @param bottom
   *     the input blobs, whose diff fields will store the gradient of the error
   *     with respect to themselves after Backward is run
   *
   * The Backward wrapper calls the relevant device wrapper function
   * (Backward_cpu or Backward_gpu) to compute the bottom blob diffs given the
   * top blob diffs.
   *
   * Your layer should implement Backward_cpu and (optionally) Backward_gpu.
   */
  // 反向传播，通过给定top blob误差梯度，计算bottom blob误差梯度
  inline void Backward(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down,
      const vector<Blob<Dtype>*>& bottom);

  /**
   * @brief Returns the vector of learnable parameter blobs.
   */
  // 获得layer的权值、偏置等
  vector<shared_ptr<Blob<Dtype> > >& blobs() {
    return blobs_;
  }

  /**
   * @brief Returns the layer parameter.
   */
  // 获得layer的配置参数
  const LayerParameter& layer_param() const { return layer_param_; }

  /**
   * @brief Writes the layer parameter to a protocol buffer
   */
  // 序列化函数，将layer参数写入protobuf文件
  virtual void ToProto(LayerParameter* param, bool write_diff = false);

  /**
   * @brief Returns the scalar loss associated with a top blob at a given index.
   */
  // 获得top blob指定index的loss值
  inline Dtype loss(const int top_index) const {
    return (loss_.size() > top_index) ? loss_[top_index] : Dtype(0);
  }

  /**
   * @brief Sets the loss associated with a top blob at a given index.
   */
  // 设置top blob指定index的loss值
  inline void set_loss(const int top_index, const Dtype value) {
    if (loss_.size() <= top_index) {
      loss_.resize(top_index + 1, Dtype(0));
    }
    loss_[top_index] = value;
  }

  /**
   * @brief Returns the layer type.
   */
  // 获得layer的类型
  virtual inline const char* type() const { return ""; }

  /**
   * @brief Returns the exact number of bottom blobs required by the layer,
   *        or -1 if no exact number is required.
   *
   * This method should be overridden to return a non-negative value if your
   * layer expects some exact number of bottom blobs.
   */
  // 获得layer所需的bottom blobs的个数
  virtual inline int ExactNumBottomBlobs() const { return -1; }
  /**
   * @brief Returns the minimum number of bottom blobs required by the layer,
   *        or -1 if no minimum number is required.
   *
   * This method should be overridden to return a non-negative value if your
   * layer expects some minimum number of bottom blobs.
   */
  // 获得layer所需的bottom blobs的最少个数
  virtual inline int MinBottomBlobs() const { return -1; }
  /**
   * @brief Returns the maximum number of bottom blobs required by the layer,
   *        or -1 if no maximum number is required.
   *
   * This method should be overridden to return a non-negative value if your
   * layer expects some maximum number of bottom blobs.
   */
  // 获得layer所需的bottom blobs的最多个数
  virtual inline int MaxBottomBlobs() const { return -1; }
  /**
   * @brief Returns the exact number of top blobs required by the layer,
   *        or -1 if no exact number is required.
   *
   * This method should be overridden to return a non-negative value if your
   * layer expects some exact number of top blobs.
   */
  // 获得layer所需的top blobs的个数
  virtual inline int ExactNumTopBlobs() const { return -1; }
  /**
   * @brief Returns the minimum number of top blobs required by the layer,
   *        or -1 if no minimum number is required.
   *
   * This method should be overridden to return a non-negative value if your
   * layer expects some minimum number of top blobs.
   */
  // 获得layer所需的top blobs的最少个数
  virtual inline int MinTopBlobs() const { return -1; }
  /**
   * @brief Returns the maximum number of top blobs required by the layer,
   *        or -1 if no maximum number is required.
   *
   * This method should be overridden to return a non-negative value if your
   * layer expects some maximum number of top blobs.
   */
  // 获得layer所需的top blobs的最多个数
  virtual inline int MaxTopBlobs() const { return -1; }
  /**
   * @brief Returns true if the layer requires an equal number of bottom and
   *        top blobs.
   *
   * This method should be overridden to return true if your layer expects an
   * equal number of bottom and top blobs.
   */
  // 判断layer所需的bottom blobs和top blobs的个数是否相等
  virtual inline bool EqualNumBottomTopBlobs() const { return false; }

  /**
   * @brief Return whether "anonymous" top blobs are created automatically
   *        by the layer.
   *
   * If this method returns true, Net::Init will create enough "anonymous" top
   * blobs to fulfill the requirement specified by ExactNumTopBlobs() or
   * MinTopBlobs().
   */
  // 判断layer所需的的top blobs是否需要由Net::Init来创建
  virtual inline bool AutoTopBlobs() const { return false; }

  /**
   * @brief Return whether to allow force_backward for a given bottom blob
   *        index.
   *
   * If AllowForceBackward(i) == false, we will ignore the force_backward
   * setting and backpropagate to blob i only if it needs gradient information
   * (as is done when force_backward == false).
   */
  // 判断layer指定的bottom blob是否需要强制梯度返回，因为有些layer其实不需要梯度信息
  virtual inline bool AllowForceBackward(const int bottom_index) const {
    return true;
  }

  /**
   * @brief Specifies whether the layer should compute gradients w.r.t. a
   *        parameter at a particular index given by param_id.
   *
   * You can safely ignore false values and always compute gradients
   * for all parameters, but possibly with wasteful computation.
   */
  // 判断layer指定的blob是否应该计算梯度
  inline bool param_propagate_down(const int param_id) {
    return (param_propagate_down_.size() > param_id) ?
        param_propagate_down_[param_id] : false;
  }
  /**
   * @brief Sets whether the layer should compute gradients w.r.t. a
   *        parameter at a particular index given by param_id.
   */
  // 设置layer指定的blob是否应该计算梯度
  inline void set_param_propagate_down(const int param_id, const bool value) {
    if (param_propagate_down_.size() <= param_id) {
      param_propagate_down_.resize(param_id + 1, true);
    }
    param_propagate_down_[param_id] = value;
  }

  inline Phase phase() { return phase_; }

  /**
   * @brief set phase
   *        enable train and test with one network, for saving memory
  */
  //设置当前网络为train或者是test
  virtual inline void set_phase(Phase phase) {
    phase_ = phase;
  }


  // Caffe中类的成员变量名都带有后缀"_"，这样就容易区分临时变量和类成员变量
 protected:
  /** The protobuf that stores the layer parameters */
	 // 配置的layer参数，创建layer对象时，通过调用构造函数从上层传入，  
	 // 关于LayerParameter类的具体参数可参考caffe.proto中的message LayerParameter
  LayerParameter layer_param_;
  /** The phase: TRAIN or TEST */
  Phase phase_;  // layer状态：指定参与网络的是train还是test
  /** The vector that stores the learnable parameters as a set of blobs. */
  vector<shared_ptr<Blob<Dtype> > > blobs_;	// 用于存储layer的学习的参数如权值和偏置 
  /** Vector indicating whether to compute the diff of each param blob. */
  vector<bool> param_propagate_down_;// 标志是否为layer指定的blob计算梯度值

  /** The vector that indicates whether each top blob has a non-zero weight in
   *  the objective function. */
  vector<Dtype> loss_;// 标志layer指定的top blob是否有一个非0权值

  /** @brief Using the CPU device, compute the layer output. */
  // CPU实现layer的前向传播，纯虚函数，子类中必须实现
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) = 0;
  /**
   * @brief Using the GPU device, compute the layer output.
   *        Fall back to Forward_cpu() if unavailable.
   */
  // GPU实现layer的前向传播
  virtual void Forward_gpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
    // LOG(WARNING) << "Using CPU code as backup.";
    return Forward_cpu(bottom, top);
  }

  /**
   * @brief Using the CPU device, compute the gradients for any parameters and
   *        for the bottom blobs if propagate_down is true.
   */
  // CPU实现layer的反向传播，纯虚函数，子类中必须实现
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down,
      const vector<Blob<Dtype>*>& bottom) = 0;
  /**
   * @brief Using the GPU device, compute the gradients for any parameters and
   *        for the bottom blobs if propagate_down is true.
   *        Fall back to Backward_cpu() if unavailable.
   */
  // GPU实现layer的反向传播
  virtual void Backward_gpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down,
      const vector<Blob<Dtype>*>& bottom) {
    // LOG(WARNING) << "Using CPU code as backup.";
    Backward_cpu(top, propagate_down, bottom);
  }

  /**
   * Called by the parent Layer's SetUp to check that the number of bottom
   * and top Blobs provided as input match the expected numbers specified by
   * the {ExactNum,Min,Max}{Bottom,Top}Blobs() functions.
   */
  // 检查bottom 和top blobs个数是否匹配
  virtual void CheckBlobCounts(const vector<Blob<Dtype>*>& bottom,
                               const vector<Blob<Dtype>*>& top) {
    if (ExactNumBottomBlobs() >= 0) {
      CHECK_EQ(ExactNumBottomBlobs(), bottom.size())
          << type() << " Layer takes " << ExactNumBottomBlobs()
          << " bottom blob(s) as input.";
    }
    if (MinBottomBlobs() >= 0) {
      CHECK_LE(MinBottomBlobs(), bottom.size())
          << type() << " Layer takes at least " << MinBottomBlobs()
          << " bottom blob(s) as input.";
    }
    if (MaxBottomBlobs() >= 0) {
      CHECK_GE(MaxBottomBlobs(), bottom.size())
          << type() << " Layer takes at most " << MaxBottomBlobs()
          << " bottom blob(s) as input.";
    }
    if (ExactNumTopBlobs() >= 0) {
      CHECK_EQ(ExactNumTopBlobs(), top.size())
          << type() << " Layer produces " << ExactNumTopBlobs()
          << " top blob(s) as output.";
    }
    if (MinTopBlobs() >= 0) {
      CHECK_LE(MinTopBlobs(), top.size())
          << type() << " Layer produces at least " << MinTopBlobs()
          << " top blob(s) as output.";
    }
    if (MaxTopBlobs() >= 0) {
      CHECK_GE(MaxTopBlobs(), top.size())
          << type() << " Layer produces at most " << MaxTopBlobs()
          << " top blob(s) as output.";
    }
    if (EqualNumBottomTopBlobs()) {
      CHECK_EQ(bottom.size(), top.size())
          << type() << " Layer produces one top blob as output for each "
          << "bottom blob input.";
    }
  }

  /**
   * Called by SetUp to initialize the weights associated with any top blobs in
   * the loss function. Store non-zero loss weights in the diff blob.
   */
	// 设置top blobs中diff值
  inline void SetLossWeights(const vector<Blob<Dtype>*>& top) {
    const int num_loss_weights = layer_param_.loss_weight_size();
    if (num_loss_weights) {
      CHECK_EQ(top.size(), num_loss_weights) << "loss_weight must be "
          "unspecified or specified once per top blob.";
      for (int top_id = 0; top_id < top.size(); ++top_id) {
        const Dtype loss_weight = layer_param_.loss_weight(top_id);
        if (loss_weight == Dtype(0)) { continue; }
        this->set_loss(top_id, loss_weight);
        const int count = top[top_id]->count();
        Dtype* loss_multiplier = top[top_id]->mutable_cpu_diff();
        caffe_set(count, loss_weight, loss_multiplier);
      }
    }
  }

 private:
  /** Whether this layer is actually shared by other nets*/
  bool is_shared_;//标志当前layer是否被其它net共享

  /** The mutex for sequential forward if this layer is shared */
  shared_ptr<boost::mutex> forward_mutex_;   // 声明boost::mutex对象,互斥锁变量

  /** Initialize forward_mutex_ */
  void InitMutex();  // 初始化互斥锁 
  /** Lock forward_mutex_ if this layer is shared */
  void Lock();   // 如果layer是共享的则加锁
  /** Unlock forward_mutex_ if this layer is shared */
  void Unlock();  // 如果layer是共享的则解锁

  DISABLE_COPY_AND_ASSIGN(Layer);// 禁止使用Layer类的拷贝和赋值操作
};  // class Layer

// Forward and backward wrappers. You should implement the cpu and
// gpu specific implementations instead, and should not change these
// functions.

// 前向传播，通过输入bottom blobs，计算输出top blobs和loss值 
template <typename Dtype>
inline Dtype Layer<Dtype>::Forward(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  // Lock during forward to ensure sequential forward
  Lock();
  Dtype loss = 0;
  Reshape(bottom, top);
  switch (Caffe::mode()) {
  case Caffe::CPU:
    Forward_cpu(bottom, top);
    for (int top_id = 0; top_id < top.size(); ++top_id) {
      if (!this->loss(top_id)) { continue; }
      const int count = top[top_id]->count();
      const Dtype* data = top[top_id]->cpu_data();
      const Dtype* loss_weights = top[top_id]->cpu_diff();
      loss += caffe_cpu_dot(count, data, loss_weights);
    }
    break;
  case Caffe::GPU:
    Forward_gpu(bottom, top);
#ifndef CPU_ONLY
    for (int top_id = 0; top_id < top.size(); ++top_id) {
      if (!this->loss(top_id)) { continue; }
      const int count = top[top_id]->count();
      const Dtype* data = top[top_id]->gpu_data();
      const Dtype* loss_weights = top[top_id]->gpu_diff();
      Dtype blob_loss = 0;
      caffe_gpu_dot(count, data, loss_weights, &blob_loss);
      loss += blob_loss;
    }
#endif
    break;
  default:
    LOG(FATAL) << "Unknown caffe mode.";
  }
  Unlock();
  return loss;
}

// 反向传播，通过给定top blob误差梯度，计算bottom blob误差梯度
template <typename Dtype>
inline void Layer<Dtype>::Backward(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down,
    const vector<Blob<Dtype>*>& bottom) {
  switch (Caffe::mode()) {
  case Caffe::CPU:
    Backward_cpu(top, propagate_down, bottom);
    break;
  case Caffe::GPU:
    Backward_gpu(top, propagate_down, bottom);
    break;
  default:
    LOG(FATAL) << "Unknown caffe mode.";
  }
}

// Serialize LayerParameter to protocol buffer
// 序列化函数，将layer参数写入protobuf文件

template <typename Dtype>
void Layer<Dtype>::ToProto(LayerParameter* param, bool write_diff) {
  param->Clear();
  param->CopyFrom(layer_param_);
  param->clear_blobs();
  for (int i = 0; i < blobs_.size(); ++i) {
    blobs_[i]->ToProto(param->add_blobs(), write_diff);
  }
}

}  // namespace caffe

#endif  // CAFFE_LAYER_H_
