/*!
 * Copyright (c) 2015 by Contributors
 * \file roi_pooling-inl.h
 * \brief roi pooling operator and symbol
 * \author Kye-Hyeon Kim, Jian Guo
*/
#ifndef MXNET_OPERATOR_ROI_POOLING_INL_H_
#define MXNET_OPERATOR_ROI_POOLING_INL_H_

#include <dmlc/logging.h>
#include <dmlc/parameter.h>
#include <mxnet/operator.h>
#include <map>
#include <vector>
#include <string>
#include <utility>
#include "./mshadow_op.h"
#include "./operator_common.h"


namespace mxnet {
namespace op {

// Declare enumeration of input order to make code more intuitive.
// These enums are only visible within this header
namespace roipool {
enum ROIPoolingOpInputs {kData, kBox};
enum ROIPoolingOpOutputs {kOut, kMaxIdx};
}  // roipool

struct ROIPoolingParam : public dmlc::Parameter<ROIPoolingParam> {
  TShape pooled_size;
  float spatial_scale;
  float pad_ratio;
  DMLC_DECLARE_PARAMETER(ROIPoolingParam) {
    DMLC_DECLARE_FIELD(pooled_size)
    .set_expect_ndim(2).enforce_nonzero()
    .describe("fix pooled size: (h, w)");
    DMLC_DECLARE_FIELD(spatial_scale).set_range(0.0, 1.0)
    .describe("Ratio of input feature map height (or w) to raw image height (or w). "
    "Equals the reciprocal of total stride in convolutional layers");
    DMLC_DECLARE_FIELD(pad_ratio).set_range(0.0, 1.0).set_default(0.0)
    .describe("pad ratio of rois, one-sided");
  }
};

template<typename xpu, typename DType>
class ROIPoolingOp : public Operator {
 public:
  explicit ROIPoolingOp(ROIPoolingParam p) {
    this->param_ = p;
  }

  virtual void Forward(const OpContext &ctx,
                       const std::vector<TBlob> &in_data,
                       const std::vector<OpReqType> &req,
                       const std::vector<TBlob> &out_data,
                       const std::vector<TBlob> &aux_args) {
    using namespace mshadow;
    size_t expected = 2;
    CHECK_EQ(in_data.size(), expected);
    CHECK_EQ(out_data.size(), expected);
    CHECK_EQ(out_data[roipool::kOut].shape_[1], in_data[roipool::kBox].shape_[1]);
    CHECK_EQ(out_data[roipool::kMaxIdx].shape_[1], in_data[roipool::kBox].shape_[1]);
    Stream<xpu> *s = ctx.get_stream<xpu>();

    Tensor<xpu, 4, DType> data = in_data[roipool::kData].get<xpu, 4, DType>(s);
    Tensor<xpu, 3, DType> bbox = in_data[roipool::kBox].get<xpu, 3, DType>(s);
    Tensor<xpu, 5, DType> out = out_data[roipool::kOut].get<xpu, 5, DType>(s);
    Tensor<xpu, 5, DType> max_idx = out_data[roipool::kMaxIdx].get<xpu, 5, DType>(s);
    CHECK_EQ(data.CheckContiguous(), true);
    CHECK_EQ(bbox.CheckContiguous(), true);
    CHECK_EQ(out.CheckContiguous(), true);
    CHECK_EQ(max_idx.CheckContiguous(), true);
    out = -FLT_MAX;
    max_idx = -1.0f;
    ROIPoolForward(out, data, bbox, max_idx, param_.spatial_scale, param_.pad_ratio);
  }

  virtual void Backward(const OpContext &ctx,
                        const std::vector<TBlob> &out_grad,
                        const std::vector<TBlob> &in_data,
                        const std::vector<TBlob> &out_data,
                        const std::vector<OpReqType> &req,
                        const std::vector<TBlob> &in_grad,
                        const std::vector<TBlob> &aux_args) {
    using namespace mshadow;
    size_t expected = 2;
    CHECK_EQ(in_data.size(), expected);
    CHECK_EQ(out_data.size(), expected);
    CHECK_EQ(out_grad[roipool::kOut].shape_[1], in_data[roipool::kBox].shape_[1]);
    CHECK_EQ(out_data[roipool::kMaxIdx].shape_[1], in_data[roipool::kBox].shape_[1]);
    CHECK_NE(req[roipool::kData], kWriteInplace) <<
      "ROIPooling: Backward doesn't support kWriteInplace.";
    CHECK_NE(req[roipool::kBox], kWriteInplace) <<
      "ROIPooling: Backward doesn't support kWriteInplace.";
    Stream<xpu> *s = ctx.get_stream<xpu>();

    Tensor<xpu, 5, DType> grad_out = out_grad[roipool::kOut].get<xpu, 5, DType>(s);
    Tensor<xpu, 3, DType> bbox = in_data[roipool::kBox].get<xpu, 3, DType>(s);
    Tensor<xpu, 5, DType> max_idx = out_data[roipool::kMaxIdx].get<xpu, 5, DType>(s);
    Tensor<xpu, 4, DType> grad_in = in_grad[roipool::kData].get<xpu, 4, DType>(s);
    Tensor<xpu, 3, DType> grad_roi = in_grad[roipool::kBox].get<xpu, 3, DType>(s);
    CHECK_EQ(grad_out.CheckContiguous(), true);
    CHECK_EQ(bbox.CheckContiguous(), true);
    CHECK_EQ(max_idx.CheckContiguous(), true);
    CHECK_EQ(grad_in.CheckContiguous(), true);
    if (kAddTo == req[roipool::kData] || kWriteTo == req[roipool::kData]) {
      if (kWriteTo == req[roipool::kData]) {
        grad_in = 0.0f;
      }
      ROIPoolBackwardAcc(grad_in, grad_out, bbox, max_idx, param_.spatial_scale, param_.pad_ratio);
    }
    if (kWriteTo == req[roipool::kBox]) {
      grad_roi = 0.0f;
    }
  }

