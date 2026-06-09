/*
 * ============================================================================
 *  KubeRoute — Container Pod Orchestration Dispatcher
 *  Language  : C++
 * ============================================================================
 *
 *  SYSTEM OVERVIEW
 *  ───────────────
 *  KubeRoute is a cloud container management platform similar to Kubernetes.
 *  It implements eight core subsystems, each backed by a purpose-selected
 *  data structure:
 *
 *   1. Catalog Listing      – Trie (prefix tree) for service name search
 *   2. Change Log           – Stack for cluster config rollback
 *   3. Execution Line       – Queue (FIFO) for ordered pod launch
 *   4. Architecture Identity– Hash Map for container-to-server lookup
 *   5. Container Sorter     – Min-Heap for memory-based ranking
 *   6. Server Mesh          – Weighted directed graph (adjacency list)
 *   7. Transit Path         – Dijkstra's algorithm for min-latency routing
 *   8. Workload Balancer    – Min-Heap on load counters for even distribution
 *
 * ============================================================================
 */

#include <iostream>
#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <stack>
#include <queue>
#include <climits>
#include <algorithm>
#include <iomanip>
#include <sstream>

using namespace std;

// ============================================================================
//  UTILITIES — Pretty-print helpers
// ============================================================================

// Prints a coloured section banner to the console
void printBanner(const string& title) {
    cout << "\n";
    cout << "╔══════════════════════════════════════════════════════════╗\n";
    cout << "║  " << left << setw(56) << title << "║\n";
    cout << "╚══════════════════════════════════════════════════════════╝\n";
}

// Prints a sub-section header
void printSection(const string& label) {
    cout << "\n  ──── " << label << " ────\n";
}

// ============================================================================
//  SUBSYSTEM 1 — CATALOG LISTING (Trie / Prefix Tree)
// ============================================================================
/*
 *  WHY A TRIE?
 *  ───────────
 *  Service names in KubeRoute are hierarchical strings (e.g. "payment-svc",
 *  "payment-gateway"). Operators frequently search by prefix — "show me all
 *  services starting with 'pay'". A hash map supports only exact lookup in
 *  O(1); to enumerate prefix matches it would need a full O(N) scan.
 *
 *  A Trie stores each character as an edge. Prefix search reaches the correct
 *  subtree in O(L) steps (L = prefix length), then collects all completions
 *  via DFS. This is how Kubernetes' etcd storage engine supports watch/list
 *  operations with prefix semantics.
 *
 *  Complexity:
 *    Insert  : O(L)         — one step per character
 *    Search  : O(L + K)     — traverse prefix (L) then collect K results
 *    Space   : O(N * L)     — at most N*L nodes in the worst case
 */

struct TrieNode {
    // Each node stores child pointers keyed by character.
    // Using map<char, TrieNode*> keeps children sorted alphabetically.
    map<char, TrieNode*> children;

    // Marks whether the path from root to this node forms a complete name
    bool isEndOfWord = false;
};

class ServiceTrie {
private:
    TrieNode* root;   // Sentinel root node (not part of any name)

    /*
     * Helper: recursively collect all service names in the subtree rooted
     * at 'node', appending them to 'results'.
     *
     * @param node    Current trie node being visited
     * @param current Accumulated characters on the path from the prefix node
     * @param results Output vector of complete service names
     */
    void collectAll(TrieNode* node, string& current, vector<string>& results) {
        if (node->isEndOfWord)
            results.push_back(current);   // Full name found — record it

        // Visit children in sorted order (map iterates alphabetically)
        for (auto& [ch, child] : node->children) {
            current.push_back(ch);        // Extend current path
            collectAll(child, current, results);
            current.pop_back();           // Backtrack
        }
    }

    /*
     * Helper: recursively delete all nodes in the subtree to free memory.
     */
    void destroyTrie(TrieNode* node) {
        for (auto& [ch, child] : node->children)
            destroyTrie(child);
        delete node;
    }

public:
    // Constructor — create the sentinel root
    ServiceTrie() : root(new TrieNode()) {}

    // Destructor — free all dynamically allocated nodes
    ~ServiceTrie() { destroyTrie(root); }

    /*
     * insert — add a service name to the trie.
     *
     * Walk the trie character by character. If a child edge for the current
     * character does not exist, create it. Mark the terminal node.
     *
     * @param name  Service name to register (e.g. "payment-svc")
     */
    void insert(const string& name) {
        TrieNode* cur = root;
        for (char ch : name) {
            // Create child node on demand
            if (!cur->children.count(ch))
                cur->children[ch] = new TrieNode();
            cur = cur->children[ch];
        }
        cur->isEndOfWord = true;  // Mark end of this service name
    }

    /*
     * searchByPrefix — return all service names starting with 'prefix'.
     *
     * Step 1: Traverse the trie along the prefix characters.
     *         If any character is missing, no service matches → return {}.
     * Step 2: From the node reached, collect every name in the subtree.
     *
     * @param prefix  The prefix string to search (e.g. "pay")
     * @return        Sorted list of matching service names
     */
    vector<string> searchByPrefix(const string& prefix) {
        TrieNode* cur = root;

        // Navigate to the end of the prefix
        for (char ch : prefix) {
            if (!cur->children.count(ch))
                return {};   // Prefix not found at all
            cur = cur->children[ch];
        }

        // Collect all names in the subtree, prepending the prefix
        vector<string> results;
        string built = prefix;          // Start collecting from the prefix
        collectAll(cur, built, results);
        return results;
    }

