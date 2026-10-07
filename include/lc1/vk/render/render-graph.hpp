#pragma once

#include "lc1/vk/resources/command-buffer.hpp"

#include <expected>
#include <functional>
#include <queue>
#include <ranges>
#include <string>
#include <vector>

namespace lc1 {

class RenderGraph {
  public:
    void add_pass(std::string name, std::vector<std::string> inputs,
                  std::vector<std::string> outputs,
                  std::function<void(CommandBuffer &)> record_commands)
    {
        passes_.emplace_back(std::move(name), std::move(inputs), std::move(outputs),
                             std::move(record_commands));
    }

    std::expected<void, std::string> compile()
    {
        std::vector<std::vector<std::size_t>> dependencies(passes_.size());
        std::vector<std::vector<std::size_t>> dependents(passes_.size());

        // We will create phony passes for initial resources that are inputs but not outputs of any
        // pass.
        std::unordered_map<std::string, std::size_t> resource_writer;
        for (auto [pass_idx, pass] : std::views::zip(std::views::iota(0UZ), passes_)) {
            for (auto const &output : pass.outputs) {
                resource_writer[output] = pass_idx;
            }
        }

        for (auto [pass_idx, pass] : std::views::zip(std::views::iota(0UZ), passes_)) {
            for (auto const &input : pass.inputs) {
                auto it = resource_writer.find(input);
                if (it == resource_writer.end()) {
                    return std::unexpected("Input resource '" + input + "' has no producing pass.");
                }
                auto v = it->second; // dependency index
                dependencies[pass_idx].push_back(v);
                dependents[v].push_back(pass_idx);
            }
        }

        // Performs topological sort.
        std::queue<std::size_t> ready;
        std::vector<std::size_t> in_degree(passes_.size());

        for (auto [idx, in, deps] :
             std::views::zip(std::views::iota(0UZ), in_degree, dependencies)) {
            in = deps.size();
            if (in == 0) {
                ready.push(idx);
            }
        }

        while (!ready.empty()) {
            auto idx = ready.front();
            ready.pop();
            pass_order_.push_back(idx);

            for (auto dependent : dependents[idx]) {
                if (--in_degree[dependent] == 0) {
                    ready.push(dependent);
                }
            }
        }
        if (pass_order_.size() != passes_.size()) {
            return std::unexpected("Cyclic dependency detected in render graph.");
        }

        // Create resource semaphores and barriers here if needed.

        return {}; // OK
    }

    void execute(CommandBuffer &commands) const
    {
        for (auto const &index : pass_order_) {
            passes_[index].record_commands(commands);
        }
    }

  private:
    struct Pass {
        std::string name;
        std::vector<std::string> inputs;  // Resources this pass reads from (dependencies)
        std::vector<std::string> outputs; // Resources this pass writes to (products)
        std::function<void(CommandBuffer &)> record_commands;
    };

    std::vector<Pass> passes_;

    // Compiled output
    std::vector<std::size_t> pass_order_;
};

} // namespace lc1