 private:
  ROIPoolingParam param_;
};  // class ROIPoolingOp

// Decalre Factory function, used for dispatch specialization
template<typename xpu>
Operator* CreateOp(ROIPoolingParam param, int dtype);

#if DMLC_USE_CXX11
class ROIPoolingProp : public OperatorProperty {
 public:
  std::vector<std::string> ListArguments() const override {
    return {"data", "rois"};
  }

  std::vector<std::string> ListOutputs() const override {
    return {"output", "maxidx"};
  }

  int NumOutputs() const override {
    return 2;
  }

  int NumVisibleOutputs() const override {
    return 1;
  }

  void Init(const std::vector<std::pair<std::string, std::string> >& kwargs) override {
    param_.Init(kwargs);
  }

  std::map<std::string, std::string> GetParams() const override {
    return param_.__DICT__();
  }

  bool InferShape(std::vector<TShape> *in_shape,
                  std::vector<TShape> *out_shape,
                  std::vector<TShape> *aux_shape) const override {
    using namespace mshadow;
    CHECK_EQ(in_shape->size(), 2U) << "Input:[data, rois]";

    // data: [batch_size, c, h, w]
    TShape dshape = in_shape->at(roipool::kData);
    CHECK_EQ(dshape.ndim(), 4U) << "data should be a 4D tensor";

    // bbox: [batch_size, num_rois, 5]
    TShape bshape = in_shape->at(roipool::kBox);
    CHECK_EQ(bshape.ndim(), 3U) << "bbox should be a 2D tensor of shape [batch, num_rois, 5]";
    CHECK_EQ(bshape[2], 5U) << "bbox should be a 2D tensor of shape [batch, num_rois, 5]";

    // out: [batch_size, num_rois, c, pooled_h, pooled_w]
    // max_idx: [batch_size, num_rois, c, pooled_h, pooled_w]
    out_shape->clear();
    out_shape->push_back(
         Shape5(dshape[0], bshape[1], dshape[1], param_.pooled_size[0], param_.pooled_size[1]));
    out_shape->push_back(
         Shape5(dshape[0], bshape[1], dshape[1], param_.pooled_size[0], param_.pooled_size[1]));
    return true;
  }

  bool InferType(std::vector<int> *in_type,
                 std::vector<int> *out_type,
                 std::vector<int> *aux_type) const override {
    CHECK_EQ(in_type->size(), 2U);
    int dtype = (*in_type)[0];
    CHECK_EQ(dtype, (*in_type)[1]);
    CHECK_NE(dtype, -1) << "Input must have specified type";

    out_type->clear();
    out_type->push_back(dtype);
    out_type->push_back(dtype);
    return true;
  }

  OperatorProperty* Copy() const override {
    ROIPoolingProp* roi_pooling_sym = new ROIPoolingProp();
    roi_pooling_sym->param_ = this->param_;
    return roi_pooling_sym;
  }

  std::string TypeString() const override {
    return "ROIPooling";
  }

  // decalre dependency and inplace optimization options
  std::vector<int> DeclareBackwardDependency(
    const std::vector<int> &out_grad,
    const std::vector<int> &in_data,
    const std::vector<int> &out_data) const override {
    return {out_grad[roipool::kOut], in_data[roipool::kBox], out_data[roipool::kMaxIdx]};
  }

  Operator* CreateOperator(Context ctx) const override {
    LOG(FATAL) << "Not Implemented.";
    return NULL;
  }

  Operator* CreateOperatorEx(Context ctx, std::vector<TShape> *in_shape,
                             std::vector<int> *in_type) const override;

 private:
  ROIPoolingParam param_;
};  // class ROIPoolingProp
#endif
}  // namespace op
}  // namespace mxnet
#endif  // MXNET_OPERATOR_ROI_POOLING_INL_H_