    /*
     * search — exact match: does this service name exist?
     *
     * @param name  Full service name to look up
     * @return      true if found, false otherwise
     */
    bool search(const string& name) {
        TrieNode* cur = root;
        for (char ch : name) {
            if (!cur->children.count(ch)) return false;
            cur = cur->children[ch];
        }
        return cur->isEndOfWord;
    }

    /*
     * remove — delete a service name from the trie.
     *
     * Simply unmarks the end-of-word flag; orphaned nodes are left in place
     * (acceptable for a service catalog where names change infrequently).
     *
     * @param name  Service name to deregister
     */
    void remove(const string& name) {
        TrieNode* cur = root;
        for (char ch : name) {
            if (!cur->children.count(ch)) return;  // Name not found — nothing to do
            cur = cur->children[ch];
        }
        cur->isEndOfWord = false;   // Unmark; node stays in trie
    }
};


// ============================================================================
//  SUBSYSTEM 2 — CHANGE LOG (Stack)
// ============================================================================
/*
 *  WHY A STACK?
 *  ────────────
 *  Cluster configuration changes must be reversible in LIFO order — the most
 *  recent change is the first to be undone, exactly as 'kubectl rollout undo'
 *  reverts to the previous ReplicaSet. A stack enforces this discipline
 *  structurally; no index arithmetic or search is needed.
 *
 *  Complexity:
 *    Push (apply change) : O(1)
 *    Pop  (rollback)     : O(1)
 *    Peek (inspect)      : O(1)
 *    Space               : O(H)   — H = depth of history
 */

// Represents one atomic cluster configuration snapshot
struct ClusterConfig {
    string serviceName;     // Name of the service being configured
    int    replicas;        // Desired number of pod replicas
    string image;           // Container image tag (e.g. "nginx:1.25")
    string envVars;         // Serialised environment variable overrides
    string timestamp;       // Human-readable change timestamp
};

class ConfigChangeLog {
private:
    // std::stack uses a deque internally; push/pop are O(1)
    stack<ClusterConfig> history;

public:
    /*
     * applyChange — record a new configuration change on top of the stack.
     *
     * @param cfg  The new cluster configuration snapshot to record
     */
    void applyChange(const ClusterConfig& cfg) {
        history.push(cfg);
        cout << "  [LOG] Applied config for '" << cfg.serviceName
             << "' | replicas=" << cfg.replicas
             << " | image=" << cfg.image << "\n";
    }

    /*
     * rollback — undo the most recent change by popping the stack.
     *
     * @return  The configuration snapshot that was rolled back to (the one
     *          now active), or an empty config if no history exists.
     */
    ClusterConfig rollback() {
        if (history.empty()) {
            cout << "  [WARN] No change history to roll back.\n";
            return {};   // Default-constructed empty config
        }

        // Remove the current (failed) configuration
        ClusterConfig failed = history.top();
        history.pop();

        cout << "  [ROLLBACK] Reverted '" << failed.serviceName
             << "' from image=" << failed.image << "\n";

        if (!history.empty()) {
            // The new top is the previously active config
            cout << "  [ROLLBACK] Now active: image=" << history.top().image
                 << " replicas=" << history.top().replicas << "\n";
            return history.top();
        } else {
            cout << "  [ROLLBACK] No previous state; cluster reset to defaults.\n";
            return {};
        }
    }

    /*
     * peek — inspect the current active configuration without removing it.
     */
    ClusterConfig peek() const {
        if (history.empty()) return {};
        return history.top();
    }

    /*
     * historyDepth — number of recorded configuration versions.
     */
    int historyDepth() const {
        return (int)history.size();
    }

    /*
     * printHistory — display all recorded configurations (top = most recent).
     * Note: printing requires a copy because std::stack has no iterator.
     */
    void printHistory() const {
        stack<ClusterConfig> tmp = history;   // Copy to preserve original
        int idx = (int)tmp.size();
        cout << "  Change history (newest first):\n";
        while (!tmp.empty()) {
            ClusterConfig& c = const_cast<ClusterConfig&>(tmp.top());
            cout << "    [" << idx-- << "] " << c.serviceName
                 << " | replicas=" << c.replicas
                 << " | image=" << c.image << "\n";
            tmp.pop();
        }
    }
};


// ============================================================================
//  SUBSYSTEM 3 — EXECUTION LINE (Queue / FIFO)
// ============================================================================
/*
 *  WHY A QUEUE?
 *  ────────────
 *  Pod launch requests must be processed in exactly the order they were
 *  submitted — no request should jump ahead of an earlier one. A queue
 *  enforces FIFO (First-In, First-Out) structurally. This mirrors the
 *  Kubernetes scheduler's internal ActiveQ, which orders pending pods by
 *  their submission timestamp.
 *
 *  Complexity:
 *    Enqueue : O(1)
 *    Dequeue : O(1)
 *    Peek    : O(1)
 *    Space   : O(Q)  — Q = number of pending requests
 */

// Describes a single pod launch request
struct PodRequest {
    string podName;         // Unique pod identifier (e.g. "pod-alpha-001")
    string namespace_;      // Kubernetes namespace (e.g. "production")
    string image;           // Container image to run
    int    memoryMB;        // Memory requirement in megabytes
    int    cpuMillicores;   // CPU request in millicores (1000 = 1 core)
    string submittedAt;     // Submission timestamp string
};

class PodLaunchQueue {
private:
    queue<PodRequest> launchQueue;   // FIFO queue of pending pod launches

public:
    /*
     * enqueue — add a pod launch request to the back of the queue.
     *
     * @param req  The pod launch request to schedule
     */
    void enqueue(const PodRequest& req) {
        launchQueue.push(req);
        cout << "  [QUEUE] Enqueued: " << req.podName
             << " (mem=" << req.memoryMB << "MB"
             << ", cpu=" << req.cpuMillicores << "m)\n";
    }

