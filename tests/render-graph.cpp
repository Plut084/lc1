#include "lc1/vk/render/render-graph.hpp"

#include "lc1/vk/resources/command-buffer.hpp"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void check(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string{message});
}

void execute(lc1::RenderGraph const &graph)
{
    // Callbacks below only record CPU observations. The scheduler forwards this
    // object without making Vulkan calls, so no device or command pool is needed.
    lc1::CommandBuffer commands{vk::raii::CommandBuffer{nullptr}};
    graph.execute(commands);
}

void compile(lc1::RenderGraph &graph)
{
    auto const result = graph.compile();
    if (!result)
        throw std::runtime_error(result.error());
}

void rejects_execution(lc1::RenderGraph const &graph)
{
    bool rejected = false;
    try {
        execute(graph);
    }
    catch (lc1::Error const &error) {
        rejected = true;
        check(std::string_view{error.what()}.contains("compile"),
              "execution diagnostics must explain the required compilation");
    }
    check(rejected, "an uncompiled or invalidated graph must not execute");
}

void rejects_compilation(lc1::RenderGraph &graph,
                         std::initializer_list<std::string_view> diagnostic_parts)
{
    auto const result = graph.compile();
    check(!result, "invalid graph unexpectedly compiled");
    for (auto const part : diagnostic_parts)
        check(result.error().contains(part), result.error());
    rejects_execution(graph);
}

void empty_graph_and_command_reference()
{
    lc1::RenderGraph graph;
    rejects_execution(graph);
    compile(graph);
    execute(graph);
    compile(graph);
    execute(graph);

    lc1::CommandBuffer commands{vk::raii::CommandBuffer{nullptr}};
    int recordings = 0;
    graph.add_pass("record", {}, {}, [&](lc1::CommandBuffer &received) {
        check(&received == &commands, "the graph must forward the supplied command buffer");
        ++recordings;
    });
    rejects_execution(graph);
    compile(graph);
    check(recordings == 0, "compilation must not invoke recorders");
    graph.execute(commands);
    check(recordings == 1, "execution must synchronously record each pass once");
}

void dependency_order()
{
    lc1::RenderGraph graph;
    std::vector<std::string> recorded;
    auto recorder = [&](std::string name) {
        return [&, name = std::move(name)](lc1::CommandBuffer &) { recorded.push_back(name); };
    };
    // Register consumers before producers, with a diamond and repeated references
    // to one producer. A newly ready earlier pass precedes later independent work.
    graph.add_pass("combine", {"left", "right"}, {"display"}, recorder("combine"));
    graph.add_pass("left", {"depth", "normal", "depth"}, {"left"}, recorder("left"));
    graph.add_pass("right", {"depth"}, {"right"}, recorder("right"));
    graph.add_pass("surface", {}, {"depth", "normal"}, recorder("surface"));
    graph.add_pass("independent", {}, {}, recorder("independent"));
    std::vector<std::string> const expected{"surface", "left", "right", "combine", "independent"};

    compile(graph);
    check(recorded.empty(), "compilation must only schedule work");
    execute(graph);
    check(recorded == expected, "dependencies and declaration priority determine pass order");
    recorded.clear();
    execute(graph);
    check(recorded == expected, "a compiled graph must be reusable without consuming its order");
    recorded.clear();
    compile(graph);
    execute(graph);
    check(recorded == expected, "recompilation must not append duplicate scheduled passes");
}

void imported_resource_versions()
{
    lc1::RenderGraph graph;
    graph.import_resource("history.previous");
    graph.import_resource("history.previous");
    graph.import_resource("unused.external");
    int const previous = 5;
    int current = 0;
    int sampled = 0;
    graph.add_pass("sample", {"history.current", "history.previous"}, {},
                   [&](lc1::CommandBuffer &) { sampled = previous + current; });
    graph.add_pass("update", {"history.previous", "history.previous"}, {"history.current"},
                   [&](lc1::CommandBuffer &) { current = previous + 1; });
    compile(graph);
    execute(graph);
    check(current == 6 && sampled == 11,
          "imports supply external values while new versions keep their producer dependency");
}

