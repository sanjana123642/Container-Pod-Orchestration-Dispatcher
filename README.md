2.1 Project Title

KubeRoute – Container Pod Orchestration Dispatcher Using Data Structures and Algorithms in C++

2.2 Problem Statement

Modern cloud platforms such as Kubernetes and Docker Swarm manage thousands of containers across distributed servers. Efficient orchestration requires fast service discovery, workload distribution, configuration rollback, routing optimization, and resource scheduling.

Traditional approaches may suffer from inefficient searches, poor load balancing, and increased routing latency. Therefore, there is a need for a system that demonstrates how fundamental Data Structures and Algorithms (DSA) can be applied to solve real-world cloud orchestration problems efficiently.

The KubeRoute project simulates a container orchestration platform by integrating multiple DSA concepts such as Trie, Stack, Queue, Hash Map, Heap, Graph, and Dijkstra’s Algorithm.

2.3 Objectives

The main objectives of the project are:

To implement a cloud-inspired container orchestration system.
To demonstrate practical applications of Data Structures and Algorithms.
To provide efficient service discovery using Trie.
To maintain configuration rollback history using Stack.
To process pod launch requests using Queue.
To manage container-server mappings using Hash Maps.
To prioritize containers using Min-Heaps.
To represent server topology using Graphs.
To find shortest network paths using Dijkstra's Algorithm.
To distribute workloads evenly across servers using Heap-based Load Balancing.
2.4 System Overview / Architecture

The KubeRoute system consists of eight interconnected subsystems:

                     +----------------------+
                     |   KubeRoute System   |
                     +----------+-----------+
                                |
      ---------------------------------------------------------
      |         |         |         |         |        |       |
      V         V         V         V         V        V       V

 Service    Change    Execution  Container  Container Server  Workload
 Catalog     Log        Line      Registry   Sorter    Mesh   Balancer

  (Trie)    (Stack)    (Queue)   (HashMap) (MinHeap) (Graph) (MinHeap)
                                                    |
                                                    V
                                            Dijkstra Algorithm
Subsystems
Subsystem	Data Structure
Catalog Listing	Trie
Change Log	Stack
Execution Line	Queue
Architecture Identity	Hash Map
Container Sorter	Min Heap
Server Mesh	Graph (Adjacency List)
Transit Path	Dijkstra Algorithm
Workload Balancer	Min Heap
2.5 Data Structures and Algorithms Used
1. Trie (Prefix Tree)

Purpose:

Service registration
Prefix-based service search

Operations:

Insert Service
Exact Search
Prefix Search
Delete Service

Complexity:

Operation	Complexity
Insert	O(L)
Search	O(L)
Prefix Search	O(L + K)
2. Stack

Purpose:

Configuration history
Rollback mechanism

Operations:

Push Configuration
Rollback
View Current Configuration

Complexity:

Operation	Complexity
Push	O(1)
Pop	O(1)
Peek	O(1)
3. Queue

Purpose:

FIFO Pod Scheduling

Operations:

Enqueue Request
Dequeue Request
Process Requests

Complexity:

Operation	Complexity
Enqueue	O(1)
Dequeue	O(1)
4. Hash Map

Purpose:

Container-to-Server Mapping

Operations:

Register Container
Lookup Container
Remove Container

Complexity:

Operation	Complexity
Insert	O(1) Average
Lookup	O(1) Average
Delete	O(1) Average
5. Min Heap

Purpose:

Container Ranking
Workload Balancing

Operations:

Insert
Extract Minimum
Priority Scheduling

Complexity:

Operation	Complexity
Insert	O(log N)
Extract Min	O(log N)
Peek	O(1)
6. Graph (Adjacency List)

Purpose:

Network Topology Representation

Complexity:

Operation	Complexity
Add Vertex	O(1)
Add Edge	O(1)
Space	O(V + E)
7. Dijkstra’s Algorithm

Purpose:

Minimum latency path computation

Complexity:

Operation	Complexity
Shortest Path	O((V+E) log V)
2.6 Implementation Approach
Step 1: Service Registration