    /*
     * dequeue — remove and return the front (oldest) request for processing.
     *
     * @return  The next pod request to launch, or empty if queue is empty
     */
    PodRequest dequeue() {
        if (launchQueue.empty()) {
            cout << "  [WARN] Launch queue is empty — nothing to process.\n";
            return {};
        }
        PodRequest req = launchQueue.front();
        launchQueue.pop();
        cout << "  [LAUNCH] Processing: " << req.podName
             << " in namespace '" << req.namespace_ << "'\n";
        return req;
    }

    /*
     * peek — inspect the next request without consuming it.
     */
    PodRequest peek() const {
        if (launchQueue.empty()) return {};
        return launchQueue.front();
    }

    /*
     * isEmpty — returns true when no requests are pending.
     */
    bool isEmpty() const { return launchQueue.empty(); }

    /*
     * size — number of requests currently waiting in the queue.
     */
    int size() const { return (int)launchQueue.size(); }

    /*
     * processAll — drain the queue, launching every pending pod in order.
     * Returns the list of pods launched.
     */
    vector<string> processAll() {
        vector<string> launched;
        int order = 1;
        while (!launchQueue.empty()) {
            PodRequest r = launchQueue.front();
            launchQueue.pop();
            cout << "  [" << order++ << "] Launching " << r.podName
                 << " | image=" << r.image << "\n";
            launched.push_back(r.podName);
        }
        return launched;
    }
};


// ============================================================================
//  SUBSYSTEM 4 — ARCHITECTURE IDENTITY (Hash Map)
// ============================================================================
/*
 *  WHY A HASH MAP?
 *  ───────────────
 *  When a container crashes, the operator needs to instantly know which
 *  physical server hosts it so they can investigate or restart it.
 *  std::unordered_map provides average O(1) insert and lookup using a hash
 *  table with chaining. This mirrors the task-ARN-to-instance mapping that
 *  Amazon ECS maintains in its control plane for routing agent heartbeats.
 *
 *  Complexity:
 *    Register (insert) : O(1) average, O(N) worst (hash collision storm)
 *    Lookup            : O(1) average
 *    Remove            : O(1) average
 *    Space             : O(N) — one entry per registered container
 */

class ContainerRegistry {
private:
    // Key   = container ID (e.g. "container-uuid-001")
    // Value = server ID   (e.g. "server-node-7")
    unordered_map<string, string> containerToServer;

    // Reverse map: server → list of containers hosted on it.
    // Allows quick server-level queries without scanning all containers.
    unordered_map<string, vector<string>> serverToContainers;

public:
    /*
     * registerContainer — associate a container ID with its host server.
     *
     * @param containerID  Unique container identifier
     * @param serverID     Server hosting this container
     */
    void registerContainer(const string& containerID, const string& serverID) {
        containerToServer[containerID] = serverID;
        serverToContainers[serverID].push_back(containerID);
        cout << "  [REGISTER] " << containerID << " -> " << serverID << "\n";
    }

    /*
     * lookup — find the server hosting a given container.
     *
     * @param containerID  Container to look up
     * @return             Server ID, or "NOT_FOUND" if unregistered
     */
    string lookup(const string& containerID) const {
        auto it = containerToServer.find(containerID);
        if (it == containerToServer.end())
            return "NOT_FOUND";
        return it->second;
    }

    /*
     * deregister — remove a container mapping (e.g. when the pod terminates).
     *
     * @param containerID  Container to unregister
     */
    void deregister(const string& containerID) {
        auto it = containerToServer.find(containerID);
        if (it == containerToServer.end()) {
            cout << "  [WARN] Container '" << containerID << "' not found.\n";
            return;
        }

        string serverID = it->second;

        // Remove from reverse map
        auto& vec = serverToContainers[serverID];
        vec.erase(remove(vec.begin(), vec.end(), containerID), vec.end());

        // Remove from forward map
        containerToServer.erase(it);
        cout << "  [DEREGISTER] Removed " << containerID
             << " from " << serverID << "\n";
    }

    /*
     * containersOnServer — list all containers running on a given server.
     *
     * @param serverID  Server to query
     * @return          Vector of container IDs hosted on that server
     */
    vector<string> containersOnServer(const string& serverID) const {
        auto it = serverToContainers.find(serverID);
        if (it == serverToContainers.end()) return {};
        return it->second;
    }

    /*
     * totalContainers — total number of registered container-to-server pairs.
     */
    int totalContainers() const {
        return (int)containerToServer.size();
    }
};


// ============================================================================
//  SUBSYSTEM 5 — CONTAINER SORTER (Min-Heap / Priority Queue)
// ============================================================================
/*
 *  WHY A MIN-HEAP?
 *  ───────────────
 *  Before assigning pods to servers, the scheduler must rank them by memory
 *  requirement so low-memory pods are placed first (best-fit strategy).
 *  A min-heap always exposes the smallest element at the top in O(1), and
 *  maintains the heap invariant on insert/delete in O(log N). This is more
 *  efficient than sorting (O(N log N)) when pods arrive incrementally.
 *
 *  This mirrors Kubernetes' LeastRequestedPriority scoring plugin that ranks
 *  nodes by available capacity before binding pods.
 *
 *  Complexity:
 *    Insert      : O(log N)
 *    Extract Min : O(log N)
 *    Peek Min    : O(1)
 *    Space       : O(N)
 */

// Represents a container waiting to be scheduled, with its resource profile
struct Container {
    string name;       // Container name (e.g. "redis-cache")
    int    memoryMB;   // Memory required to run this container
    int    cpuMilli;   // CPU millicores required

