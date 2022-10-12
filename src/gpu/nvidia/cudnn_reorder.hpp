/*******************************************************************************
* Copyright 2020-2022 Intel Corporation
* Copyright 2020 Codeplay Software Limited
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef GPU_NVIDIA_CUDNN_REORDER_HPP
#define GPU_NVIDIA_CUDNN_REORDER_HPP

#include "common/memory_desc_wrapper.hpp"
#include "common/primitive.hpp"
#include "gpu/gpu_reorder_pd.hpp"
#include "gpu/nvidia/cudnn_reorder_impl.hpp"
#include "gpu/nvidia/sycl_cuda_engine.hpp"
#include "gpu/nvidia/sycl_cuda_utils.hpp"

namespace dnnl {
namespace impl {
namespace gpu {
namespace nvidia {

struct cudnn_reorder_t : public primitive_t {
    using primitive_t::primitive_t;

    struct pd_t : public gpu_reorder_pd_t {
        using gpu_reorder_pd_t::gpu_reorder_pd_t;

        DECLARE_COMMON_PD_T("cuda:cudnn:any", cudnn_reorder_t);

        // Function to verify data and memory format
        bool valid_data_n_mem_format() const {
            bool ok = utils::one_of(src_md()->data_type, data_type::s8,
                              data_type::f16, data_type::f32)
                    && utils::one_of(dst_md()->data_type, data_type::s8,
                            data_type::f16, data_type::f32);

            // Nvidia only supports blocking for Int8
            if (!utils::one_of(src_md()->data_type, data_type::s8)
                    && src_md()->format_desc.blocking.inner_nblks > 0)
                return false;
            if (!utils::one_of(dst_md()->data_type, data_type::s8)
                    && dst_md()->format_desc.blocking.inner_nblks > 0)
                return false;

            // Nvidia supports blocking only on channel dimension C
            if (dst_md()->format_desc.blocking.inner_nblks > 1
                    || src_md()->format_desc.blocking.inner_nblks > 1)
                return false;
            if (utils::one_of(src_md()->data_type, data_type::s8)
                    && src_md()->format_desc.blocking.inner_nblks == 1) {
                ok = ok && memory_desc_matches_nchw_vect_c(src_md());
            }
            int blks = dst_md()->format_desc.blocking.inner_nblks;
            if (utils::one_of(dst_md()->data_type, data_type::s8)
                    && blks == 1) {
                ok = ok && memory_desc_matches_nchw_vect_c(dst_md());
            }
            return ok;
        }

        bool check_scales_mask() const {
            // cuDNN does not support scaling per dimension.
            if (attr()->output_scales_.mask_ != 0) { return false; }
            return true;
        }

        status_t init(
                engine_t *engine, engine_t *src_engine, engine_t *dst_engine) {
            const auto attr_skip_mask = primitive_attr_t::skip_mask_t::oscale
                    | primitive_attr_t::skip_mask_t::post_ops;
            bool ok = engine == dst_engine
                    && src_engine->kind() == engine_kind::gpu
                    && valid_data_n_mem_format()
                    && attr()->has_default_values(attr_skip_mask)
                    && check_scales_mask();
            if (!ok) return status::unimplemented;

            if (has_different_block_size(src_md(), dst_md())) {
                reorder_.reset(new cudnn_reorder_ex_t());
            } else {
                reorder_.reset(new cudnn_reorder_stride_t());
            }

            return reorder_->init(this);
        }
        std::shared_ptr<cudnn_reorder_generic_t> reorder_;

    private:
        DECLARE_GPU_REORDER_CREATE();
    };

    status_t init(engine_t *engine) override {
        alpha_ = new float;
        if (!alpha_) return status::out_of_memory;
        *alpha_ = 1.0f;
        return status::success;
    }

    status_t execute(const exec_ctx_t &ctx) const override;

private:
    const pd_t *pd() const { return (const pd_t *)primitive_t::pd().get(); }

    float *alpha_ = nullptr;
};

} // namespace nvidia
} // namespace gpu
} // namespace impl
} // namespace dnnl

#endif