Services are stored in a Trie to support fast prefix searches.

Example:

catalog.insert("payment-svc");
catalog.insert("auth-service");
Step 2: Configuration Tracking

Each configuration update is pushed onto a Stack.

changeLog.applyChange(config);

Rollback is achieved using:

changeLog.rollback();
Step 3: Pod Scheduling

Pod requests are stored in a Queue and processed in FIFO order.

execLine.enqueue(request);
execLine.processAll();
Step 4: Container Mapping

Containers are mapped to servers using Hash Maps.

identity.registerContainer(id, server);
Step 5: Container Prioritization

Containers are inserted into a Min Heap based on memory requirements.

sorter.addContainer(container);
Step 6: Network Routing

A weighted graph represents server connections.

mesh.addLink(0,1,2);

Shortest path is computed using Dijkstra.

mesh.shortestPath(0,4);
Step 7: Load Balancing

Pods are assigned to the least-loaded server.

balancer.assignPod("pod-001");
2.7 Time and Space Complexity Analysis
Subsystem	Time Complexity	Space Complexity
Trie Search	O(L + K)	O(N×L)
Stack Rollback	O(1)	O(H)
Queue Processing	O(1)	O(Q)
Hash Map Lookup	O(1) Avg	O(N)
Heap Insert	O(log N)	O(N)
Heap Extract	O(log N)	O(N)
Graph Storage	O(V+E)	O(V+E)
Dijkstra	O((V+E)logV)	O(V)
Load Balancer	O(log N)	O(N)

Where:

N = Number of elements
L = Length of string
V = Vertices
E = Edges
H = History depth
Q = Queue size
2.8 Execution Steps
Compilation
g++ -std=c++17 -O2 -Wall kuberoute.cpp -o kuberoute
Run
./kuberoute
Execution Flow
Initialize Dispatcher
Initialize Server Mesh
Execute Trie Demonstration
Execute Stack Demonstration
Execute Queue Demonstration
Execute Hash Map Demonstration
Execute Heap Demonstration
Execute Graph Demonstration
Execute Dijkstra Demonstration
Execute Load Balancer Demonstration
Display Final Summary
2.9 Sample Inputs and Outputs
Input

Service Registration

payment-svc
payment-gateway
payment-callback
Output
Prefix Search: pay

Found:
payment-svc
payment-gateway
payment-callback
Input

Container Lookup

container-uuid-001
Output
container-uuid-001 -> server-node-7
Input

Shortest Path Query

Source: node-dc1-rack1
Destination: node-dc3-edge
Output
Path:
node-dc1-rack1 -> node-dc3-edge

Latency:
40 ms
Input

Pod Distribution

12 Pods
4 Servers
Output
node-A : 3 pods
node-B : 3 pods
node-C : 3 pods
node-D : 3 pods

Load Spread = 0


2.11 Results and Observations
Trie successfully performed efficient prefix searches.
Stack provided instant rollback functionality.
Queue preserved FIFO execution order.
Hash Map enabled near-constant-time lookups.
Min Heap correctly prioritized low-memory containers.
Graph accurately modeled server topology.
Dijkstra produced optimal minimum-latency routes.
Load Balancer distributed pods almost uniformly across servers.
All subsystems executed successfully without runtime errors.
The project demonstrated practical applications of core DSA concepts in cloud computing.
2.12 Conclusion

The KubeRoute – Container Pod Orchestration Dispatcher project successfully demonstrates how Data Structures and Algorithms can be applied to real-world cloud orchestration systems. By integrating Trie, Stack, Queue, Hash Map, Heap, Graph, and Dijkstra’s Algorithm, the system efficiently performs service discovery, rollback management, task scheduling, routing, and load balancing.

The project highlights the importance of selecting appropriate data structures for specific tasks and provides a strong foundation for understanding modern container orchestration platforms such as Kubernetes and Docker Swarm. Future enhancements may include dynamic scaling, fault tolerance, monitoring dashboards, and real-time cluster simulation.