    // Comparator for min-heap: lower memoryMB has higher priority
    // std::priority_queue is a max-heap by default; we invert the comparison
    // to get min-heap behaviour.
    bool operator>(const Container& other) const {
        return memoryMB > other.memoryMB;   // Invert: smallest memory → top
    }
};

class ContainerSorter {
private:
    // priority_queue<T, Container, Comparator>
    // Using 'greater<Container>' flips the default max-heap into a min-heap
    priority_queue<Container, vector<Container>, greater<Container>> minHeap;

public:
    /*
     * addContainer — push a container onto the min-heap.
     *
     * The heap automatically sifts it to the correct position in O(log N).
     *
     * @param c  Container descriptor with resource requirements
     */
    void addContainer(const Container& c) {
        minHeap.push(c);
        cout << "  [SORTER] Added: " << c.name
             << " | memory=" << c.memoryMB << "MB"
             << " | cpu=" << c.cpuMilli << "m\n";
    }

    /*
     * extractSmallest — pop and return the container with the least memory.
     *
     * Call this when the scheduler is ready to place the next container;
     * it always returns the one that is easiest to satisfy memory-wise.
     *
     * @return  Container with minimum memory requirement
     */
    Container extractSmallest() {
        if (minHeap.empty()) {
            cout << "  [WARN] Sorter is empty.\n";
            return {"EMPTY", 0, 0};
        }
        Container top = minHeap.top();
        minHeap.pop();
        return top;
    }

    /*
     * peekSmallest — inspect the minimum-memory container without removing it.
     */
    Container peekSmallest() const {
        if (minHeap.empty()) return {"EMPTY", 0, 0};
        return minHeap.top();
    }

    /*
     * isEmpty — true when no containers are pending scheduling.
     */
    bool isEmpty() const { return minHeap.empty(); }

    /*
     * size — number of containers currently in the sorter.
     */
    int size() const { return (int)minHeap.size(); }

    /*
     * drainSorted — extract all containers in ascending memory order.
     * Useful for displaying the scheduling plan.
     *
     * @return  Vector of containers sorted by ascending memoryMB
     */
    vector<Container> drainSorted() {
        vector<Container> sorted;
        while (!minHeap.empty()) {
            sorted.push_back(minHeap.top());
            minHeap.pop();
        }
        return sorted;   // Already in sorted order (min-heap extraction)
    }
};


// ============================================================================
//  SUBSYSTEM 6 & 7 — SERVER MESH + TRANSIT PATH
//  (Weighted Directed Graph + Dijkstra's Algorithm)
// ============================================================================
/*
 *  WHY AN ADJACENCY LIST GRAPH?
 *  ────────────────────────────
 *  Servers are connected by network links with varying latency (weight).
 *  An adjacency matrix uses O(V²) space and is wasteful when the mesh is
 *  sparse (few links per server). An adjacency list uses O(V + E) space and
 *  gives O(degree) neighbour enumeration — ideal for sparse server networks.
 *
 *  WHY DIJKSTRA'S ALGORITHM?
 *  ──────────────────────────
 *  BFS finds shortest paths only on unweighted graphs (hop count). Link
 *  latencies are heterogeneous (e.g. 1ms LAN vs 80ms WAN), so we need a
 *  weighted shortest-path algorithm. Dijkstra's greedy relaxation with a
 *  min-heap priority queue gives the correct answer in O((V+E) log V).
 *
 *  This mirrors how Istio's Envoy sidecar proxies compute optimal traffic
 *  paths across a service mesh based on observed latency metrics.
 *
 *  Complexity:
 *    Add server (vertex)  : O(1)
 *    Add link (edge)      : O(1)
 *    Dijkstra shortest path: O((V+E) log V)
 *    Space (graph)        : O(V + E)
 *    Space (Dijkstra)     : O(V)  for dist[] and visited[]
 */

class ServerMesh {
private:
    int numServers;   // Total number of servers (vertices) in the mesh

    // adj[u] = list of (v, latency_ms) meaning: server u connects to server v
    //          with latency_ms milliseconds of link latency.
    vector<vector<pair<int,int>>> adj;

    // Human-readable names for each server index
    vector<string> serverNames;

public:
    /*
     * Constructor — initialise the mesh with a fixed number of servers.
     *
     * @param n  Number of servers in the cluster
     */
    explicit ServerMesh(int n) : numServers(n), adj(n) {
        // Default names if not overridden
        for (int i = 0; i < n; ++i)
            serverNames.push_back("server-" + to_string(i));
    }

    /*
     * setServerName — assign a human-readable label to a server node.
     *
     * @param idx   Server index (0-based)
     * @param name  Human-readable server label
     */
    void setServerName(int idx, const string& name) {
        if (idx >= 0 && idx < numServers)
            serverNames[idx] = name;
    }

    /*
     * addLink — add a directed edge from server 'from' to server 'to'
     *           with the given latency in milliseconds.
     *
     *  For undirected (bidirectional) links, call addLink twice (both ways).
     *
     * @param from       Source server index
     * @param to         Destination server index
     * @param latencyMs  Link latency in milliseconds
     */
    void addLink(int from, int to, int latencyMs) {
        adj[from].push_back({to, latencyMs});
        cout << "  [MESH] Link: " << serverNames[from]
             << " -> " << serverNames[to]
             << " (" << latencyMs << "ms)\n";
    }

