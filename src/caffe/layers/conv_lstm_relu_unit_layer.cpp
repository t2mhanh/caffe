#include <algorithm>
#include <cmath>
#include <vector>

#include "caffe/layer.hpp"
#include "caffe/layers/conv_lstm_relu_layer.hpp"

namespace caffe {

template <typename Dtype>
inline Dtype sigmoid(Dtype x) {
  return 1. / (1. + exp(-x));
}

// template <typename Dtype>
// inline Dtype tanh(Dtype x) {
//   return 2. * sigmoid(2. * x) - 1.;
// }

template <typename Dtype>
void ConvLSTMReLUUnitLayer<Dtype>::Reshape(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {

  // The expected input shape is: 1 x N x C x H x W for inputs
  // 1 x N for sequence markers

  const int num_instances = bottom[0]->shape(1);

  for (int i = 0; i < bottom.size(); ++i) {
    if (i == 2) {
      CHECK_EQ(2, bottom[i]->num_axes());
    } else {
      CHECK_EQ(5, bottom[i]->num_axes());
    }
    CHECK_EQ( num_instances, bottom[i]->shape(1) );
    CHECK_EQ( bottom[i]->shape( 0 ), 1 );
  }

  // Conv. Shape along c_t or h_t
  // Note that x has 4 times as many channels
  hidden_dim_ = 1;
  for( int i = 2; i < bottom[0]->num_axes(); ++i )
    hidden_dim_ *= bottom[0]->shape(i);

  CHECK_EQ(4 * bottom[0]->shape(2), bottom[1]->shape(2));
  top[0]->ReshapeLike(*bottom[0]);
  top[1]->ReshapeLike(*bottom[0]);
  X_acts_.ReshapeLike(*bottom[1]);
}

template <typename Dtype>
void ConvLSTMReLUUnitLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {

  // N
  const int num = bottom[0]->shape(1);
  const int x_dim = hidden_dim_ * 4;
  const Dtype* C_prev = bottom[0]->cpu_data();
  const Dtype* X = bottom[1]->cpu_data();
  const Dtype* cont = bottom[2]->cpu_data();
  Dtype* C = top[0]->mutable_cpu_data();
  Dtype* H = top[1]->mutable_cpu_data();

  for (int n = 0; n < num; ++n) {
    for (int d = 0; d < hidden_dim_; ++d) {
      const Dtype i = sigmoid(X[d]);
      const Dtype f = (*cont == 0) ? 0 :
          (*cont * sigmoid(X[1 * hidden_dim_ + d]));
      const Dtype o = sigmoid(X[2 * hidden_dim_ + d]);
      const Dtype g = std::max(X[3 * hidden_dim_ + d], Dtype(0));//tanh(X[3 * hidden_dim_ + d]);
      const Dtype c_prev = C_prev[d];
      const Dtype c = f * c_prev + i * g;
      C[d] = c;
      const Dtype relu_c = std::max(c,Dtype(0));
      H[d] = o * relu_c;
    }
    C_prev += hidden_dim_;
    X += x_dim;
    C += hidden_dim_;
    H += hidden_dim_;
    ++cont;
  }
}

template <typename Dtype>
void ConvLSTMReLUUnitLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {

  CHECK(!propagate_down[2]) << "Cannot backpropagate to sequence indicators.";

  if (!propagate_down[0] && !propagate_down[1])
    return;

  const int num = bottom[0]->shape(1);

  const int x_dim = hidden_dim_ * 4;

  const Dtype* C_prev = bottom[0]->cpu_data();
  const Dtype* X = bottom[1]->cpu_data();
  const Dtype* cont = bottom[2]->cpu_data();

  const Dtype* C = top[0]->cpu_data();
  const Dtype* H = top[1]->cpu_data();

  const Dtype* C_diff = top[0]->cpu_diff();
  const Dtype* H_diff = top[1]->cpu_diff();

  Dtype* C_prev_diff = bottom[0]->mutable_cpu_diff();
  Dtype* X_diff = bottom[1]->mutable_cpu_diff();

  for (int n = 0; n < num; ++n) {
    for (int d = 0; d < hidden_dim_; ++d) {
      const Dtype i = sigmoid(X[d]);
      const Dtype f = (*cont == 0) ? 0 :
          (*cont * sigmoid(X[1 * hidden_dim_ + d]));
      const Dtype o = sigmoid(X[2 * hidden_dim_ + d]);
      const Dtype g = std::max(X[3 * hidden_dim_ + d], Dtype(0));//anh(X[3 * hidden_dim_ + d]);
      const Dtype c_prev = C_prev[d];
      const Dtype c = C[d];
      const Dtype relu_c = std::max(c,Dtype(0));//tanh(c);
      Dtype* c_prev_diff = C_prev_diff + d;

      Dtype* i_diff = X_diff + d;
      Dtype* f_diff = X_diff + 1 * hidden_dim_ + d;
      Dtype* o_diff = X_diff + 2 * hidden_dim_ + d;
      Dtype* g_diff = X_diff + 3 * hidden_dim_ + d;

      const Dtype c_term_diff =
          C_diff[d] + H_diff[d] * o * (c > 0);

      *c_prev_diff = c_term_diff * f;
      *i_diff = c_term_diff * g * i * (1 - i);
      *f_diff = c_term_diff * c_prev * f * (1 - f);
      *o_diff = H_diff[d] * relu_c * o * (1 - o);
      *g_diff = c_term_diff * i * (g > 0);
    }
    C_prev += hidden_dim_;
    X += x_dim;

    C += hidden_dim_;
    H += hidden_dim_;

    C_diff += hidden_dim_;
    H_diff += hidden_dim_;
    X_diff += x_dim;
    C_prev_diff += hidden_dim_;
    ++cont;
  }
}

#ifdef CPU_ONLY
STUB_GPU(ConvLSTMReLUUnitLayer);
#endif

INSTANTIATE_CLASS(ConvLSTMReLUUnitLayer);
REGISTER_LAYER_CLASS(ConvLSTMReLUUnit);

}  // namespace caffe
