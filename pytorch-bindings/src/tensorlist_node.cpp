#include "tensorlist_node.h"

#include <torch/csrc/autograd/functions/accumulate_grad.h>

torch::autograd::variable_list TensorlistFunction::apply(
    torch::autograd::variable_list args,
    std::function<torch::autograd::variable_list(TensorlistAutogradContext*, torch::autograd::variable_list)> forward,
    std::function<torch::autograd::variable_list(TensorlistAutogradContext*, torch::autograd::variable_list)> backward)
{

    std::shared_ptr<TensorlistNode> node(new TensorlistNode(), torch::autograd::deleteNode);
    node->backward_ = backward;

    const size_t num_inputs = args.size();

    node->is_variable_input_.reserve(num_inputs);

    for (size_t i = 0; i < num_inputs; i++)
    {
        node->is_variable_input_.push_back(true);
    }

    bool is_executable = torch::autograd::GradMode::is_enabled() && torch::autograd::any_variable_requires_grad(args);

    auto next_edges = torch::autograd::collect_next_edges(args);

    node->set_ctx_grad_fn(node);
    node->set_next_edges(std::move(next_edges));
    node->clear_input_metadata();

    node->input_info_.reserve(num_inputs);
    for (auto& var : args)
    {
        node->input_info_.emplace_back(var);
    }

    torch::autograd::variable_list outputs;
    {
        torch::autograd::AutoGradMode grad_mode(false);
        outputs = forward(&node->ctx_, args);
    }

    torch::autograd::_jvp_fn_t jvp_fn = [](torch::autograd::variable_list inputs, torch::autograd::variable_list gI) -> torch::autograd::variable_list {
        TORCH_CHECK(false, "jvp is not implemented for the c++ API of custom Function yet.",
            "Please open a feature request on Github if you need this.");
    };

    auto wrapped_outputs = _wrap_outputs(
        args,
        node->ctx_.get_non_differentiable(),
        node->ctx_.get_dirty(),
        torch::autograd::to_optional(outputs),
        is_executable ? node : nullptr,
        jvp_fn);
    
    node->output_info_.reserve(wrapped_outputs.size());
    for (auto& output : wrapped_outputs)
    {
        if (is_executable && output.has_value()) {
            node->output_info_.emplace_back(output.value());
        }
        else if (is_executable) {
            node->output_info_.emplace_back();
        }
    }

    if (is_executable)
    {
        node->save_variables_to_ctx();
    }

    // wrapped_outputs will be a variable_list so, convert it to the correct
    // return type. Only Variable and variable_list are accepted as return types.
    return torch::autograd::to_output_type<torch::autograd::variable_list>(wrapped_outputs);
}

// The logic here is the same as PyNode::apply, so changes to it should be done
// in both the places
torch::autograd::variable_list TensorlistNode::apply(torch::autograd::variable_list&& inputs)
{
    at::OptionalDeviceGuard _device_guard;

    int num_inputs = inputs.size();
    torch::autograd::variable_list backward_inputs;
    backward_inputs.reserve(num_inputs);
    for (int i = 0; i < num_inputs; ++i)
    {
        if (inputs[i].defined())
        {
            backward_inputs.emplace_back(inputs[i]);
        }
        else
        {
            backward_inputs.emplace_back(output_info_[i].zeros(_device_guard));
        }
    }

    auto outputs = this->backward_(&ctx_, backward_inputs);

    int num_forward_inputs = is_variable_input_.size();
    int num_outputs = outputs.size();
    // Returning too many results is ok, but only as long as they're all undefined.
    // Truncate the result vector in that case.
    if (num_outputs > num_forward_inputs)
    {
        bool all_undef = true;
        for (int i = num_forward_inputs; i < num_outputs; ++i)
        {
            all_undef &= (!outputs[i].defined());
        }
        if (all_undef)
        {
            outputs.resize(num_forward_inputs);
            num_outputs = num_forward_inputs;
        }
    }

    if (num_outputs != num_forward_inputs)
    {
        std::string msg("function ");
        msg += name() + " returned an incorrect number of gradients (expected ";
        msg += c10::to_string(num_forward_inputs) + ", got ";
        msg += c10::to_string(num_outputs) + ")";
        throw std::runtime_error(msg);
    }

    torch::autograd::variable_list results;
    results.reserve(num_outputs);
    for (int i = 0; i < num_outputs; ++i)
    {
        if (!is_variable_input_[i])
        {
            if (outputs[i].defined())
            {
                std::string msg("function ");
                msg += name() + " returned a gradient different that is defined at position ";
                msg += c10::to_string(i + 1) + ", but the corresponding forward input was not a Variable";
                throw std::runtime_error(msg);
            }
            continue;
        }
        if (!outputs[i].defined())
        {
            auto& info = input_info_[results.size()];
            if (info.requires_grad)
            {
                results.emplace_back(info.zeros(_device_guard));
            }
            else
            {
                results.emplace_back();
            }
        }
        else
        {
            results.emplace_back(outputs[i]);
        }
    }
    return results;
}

void TensorlistNode::release_variables()
{
    ctx_.saved_variables_.clear();
    ctx_.has_freed_buffers_ = true;
}

void TensorlistNode::save_variables_to_ctx()
{
    ctx_.save_variables();
}

void TensorlistNode::set_ctx_grad_fn(const std::shared_ptr<Node>& node)
{
    ctx_.grad_fn_ = node;
}

void TensorlistAutogradContext::save_for_backward(torch::autograd::variable_list to_save)
{
    to_save_ = std::move(to_save);
}

// The logic for handling saved variables here is the same as python_function.cpp
// See _save_variables() and unpack_saved_variables()
void TensorlistAutogradContext::save_variables()
{
    saved_variables_.clear();
    auto ptr = grad_fn_.lock();

    for (const auto& var : to_save_)
    {
        // Allow empty variables to be saved
        if (var.defined())
        {
            bool is_output = var.grad_fn().get() == ptr.get();
            saved_variables_.emplace_back(var, is_output);
        }
        else
        {
            saved_variables_.emplace_back();
        }
    }
    to_save_.clear();
}

torch::autograd::variable_list TensorlistAutogradContext::get_saved_variables() const
{
    TORCH_CHECK(!has_freed_buffers_, torch::autograd::ERR_BACKWARD_TWICE);
    torch::autograd::variable_list saved;
    saved.reserve(saved_variables_.size());
    auto ptr = grad_fn_.lock();
    TORCH_INTERNAL_ASSERT(ptr);
    for (auto& var : saved_variables_)
    {
        saved.push_back(var.unpack(ptr));
    }
    return saved;
}

void TensorlistAutogradContext::mark_dirty(const torch::autograd::variable_list& inputs)
{
    dirty_inputs_.clear();
    dirty_inputs_.reserve(inputs.size());
    for (auto& var : inputs)
    {
        dirty_inputs_.insert(var.unsafeGetTensorImpl());
    }
}

void TensorlistAutogradContext::mark_non_differentiable(const torch::autograd::variable_list& outputs)
{
    non_differentiable_.clear();
    non_differentiable_.reserve(outputs.size());
    for (auto& var : outputs)
    {
        non_differentiable_.insert(var.unsafeGetTensorImpl());
    }
}

const std::unordered_set<at::TensorImpl*>& TensorlistAutogradContext::get_dirty() const
{
    return dirty_inputs_;
}

const std::unordered_set<at::TensorImpl*>& TensorlistAutogradContext::get_non_differentiable() const
{
    return non_differentiable_;
}