    /*
     * printMesh — display the full adjacency list for debugging.
     */
    void printMesh() const {
        cout << "  Server Mesh Topology:\n";
        for (int u = 0; u < numServers; ++u) {
            cout << "    " << serverNames[u] << " connects to:\n";
            if (adj[u].empty()) {
                cout << "      (no outgoing links)\n";
            } else {
                for (auto [v, w] : adj[u])
                    cout << "      -> " << serverNames[v]
                         << " (" << w << "ms)\n";
            }
        }
    }

    /*
     * dijkstra — compute the minimum-latency path from 'src' to all other
     *            servers using Dijkstra's algorithm with a min-heap.
     *
     *  Algorithm steps:
     *    1. Initialise dist[src] = 0; all others = INF.
     *    2. Push (0, src) onto the priority queue.
     *    3. While the queue is non-empty:
     *       a. Extract the server u with the smallest known distance.
     *       b. Skip u if we already finalised its shortest distance.
     *       c. For each neighbour v of u with edge weight w:
     *          If dist[u] + w < dist[v], update dist[v] and push to queue.
     *    4. Return dist[] and prev[] (for path reconstruction).
     *
     * @param src    Source server index
     * @param dist   Output: dist[v] = min latency from src to v
     * @param prev   Output: prev[v] = predecessor of v on shortest path
     */
    void dijkstra(int src,
                  vector<int>& dist,
                  vector<int>& prev) const {

        // Initialise all distances to "infinity"
        dist.assign(numServers, INT_MAX);
        prev.assign(numServers, -1);   // -1 = no predecessor (path not found)
        dist[src] = 0;

        // Min-heap: (distance, serverIndex)
        // priority_queue is max-heap by default; negate or use greater<> to get min-heap
        priority_queue<pair<int,int>,
                       vector<pair<int,int>>,
                       greater<pair<int,int>>> pq;
        pq.push({0, src});

        while (!pq.empty()) {
            auto [d, u] = pq.top();
            pq.pop();

            // Stale entry check: if we already found a shorter path to u, skip
            if (d > dist[u]) continue;

            // Relax all edges out of u
            for (auto [v, w] : adj[u]) {
                if (dist[u] + w < dist[v]) {
                    dist[v]  = dist[u] + w;
                    prev[v]  = u;              // Record the path predecessor
                    pq.push({dist[v], v});     // Push updated distance
                }
            }
        }
    }

    /*
     * shortestPath — high-level interface: find and display the minimum-
     *                latency path between two servers.
     *
     * @param src  Source server index
     * @param dst  Destination server index
     */
    void shortestPath(int src, int dst) const {
        vector<int> dist, prev;
        dijkstra(src, dist, prev);

        cout << "\n  Shortest path: "
             << serverNames[src] << " → " << serverNames[dst] << "\n";

        if (dist[dst] == INT_MAX) {
            cout << "  [RESULT] No path exists (servers are disconnected).\n";
            return;
        }

        // Reconstruct path by walking back through prev[] pointers
        vector<int> path;
        for (int v = dst; v != -1; v = prev[v])
            path.push_back(v);
        reverse(path.begin(), path.end());   // Reverse to get src -> dst order

        // Print the path
        cout << "  Path   : ";
        for (int i = 0; i < (int)path.size(); ++i) {
            if (i) cout << " -> ";
            cout << serverNames[path[i]];
        }
        cout << "\n";
        cout << "  Latency: " << dist[dst] << " ms (optimal)\n";
    }

    /*
     * allShortestPaths — display minimum latency from src to every server.
     *
     * @param src  Source server index
     */
    void allShortestPaths(int src) const {
        vector<int> dist, prev;
        dijkstra(src, dist, prev);

        cout << "\n  All minimum-latency paths from " << serverNames[src] << ":\n";
        for (int v = 0; v < numServers; ++v) {
            cout << "    -> " << setw(14) << left << serverNames[v] << ": ";
            if (dist[v] == INT_MAX)
                cout << "UNREACHABLE\n";
            else
                cout << dist[v] << " ms\n";
        }
    }

    int getNumServers() const { return numServers; }
    string getServerName(int idx) const { return serverNames[idx]; }
};


// ============================================================================
//  SUBSYSTEM 8 — WORKLOAD BALANCER (Min-Heap on Load Counters)
// ============================================================================
/*
 *  WHY A MIN-HEAP ON LOAD SCORES?
 *  ──────────────────────────────
 *  The goal is to always assign the next pod to the least-loaded server.
 *  Scanning all servers each time is O(N); maintaining a sorted array is
 *  O(N log N) per update. A min-heap lets us find the minimum in O(1) and
 *  update it in O(log N) — ideal for a hot path executed on every pod
 *  placement event.
 *
 *  This mirrors Kubernetes' 'LeastRequestedPriority' scoring function and
 *  Amazon ECS' 'spread' placement strategy.
 *
 *  Complexity:
 *    Add server       : O(log N)
 *    Assign pod       : O(log N)  — extract min + re-insert
 *    Peek min load    : O(1)
 *    Space            : O(N)
 */

struct ServerLoad {
    string serverID;   // Server identifier
    int    podCount;   // Number of pods currently assigned to this server

    // Comparator: smaller podCount → higher priority in min-heap
    bool operator>(const ServerLoad& other) const {
        return podCount > other.podCount;
    }
};

class WorkloadBalancer {
private:
    // Min-heap ordered by ascending podCount
    priority_queue<ServerLoad,
                   vector<ServerLoad>,
                   greater<ServerLoad>> minHeap;

    // Persistent load counts (the heap may have stale entries after updates)
    unordered_map<string, int> loadMap;

public:
    /*
     * addServer — register a server in the balancer with initial load of 0.
     *
     * @param serverID  Server identifier (e.g. "node-A")
     */
    void addServer(const string& serverID) {
        loadMap[serverID] = 0;
        minHeap.push({serverID, 0});
        cout << "  [BALANCER] Registered server: " << serverID << " (load=0)\n";
    }

