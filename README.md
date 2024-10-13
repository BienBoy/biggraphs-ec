# 作业5：使用 OpenMP 处理大规模图

**截止日期：12 月 8 日（周五）, 11:59PM PT（不允许迟交）**

**总分：84 分**

如果你完成这个作业，你将在一个常规编程作业（PA1-PA4）中获得最多 10 分的加分。注意，编程作业的平均分没有上限，所以这基本上是课程中的“额外奖励”。

## 概述

在这个作业中，你将实现[广度优先搜索](https://en.wikipedia.org/wiki/Breadth-first_search) （BFS）。一个好的实现在多核机器上能够仅用几秒钟的时间在包含数亿条边的图上运行此算法。

## 环境设置

最终评分将在具有 32 个 vCPU 的机器上进行，这些机器将在 AWS 云平台上运行。不过，你可以在 Myth 集群中的 4 核（8 个超线程）机器上开始这个作业。这些机器足以进行基本的开发和性能测试。不过，**在提交之前请在 AWS 上测试你的代码**。你可以在 PA4 用过的共享集群上测试你的代码。或者，你可以设置自己的 EC2 集群（[说明](./cloud_readme.md)），但这种选择需要一些预算。

作业的起始代码可以在 [Github](https://github.com/stanford-cs149/biggraphs-ec) 上获取。请使用以下命令克隆作业 5 的起始代码：

```
git clone https://github.com/stanford-cs149/biggraphs-ec.git
```

**不要在共享集群上存储任何未加密的私有 SSH 密钥或其他敏感信息。** 课程工作人员不对共享集群提供任何信息安全保证。

### 背景：学习 OpenMP

在这个作业中，我们希望你使用 [OpenMP](http://openmp.org/) 进行多核并行化。OpenMP 是一个 API 和一组 C 语言扩展，为并行性提供编译器支持。你也可以使用 OpenMP 告诉编译器并行化 `for` 循环，并管理互斥。网上有很多关于 OpenMP 的详细文档，这里是一个带有互斥的 `for` 循环并行化的简短示例。

```c
/* 这个 for 循环可以被编译器并行化 */
#pragma omp parallel for
for (int i = 0; i < 100; i++) {  

    /* 这个部分的不同迭代可以在不同的核上并行运行 */

    #pragma omp critical
    {
    /* 这个块同一时间最多由一个线程执行。 */
    printf("Thread %d got iteration %lu\n", omp_get_thread_num(), i);           
    }                                                                             
}
```

请参阅 OpenMP 文档，了解如何使用不同形式的静态或动态调度的语法。（例如，`omp parallel for schedule(dynamic 100)`使用动态调度将 100 次迭代为一个块分配给线程）。你可以将实现想象为一个动态工作队列，线程池中的线程一次处理 100 次迭代，就像我们在[这些讲义](https://gfxcourses.stanford.edu/cs149/fall23/lecture/perfopt1/slide_11)中讨论的。

下面是一个在 OpenMP 中进行原子计数器更新的示例。

```c
int my_counter = 0;
#pragma omp parallel for                                                        
for (int i = 0; i < 100; i++) {                                                      
    if (... some condition ...) {
        #pragma omp atomic
        my_counter++;
    }
}
```

我们希望你能够自己阅读 OpenMP 文档（Google 会非常有帮助），但这里有一些有用的链接供你入门：

 - OpenMP 3.0 规范:：<http://www.openmp.org/mp-documents/spec30.pdf>。
 - OpenMP 速查表：<http://openmp.org/mp-documents/OpenMP3.1-CCard.pdf>。
 - OpenMP 支持对共享变量的归约操作，以及声明线程本地变量的副本。
 - 这是一个关于 `omp parallel_for` 指令的不错指南: <http://www.inf.ufsc.br/~bosco.sobral/ensino/ine5645/OpenMP_Dynamic_Scheduling.pdf>

### 背景：图的表示

起始代码操作的是有向图，其实现可以在 `graph.h` 和 `graph_internal.h` 中找到。我们建议你首先了解这些文件中的图的表示方法。图由一个边数组表示（包括 `outgoing_edges` 和 `incoming_edges`），每条边由一个整数表示目标顶点的 ID。边按照源顶点排序存储，因此源顶点在表示中是隐含的。这种表示方法不仅紧凑，还允许图在内存中连续存储。例如，要迭代图中所有节点的出边，可以使用以下代码，该代码利用了 `graph.h` 中定义（并 `graph_internal.h` 中实现）的方便辅助函数：

```c
for (int i = 0; i < num_nodes(g); i++) {
    // Vertex 被定义为一个整数。Vertex* 指向 g.outgoing_edges[] 中
    const Vertex* start = outgoing_begin(g, i);
    const Vertex* end = outgoing_end(g, i);
    for (const Vertex* v = start; v != end; v++)
        printf("Edge %u %u\n", i, *v);
}
```

### 数据集

在这个项目中，你将使用一个大型图数据集来测试性能。根据你的设置，可以找到数据集的位置如下：

- 如果你在 myth 机器上工作，图目录的路径是`/afs/ir.stanford.edu/class/cs149/data/asst3_graphs/`
- 如果你在共享集群上工作，图目录的路径是`/opt/cs149_graphs`
- 如果你在自己的 EC2 实例或本地机器上工作，可以从 <http://cs149.stanford.edu/cs149asstdata/all_graphs.tgz> 下载数据集。你可以使用 `wget http://cs149.stanford.edu/cs149asstdata/all_graphs.tgz`下载数据集，然后使用 `tar -xzvf all_graphs.tgz` 解压。请注意，这是一个 3 GB 的下载。

一些有趣的真实世界图包括：

 - com-orkut_117m.graph 
 - oc-pokec_30m.graph
 - soc-livejournal1_68m.graph

一些有用但很大的合成图包括：

 - random_500m.graph
 - rmat_200m.graph

还有一些非常小的图用于测试。如果你查看起始代码的 `/tools` 目录，你会注意到一个名为 `graphTools.cpp` 的有用程序，它也可以用来制作你自己的图。

## 第 1 部分：并行“自顶向下”广度优先搜索（20 分）

广度优先搜索（BFS）是一种常见的算法，可能在之前的算法课程中已经见过（参考 [这里](https://www.hackerearth.com/practice/algorithms/graphs/breadth-first-search/tutorial/) 和 [这里](https://www.youtube.com/watch?v=oDqjPvD54Ss) 获取有用的资料）。请熟悉 `bfs/bfs.cpp` 中的 `bfs_top_down()` 函数，该函数包含 BFS 的串行实现。该代码使用 BFS 计算图中所有顶点到顶点 0 的距离。你可能需要熟悉 `common/graph.h` 中定义的图结构以及简单的数组数据结构 `vertex_set`（`bfs/bfs.h`），`vertex_set` 是一个顶点数组，用于表示 BFS 当前边界。

你可以使用以下命令运行 bfs：

```sh
./bfs <PATH_TO_GRAPHS_DIRECTORY>/rmat_200m.graph
```

其中 `<PATH_TO_GRAPHS_DIRECTORY>` 是包含图文件的目录路径（见上面的“数据集”部分）。

当你运行 `bfs` 时，你会看到每个算法步骤的执行时间和边界大小。由于我们给出了一个正确的串行实现，自顶向下版本的正确性是有保证的，但它会很慢。（注意 `bfs` 会报告“自底向上”和“混合”版本算法失败，这些版本你将在后续的作业中实现。）

在本部分作业中，你的任务是并行化自顶向下的广度优先搜索（BFS）。你需要重点识别可以并行执行的工作，并插入适当的同步以确保正确性。我们要提醒你，不应该期望在这个问题上实现接近完美的加速比（我们留给你思考原因！）。

**提示/建议：**

- 始终从考虑哪些工作可以并行执行开始。
- 计算的某些部分可能需要同步，例如，通过使用 `#pragma omp critical` 或 `#pragma omp atomic` 将适当的代码包装在一个临界区内。**然而，在这个问题中，你应该考虑如何利用一个称为“比较并交换”（compare and swap，CAS）的简单原子操作。**你可以阅读关于 [GCC 的比较并交换实现](http://gcc.gnu.org/onlinedocs/gcc-4.1.2/gcc/Atomic-Builtins.html)，它在 C 代码中作为函数 `__sync_bool_compare_and_swap` 暴露。如果你能弄清楚如何在这个问题中使用比较并交换，你将比使用临界区获得更高的性能。
- 在类似 `counter++;` 的行之前使用 `#pragma omp atomic` 可以高效地更新共享计数器。
- 是否存在可以避免使用 `compare_and_swap` 的情况？换句话说，当你提前知道比较会失败时？
- 有一个预处理宏 `VERBOSE` 可以方便地禁用每步有用的打印计时（见 `bfs/bfs.cpp` 的顶部）。一般来说，这些打印的发生频率很低（每个 BFS 步骤只发生一次），因此它们对性能没有显著影响，但如果你想在计时时禁用 printf，可以方便地使用这个 `#define`。

## 第 2 部分：“自底向上”广度优先搜索（25 分）

考虑第 1 部分的 BFS 实现中可能导致性能问题的行为。在这些情况下，广度优先搜索步骤的另一种实现可能更高效。与其遍历边界中的所有顶点并标记所有与边界相邻的顶点，不如通过让*每个顶点检查自己是否应该被添加到边界中*来实现 BFS！该算法的基本伪代码如下：

```
    对于图中的每个顶点 v：
        如果 v 尚未被访问 并且
           v 与边界上的顶点 u 共享一条入边：
              将顶点 v 添加到边界；
```

该算法有时被称为 BFS 的“自底向上”实现，因为每个顶点都“向上查看 BFS 树”以找到其祖先。（与第 1 部分实现的被其祖先“自顶向下”找到的方式相对。）

请实现一个自底向上的 BFS，从根节点计算到图中所有顶点的最短路径（参见 `bfs/bfs.cpp` 中的 `bfs_bottom_up()`）。首先实现一个简单的串行版本。然后并行化你的实现。

**提示/建议：**

- 考虑如何表示未访问节点的集合可能是有用的。自顶向下和自底向上的代码版本是否适合不同的实现？
- 自底向上的 BFS 的同步需求如何变化？

## 第3部分：混合 BFS（25 分）

注意，在 BFS 的某些步骤中，“自底向上”BFS 比“自顶向下”版本快得多。而在其他步骤中，“自顶向下”版本又明显更快。这表明，如果**你可以根据边界的大小或图的其他特性动态选择“自顶向下”和“自底向上”这两种实现方式**，你的实现会有显著的性能提升。如果你希望解决方案能够与参考版本竞争，你的实现可能需要实现这种动态优化。请在 `bfs/bfs.cpp` 中的 `bfs_hybrid()` 提供你的解决方案。

**提示/建议：**

- 如果你在第 1 和第 2 部分中使用了不同的边界表示方法，你可能需要在混合解决方案中转换这些表示方法。你如何有效地在它们之间进行转换？这样做是否会产生开销？

你可以通过以下命令运行我们的评分脚本：`./bfs_grader <path to graphs directory>`，它将报告多个图的正确性和性能分数。

## 评分和提交

除了你的代码之外，我们希望你提交一个清晰但简明的高水平描述，说明你的实现方式以及简要描述你如何得出解决方案的。特别是要说明你在过程中尝试的方法，以及你如何确定优化代码的方法（例如，你进行了哪些测量来指导你的优化工作？）。

你在报告中应提及的工作方面包括：

1. 在报告的顶部写上两位合作伙伴的名字。
2. 在 AWS 上运行 bfs_grader 并在你的解决方案中插入分数表的副本。
3. 描述优化代码的过程：

 - 在第 1 部分（自顶向下）和第 2 部分（自底向上），每个解决方案中的同步在哪里？你是否采取措施来限制同步的开销？
 - 在第 3 部分（混合），你是否决定动态地在自顶向下和自底向上的BFS实现之间切换？你如何决定使用哪种实现？
 - 你认为你的代码（以及参考实现）为什么无法实现完美的加速比？（是工作负载不平衡？通信/同步？数据移动？）

请确保你的报告内容简洁明了，并充分解释你在优化过程中所做的关键决策和权衡。

## 分数分配

本次作业的 84 分分配如下：

- 70 分：BFS 性能
- 14 分：报告

如果你在本次作业中获得`x`分，我们将把你之前任一编程作业的成绩提高`(x/84) * 10`分，四舍五入到最接近的十分位。

## 提交说明

请使用 Gradescope 提交你的作业。

1. **请将你的报告作为 PDF 提交到 Gradescope 作业 Programming Assignment 5 (Writeup) 中。**
2. **要提交你的代码，请运行`sh create_submission.sh`生成一个`tar.gz`文件，并将其提交到 Programming Assignment 5 (Code) 中。** 我们只会查看你的`bfs/bfs.cpp`和`bfs/bfs.h`文件，因此不要更改其他任何文件。在提交源文件之前，请确保所有代码可以编译和运行！我们应该能够在`/bfs`目录中简单地使用 make，然后执行你的程序，而无需手动干预。

我们的评分脚本将重新运行检查代码，允许我们验证你的分数与提交的报告内容一致。我们还可能在其他数据集上运行你的代码以进一步检查其正确性。