void invalid_resource_dependencies()
{
    int recordings = 0;
    auto record = [&](lc1::CommandBuffer &) { ++recordings; };
    {
        lc1::RenderGraph graph;
        graph.add_pass("lighting", {"absent-depth"}, {"hdr"}, record);
        rejects_compilation(graph, {"lighting", "absent-depth", "import"});
    }
    {
        lc1::RenderGraph graph;
        graph.add_pass("first-writer", {}, {"hdr"}, record);
        graph.add_pass("second-writer", {}, {"hdr"}, record);
        rejects_compilation(graph, {"first-writer", "second-writer", "hdr", "version"});
    }
    {
        lc1::RenderGraph graph;
        graph.add_pass("repeated-output", {}, {"hdr", "hdr"}, record);
        rejects_compilation(graph, {"repeated-output", "hdr", "once"});
    }
    {
        lc1::RenderGraph graph;
        graph.import_resource("history");
        graph.add_pass("overwrite", {}, {"history"}, record);
        rejects_compilation(graph, {"overwrite", "history", "imported", "version"});
    }
    {
        lc1::RenderGraph graph;
        graph.add_pass("self-dependent", {"history"}, {"history"}, record);
        rejects_compilation(graph, {"self-dependent", "history", "reads and writes", "version"});
    }
    {
        lc1::RenderGraph graph;
        graph.add_pass("independent", {}, {"ready"}, record);
        graph.add_pass("cycle-first", {"cycle-b"}, {"cycle-a"}, record);
        graph.add_pass("cycle-second", {"cycle-a"}, {"cycle-b"}, record);
        graph.add_pass("blocked-consumer", {"cycle-a", "ready"}, {}, record);
        rejects_compilation(graph, {"cycle", "cycle-first", "cycle-second", "cycle-a", "cycle-b"});
        rejects_compilation(graph, {"cycle"});
    }
    check(recordings == 0, "failed compilation must never leave executable partial work");
}

void invalid_pass_definitions()
{
    auto record = [](lc1::CommandBuffer &) {};
    {
        lc1::RenderGraph graph;
        graph.add_pass("duplicate", {}, {}, record);
        graph.add_pass("duplicate", {}, {}, record);
        rejects_compilation(graph, {"duplicate", "pass name"});
    }
    {
        lc1::RenderGraph graph;
        graph.add_pass("missing-recorder", {}, {}, {});
        rejects_compilation(graph, {"missing-recorder", "callback"});
    }
    {
        lc1::RenderGraph graph;
        graph.add_pass("", {}, {}, record);
        rejects_compilation(graph, {"pass", "no name"});
    }
    {
        lc1::RenderGraph graph;
        graph.import_resource("");
        rejects_compilation(graph, {"imports", "empty resource name"});
    }
    {
        lc1::RenderGraph graph;
        graph.add_pass("empty-input", {""}, {}, record);
        rejects_compilation(graph, {"empty-input", "empty input"});
    }
    {
        lc1::RenderGraph graph;
        graph.add_pass("empty-output", {}, {""}, record);
        rejects_compilation(graph, {"empty-output", "empty output"});
    }
}

void compilation_lifetime_and_recovery()
{
    lc1::RenderGraph graph;
    int recordings = 0;
    auto record = [&](lc1::CommandBuffer &) { ++recordings; };
    graph.add_pass("producer", {}, {"current"}, record);
    compile(graph);
    execute(graph);
    check(recordings == 1, "the original graph executes its producer");

    graph.add_pass("consumer", {"current", "external"}, {}, record);
    rejects_execution(graph);
    rejects_compilation(graph, {"consumer", "external"});
    check(recordings == 1, "mutation and failed recompilation invalidate the old schedule");

    graph.import_resource("external");
    rejects_execution(graph);
    compile(graph);
    execute(graph);
    check(recordings == 3, "adding a missing import recovers the graph after a compile failure");

    graph.import_resource("another-external");
    rejects_execution(graph);
    compile(graph);
    execute(graph);
    check(recordings == 5, "imports added after compilation require a fresh schedule");

    graph.add_pass("second-producer", {}, {"current"}, record);
    rejects_compilation(graph, {"producer", "second-producer", "current"});
    check(recordings == 5, "a failed compile must discard a previously successful schedule");
}

} // namespace

int main()
{
    try {
        empty_graph_and_command_reference();
        dependency_order();
        imported_resource_versions();
        invalid_resource_dependencies();
        invalid_pass_definitions();
        compilation_lifetime_and_recovery();
        std::cout << "Render graph ordering, imports, validation and compilation lifetime passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