    /*
     * assignPod — place a pod on the currently least-loaded server.
     *
     *  Implementation note: Because std::priority_queue does not support
     *  in-place updates, we use a "lazy deletion" pattern:
     *    1. Extract the top entry.
     *    2. If its stored podCount is stale (less than loadMap value), discard
     *       and try the next entry.
     *    3. Once a fresh entry is found, increment its count and re-insert.
     *
     * @param podName  Name of the pod to assign
     * @return         Server ID that received the pod
     */
    string assignPod(const string& podName) {
        if (minHeap.empty()) {
            cout << "  [ERROR] No servers registered in balancer.\n";
            return "";
        }

        // Lazy deletion: skip stale heap entries
        while (!minHeap.empty()) {
            ServerLoad top = minHeap.top();
            minHeap.pop();

            // Check freshness: loadMap holds the authoritative count
            if (top.podCount == loadMap[top.serverID]) {
                // This entry is current — use it
                loadMap[top.serverID]++;          // Increment authoritative count
                minHeap.push({top.serverID,
                               loadMap[top.serverID]});  // Re-insert updated entry

                cout << "  [ASSIGN] " << podName
                     << " -> " << top.serverID
                     << " (load now " << loadMap[top.serverID] << ")\n";
                return top.serverID;
            }
            // Otherwise: stale entry — discard and continue
        }

        cout << "  [ERROR] All server entries are stale — balancer corrupt.\n";
        return "";
    }

    /*
     * getLoad — return the current pod count for a specific server.
     *
     * @param serverID  Server to query
     * @return          Number of pods assigned, or -1 if server unknown
     */
    int getLoad(const string& serverID) const {
        auto it = loadMap.find(serverID);
        if (it == loadMap.end()) return -1;
        return it->second;
    }

    /*
     * printLoads — display pod count for every registered server.
     */
    void printLoads() const {
        cout << "  Current server loads:\n";
        // Copy to sortable vector for ordered display
        vector<pair<string,int>> loads(loadMap.begin(), loadMap.end());
        sort(loads.begin(), loads.end());  // Sort by server name
        for (auto& [srv, cnt] : loads)
            cout << "    " << left << setw(12) << srv
                 << ": " << cnt << " pods\n";
    }

    /*
     * leastLoadedServer — return the server with the fewest pods without
     *                     modifying the heap.
     */
    string leastLoadedServer() {
        // Find minimum in loadMap (O(N)) — used only for inspection
        string best;
        int minLoad = INT_MAX;
        for (auto& [srv, cnt] : loadMap) {
            if (cnt < minLoad) { minLoad = cnt; best = srv; }
        }
        return best;
    }
};


// ============================================================================
//  DISPATCHER — Central Orchestration Controller
// ============================================================================
/*
 *  The Dispatcher wires all eight subsystems together and provides a unified
 *  interface. In a real system (Kubernetes, ECS) this role is played by the
 *  API Server + Controller Manager + Scheduler pipeline.
 */

class KubeRouteDispatcher {
public:
    ServiceTrie      catalog;      // Subsystem 1: Service name registry
    ConfigChangeLog  changeLog;    // Subsystem 2: Config rollback log
    PodLaunchQueue   execLine;     // Subsystem 3: FIFO pod launch queue
    ContainerRegistry identity;   // Subsystem 4: Container-to-server map
    ContainerSorter  sorter;       // Subsystem 5: Memory-based container ranking
    WorkloadBalancer balancer;     // Subsystem 8: Even workload distribution

    // Server mesh is initialised separately because it needs a size at construction
    ServerMesh* mesh = nullptr;    // Subsystem 6 & 7: Network topology + routing

    ~KubeRouteDispatcher() { delete mesh; }

    void initMesh(int numServers) {
        delete mesh;
        mesh = new ServerMesh(numServers);
    }
};


// ============================================================================
//  DEMO FUNCTIONS — Exercise every subsystem with illustrative examples
// ============================================================================

// ── Demo 1 ──────────────────────────────────────────────────────────────────
void demoCatalogListing(ServiceTrie& catalog) {
    printBanner("SUBSYSTEM 1 — Catalog Listing (Trie)");

    // Register services: these form paths in the trie character by character
    vector<string> services = {
        "payment-svc", "payment-gateway", "payment-callback",
        "auth-service", "auth-proxy", "auth-token-refresher",
        "monitoring-agent", "monitoring-dashboard",
        "user-profile", "user-settings", "user-avatar",
        "order-service", "order-history", "order-tracking"
    };

    printSection("Inserting services");
    for (const string& s : services)
        catalog.insert(s);
    cout << "  Inserted " << services.size() << " services into trie.\n";

    // Prefix search — returns all services sharing the given prefix
    printSection("Prefix search 'pay'");
    auto payResults = catalog.searchByPrefix("pay");
    for (const string& r : payResults)
        cout << "  Found: " << r << "\n";

    printSection("Prefix search 'auth'");
    auto authResults = catalog.searchByPrefix("auth");
    for (const string& r : authResults)
        cout << "  Found: " << r << "\n";

    printSection("Prefix search 'order'");
    auto orderResults = catalog.searchByPrefix("order");
    for (const string& r : orderResults)
        cout << "  Found: " << r << "\n";

    printSection("Prefix search 'xyz' (no match expected)");
    auto none = catalog.searchByPrefix("xyz");
    cout << (none.empty() ? "  No services found.\n" : "  Unexpected results!\n");

    printSection("Exact search");
    cout << "  'auth-service' exists: "
         << (catalog.search("auth-service") ? "YES" : "NO") << "\n";
    cout << "  'auth-unknown'  exists: "
         << (catalog.search("auth-unknown") ? "YES" : "NO") << "\n";

    printSection("Remove 'payment-gateway' then re-search 'pay'");
    catalog.remove("payment-gateway");
    auto afterRemove = catalog.searchByPrefix("pay");
    for (const string& r : afterRemove)
        cout << "  Found: " << r << "\n";
}

// ── Demo 2 ──────────────────────────────────────────────────────────────────
void demoChangeLog(ConfigChangeLog& log) {
    printBanner("SUBSYSTEM 2 — Change Log (Stack)");

    // Simulate a sequence of configuration changes
    printSection("Applying configuration changes");
    log.applyChange({"payment-svc", 3, "nginx:1.21", "ENV=prod", "2025-01-01T08:00Z"});
    log.applyChange({"payment-svc", 5, "nginx:1.25", "ENV=prod", "2025-01-01T09:30Z"});
    log.applyChange({"payment-svc", 10, "nginx:1.25-alpine", "ENV=prod,DEBUG=1", "2025-01-01T10:00Z"});

    printSection("History depth: " + to_string(log.historyDepth()));
    log.printHistory();

    // The last change caused a crash — roll back twice
    printSection("Rolling back (last change was faulty)");
    log.rollback();

    printSection("Rolling back again");
    log.rollback();

    printSection("Attempting rollback on empty stack");
    log.rollback();
    log.rollback();  // Should warn gracefully
}

// ── Demo 3 ──────────────────────────────────────────────────────────────────
void demoExecutionLine(PodLaunchQueue& execLine) {
    printBanner("SUBSYSTEM 3 — Execution Line (FIFO Queue)");

    printSection("Submitting pod launch requests");
    execLine.enqueue({"pod-alpha-001", "production", "redis:7.0", 128, 250, "T+0ms"});
    execLine.enqueue({"pod-beta-002",  "staging",    "nginx:1.25", 64, 100, "T+5ms"});
    execLine.enqueue({"pod-gamma-003", "production", "postgres:15", 512, 500, "T+9ms"});
    execLine.enqueue({"pod-delta-004", "dev",        "python:3.11", 256, 300, "T+12ms"});

    cout << "\n  Queue size: " << execLine.size() << " pending requests\n";

    printSection("Processing all pods in FIFO order");
    execLine.processAll();
}

// ── Demo 4 ──────────────────────────────────────────────────────────────────
void demoArchitectureIdentity(ContainerRegistry& reg) {
    printBanner("SUBSYSTEM 4 — Architecture Identity (Hash Map)");

    printSection("Registering containers");
    reg.registerContainer("container-uuid-001", "server-node-7");
    reg.registerContainer("container-uuid-002", "server-node-3");
    reg.registerContainer("container-uuid-003", "server-node-7");
    reg.registerContainer("container-uuid-004", "server-node-1");
    reg.registerContainer("container-uuid-005", "server-node-3");

    printSection("Lookup tests");
    vector<string> toFind = {"container-uuid-001", "container-uuid-003",
                              "container-uuid-999"};
    for (const string& id : toFind) {
        string server = reg.lookup(id);
        cout << "  " << id << " -> " << server << "\n";
    }

    printSection("Containers on server-node-7");
    auto onNode7 = reg.containersOnServer("server-node-7");
    for (const string& c : onNode7)
        cout << "  " << c << "\n";

    printSection("Deregister container-uuid-002");
    reg.deregister("container-uuid-002");
    cout << "  Total containers: " << reg.totalContainers() << "\n";
}

// ── Demo 5 ──────────────────────────────────────────────────────────────────
void demoContainerSorter(ContainerSorter& sorter) {
    printBanner("SUBSYSTEM 5 — Container Sorter (Min-Heap)");

    printSection("Adding containers (unsorted order)");
    sorter.addContainer({"ml-trainer",   8192, 4000});
    sorter.addContainer({"redis-cache",   128,  250});
    sorter.addContainer({"api-gateway",   512,  500});
    sorter.addContainer({"log-collector",  64,  100});
    sorter.addContainer({"postgres-db",  2048, 1000});
    sorter.addContainer({"nginx-proxy",   256,  200});

    printSection("Extracting containers in ascending memory order");
    int rank = 1;
    while (!sorter.isEmpty()) {
        Container c = sorter.extractSmallest();
        cout << "  [" << rank++ << "] " << left << setw(16) << c.name
             << " mem=" << setw(6) << c.memoryMB << "MB"
             << " cpu=" << c.cpuMilli << "m\n";
    }
}

// ── Demo 6 & 7 ──────────────────────────────────────────────────────────────
void demoServerMesh(ServerMesh& mesh) {
    printBanner("SUBSYSTEM 6 — Server Mesh (Weighted Graph)");

    // Label nodes with meaningful server names
    mesh.setServerName(0, "node-dc1-rack1");
    mesh.setServerName(1, "node-dc1-rack2");
    mesh.setServerName(2, "node-dc2-rack1");
    mesh.setServerName(3, "node-dc2-rack2");
    mesh.setServerName(4, "node-dc3-edge");

    printSection("Adding network links (directed, with latency ms)");
    // Within DC1 — fast LAN links
    mesh.addLink(0, 1, 2);    // dc1-rack1 -> dc1-rack2 : 2ms
    mesh.addLink(1, 0, 2);    // bidirectional

    // DC1 to DC2 — cross-datacenter
    mesh.addLink(0, 2, 10);
    mesh.addLink(2, 0, 10);
    mesh.addLink(1, 3, 12);
    mesh.addLink(3, 1, 12);

    // Within DC2 — fast LAN
    mesh.addLink(2, 3, 3);
    mesh.addLink(3, 2, 3);

    // DC2 to DC3 — edge link (higher latency)
    mesh.addLink(3, 4, 25);
    mesh.addLink(4, 3, 25);

    // Direct DC1 to DC3 — long-haul link
    mesh.addLink(0, 4, 40);

    printSection("Mesh topology");
    mesh.printMesh();

    printBanner("SUBSYSTEM 7 — Transit Path (Dijkstra's Algorithm)");

    printSection("All shortest paths from node-dc1-rack1");
    mesh.allShortestPaths(0);

    printSection("Specific path queries");
    mesh.shortestPath(0, 4);   // dc1-rack1 -> dc3-edge
    mesh.shortestPath(1, 4);   // dc1-rack2 -> dc3-edge
    mesh.shortestPath(2, 4);   // dc2-rack1 -> dc3-edge
}

// ── Demo 8 ──────────────────────────────────────────────────────────────────
void demoWorkloadBalancer(WorkloadBalancer& balancer) {
    printBanner("SUBSYSTEM 8 — Workload Balancer (Min-Heap on Load)");

    printSection("Registering servers");
    balancer.addServer("node-A");
    balancer.addServer("node-B");
    balancer.addServer("node-C");
    balancer.addServer("node-D");

    printSection("Distributing 12 pods across the cluster");
    vector<string> pods = {
        "pod-001","pod-002","pod-003","pod-004",
        "pod-005","pod-006","pod-007","pod-008",
        "pod-009","pod-010","pod-011","pod-012"
    };
    for (const string& pod : pods)
        balancer.assignPod(pod);

    printSection("Final load distribution");
    balancer.printLoads();

    // Verify balance
    int minL = INT_MAX, maxL = 0;
    for (const string srv : {"node-A","node-B","node-C","node-D"}) {
        int load = balancer.getLoad(srv);
        minL = min(minL, load);
        maxL = max(maxL, load);
    }
    cout << "\n  Load spread (max - min): " << (maxL - minL)
         << " pod(s) — "
         << (maxL - minL <= 1 ? "WELL BALANCED" : "IMBALANCED") << "\n";
}


// ============================================================================
//  MAIN — Run all demonstrations
// ============================================================================

int main() {
    cout << "\n";
    cout << "██╗  ██╗██╗   ██╗██████╗ ███████╗██████╗  ██████╗ ██╗   ██╗████████╗███████╗\n";
    cout << "██║ ██╔╝██║   ██║██╔══██╗██╔════╝██╔══██╗██╔═══██╗██║   ██║╚══██╔══╝██╔════╝\n";
    cout << "█████╔╝ ██║   ██║██████╔╝█████╗  ██████╔╝██║   ██║██║   ██║   ██║   █████╗  \n";
    cout << "██╔═██╗ ██║   ██║██╔══██╗██╔══╝  ██╔══██╗██║   ██║██║   ██║   ██║   ██╔══╝  \n";
    cout << "██║  ██╗╚██████╔╝██████╔╝███████╗██║  ██║╚██████╔╝╚██████╔╝   ██║   ███████╗\n";
    cout << "╚═╝  ╚═╝ ╚═════╝ ╚═════╝ ╚══════╝╚═╝  ╚═╝ ╚═════╝  ╚═════╝   ╚═╝   ╚══════╝\n";
    cout << "\n  Container Pod Orchestration Dispatcher — Full System Demo\n";
    cout << "  Data Structures & Algorithms | C++17\n\n";

    // ── Instantiate the central dispatcher ──────────────────────────────────
    KubeRouteDispatcher dispatcher;
    dispatcher.initMesh(5);   // 5 servers in the cluster mesh

    // ── Run each subsystem demonstration ────────────────────────────────────
    demoCatalogListing(dispatcher.catalog);
    demoChangeLog(dispatcher.changeLog);
    demoExecutionLine(dispatcher.execLine);
    demoArchitectureIdentity(dispatcher.identity);
    demoContainerSorter(dispatcher.sorter);
    demoServerMesh(*dispatcher.mesh);
    demoWorkloadBalancer(dispatcher.balancer);

    // ── Final summary ────────────────────────────────────────────────────────
    printBanner("ALL SUBSYSTEMS EXECUTED SUCCESSFULLY");
    cout << "\n  KubeRoute subsystem summary:\n";
    cout << "    [1] Catalog Listing      — Trie          — O(L) prefix search\n";
    cout << "    [2] Change Log           — Stack         — O(1) rollback\n";
    cout << "    [3] Execution Line       — Queue         — O(1) FIFO dispatch\n";
    cout << "    [4] Architecture Identity— Hash Map      — O(1) avg lookup\n";
    cout << "    [5] Container Sorter     — Min-Heap      — O(log N) extraction\n";
    cout << "    [6] Server Mesh          — Adj-List Graph— O(V+E) storage\n";
    cout << "    [7] Transit Path         — Dijkstra      — O((V+E)log V)\n";
    cout << "    [8] Workload Balancer    — Min-Heap      — O(log N) placement\n";
    cout << "\n";

    return 0;
}

/*
 * ============================================================================
 *  HOW TO COMPILE AND RUN
 * ============================================================================
 *
 *  g++ -std=c++17 -O2 -Wall kuberoute.cpp -o kuberoute
 *  ./kuberoute
 *
 *  Expected output: a coloured console walkthrough of all 8 subsystems with
 *  realistic service names, pod requests, server topologies, and load counts.
 *
 * ============================================================================
 */