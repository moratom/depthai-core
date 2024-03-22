#pragma once

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>

// project
#include "depthai/openvino/OpenVINO.hpp"
#include "depthai/pipeline/AssetManager.hpp"
#include "depthai/pipeline/MessageQueue.hpp"
#include "depthai/utility/copyable_unique_ptr.hpp"

// depthai
#include "depthai/pipeline/datatype/DatatypeEnum.hpp"
#include "depthai/properties/Properties.hpp"

// libraries
#include <optional>

namespace dai {
// fwd declare Pipeline
class Pipeline;
class PipelineImpl;

/**
 * @brief Abstract Node
 */
class Node : public std::enable_shared_from_this<Node> {
    friend class Pipeline;
    friend class PipelineImpl;

   public:
    /// Node identificator. Unique for every node on a single Pipeline
    using Id = std::int64_t;
    struct Connection;
    struct ConnectionInternal;
    // fwd declare classes
    class Input;
    class Output;
    class InputMap;
    class OutputMap;

   protected:
    std::unordered_map<std::string, Output*> outputRefs;
    std::unordered_map<std::string, Input*> inputRefs;
    std::unordered_map<std::string, OutputMap*> outputMapRefs;
    std::unordered_map<std::string, InputMap*> inputMapRefs;
    std::unordered_map<std::string, std::shared_ptr<Node>*> nodeRefs;

    // helpers for setting refs
    void setOutputRefs(std::initializer_list<Output*> l);
    void setOutputRefs(Output* outRef);
    void setInputRefs(std::initializer_list<Input*> l);
    void setInputRefs(Input* inRef);
    void setOutputMapRefs(std::initializer_list<OutputMap*> l);
    void setOutputMapRefs(OutputMap* outMapRef);
    void setInputMapRefs(std::initializer_list<InputMap*> l);
    void setInputMapRefs(InputMap* inMapRef);
    void setNodeRefs(std::initializer_list<std::pair<std::string, std::shared_ptr<Node>*>> l);
    void setNodeRefs(std::pair<std::string, std::shared_ptr<Node>*> nodeRef);
    void setNodeRefs(std::string alias, std::shared_ptr<Node>* nodeRef);

    template <typename T>
    class Subnode {
        std::shared_ptr<Node> node;

       public:
        Subnode(Node& parent, std::string alias) {
            if(!parent.configureMode) {
                // Create node as well
                node = std::make_shared<T>();
                node->setAlias(alias);
                node->parentNode = node;
                // Add node to parents map
                parent.nodeMap.push_back(node);
            }
            // Add reference
            parent.nodeRefs[alias] = &node;
        }
        T& operator*() const noexcept {
            return *std::static_pointer_cast<T>(node).get();
        }
        T* operator->() const noexcept {
            return std::static_pointer_cast<T>(node).get();
        }
    };

   public:
    struct DatatypeHierarchy {
        DatatypeHierarchy(DatatypeEnum d, bool c) : datatype(d), descendants(c) {}
        DatatypeEnum datatype;
        bool descendants;
    };

    class Output {
       public:
        struct QueueConnection {
            Output* output;
            std::shared_ptr<MessageQueue> queue;
            bool operator==(const QueueConnection& rhs) const {
                return output == rhs.output && queue == rhs.queue;
            }
        };

       private:
        Node& parent;
        std::vector<std::shared_ptr<MessageQueue>> connectedInputs;
        std::vector<QueueConnection> queueConnections;

       public:
        enum class Type { MSender, SSender };
        std::string group = "";
        std::string name;
        Type type;
        // Which types and do descendants count as well?
        std::vector<DatatypeHierarchy> possibleDatatypes;

        Output(Node& par) : parent(par), type(Type::MSender), possibleDatatypes({{DatatypeEnum::Buffer, true}}) {
            // Place oneself to the parents references
            parent.setOutputRefs(this);
        }

        Output(Node& par, std::string name) : parent(par), name{std::move(name)}, type(Type::MSender), possibleDatatypes({{DatatypeEnum::Buffer, true}}) {
            // Place oneself to the parents references
            parent.setOutputRefs(this);
        }

        Output(Node& par, std::string n, Type t, std::vector<DatatypeHierarchy> types)
            : parent(par), name(std::move(n)), type(t), possibleDatatypes(std::move(types)) {
            // Place oneself to the parents references
            parent.setOutputRefs(this);
        }
        Output(Node& par, std::string group, std::string n, Type t, std::vector<DatatypeHierarchy> types)
            : parent(par), group(std::move(group)), name(std::move(n)), type(t), possibleDatatypes(std::move(types)) {
            parent.setOutputRefs(this);
        }

        Node& getParent() {
            return parent;
        }
        const Node& getParent() const {
            return parent;
        }

        /// Output to string representation
        std::string toString() const;

        /**
         * Check if this output and given input are on the same pipeline.
         * @see canConnect for checking if connection is possible
         * @returns True if output and input are on the same pipeline
         */
        bool isSamePipeline(const Input& in);

        /**
         * Check if connection is possible
         * @param in Input to connect to
         * @returns True if connection is possible, false otherwise
         */
        bool canConnect(const Input& in);

        /**
         * Retrieve all connections from this output
         * @returns Vector of connections
         */
        std::vector<ConnectionInternal> getConnections();

        /**
         * Retrieve all queue connections from this output
         * @returns Vector of queue connections
         */
        std::vector<QueueConnection> getQueueConnections() {
            return queueConnections;
        }

        std::shared_ptr<dai::MessageQueue> getQueue() {
            auto queue = std::make_shared<MessageQueue>();
            link(queue);
            return queue;
        }

        void link(const std::shared_ptr<dai::MessageQueue>& queue) {
            connectedInputs.push_back(queue);
            queueConnections.push_back({this, queue});
        }

        void unlink(const std::shared_ptr<dai::MessageQueue>& queue) {
            connectedInputs.erase(std::remove(connectedInputs.begin(), connectedInputs.end(), queue), connectedInputs.end());
            queueConnections.erase(std::remove(queueConnections.begin(), queueConnections.end(), QueueConnection{this, queue}), queueConnections.end());
        }

        /**
         * Link current output to input.
         *
         * Throws an error if this output cannot be linked to given input,
         * or if they are already linked
         *
         * @param in Input to link to
         */
        void link(Input& in);

        /**
         * Unlink a previously linked connection
         *
         * Throws an error if not linked.
         *
         * @param in Input from which to unlink from
         */
        void unlink(Input& in);

        /**
         * Sends a Message to all connected inputs
         * @param msg Message to send to all connected inputs
         */
        void send(const std::shared_ptr<ADatatype>& msg);

        /**
         * Try sending a message to all connected inputs
         * @param msg Message to send to all connected inputs
         * @returns True if ALL connected inputs got the message, false otherwise
         */
        bool trySend(const std::shared_ptr<ADatatype>& msg);
    };

    struct PairHash {
        template <class T1, class T2>
        std::size_t operator()(const std::pair<T1, T2>& pair) const {
            return std::hash<T1>()(pair.first) ^ std::hash<T2>()(pair.second);
        }
    };
    /**
     * Output map which keeps track of extra outputs assigned to a node
     * Extends std::unordered_map<std::string, dai::Node::Output>
     */
    class OutputMap : public std::unordered_map<std::pair<std::string, std::string>, Output, PairHash> {
        Output defaultOutput;

       public:
        std::string name;
        OutputMap(std::string name, Output defaultOutput);
        OutputMap(Output defaultOutput);
        OutputMap(bool ref, Node& parent, std::string name, Output defaultOutput);
        OutputMap(bool ref, Node& parent, Output defaultOutput);
        /// Create or modify an output
        Output& operator[](const std::string& key);
        /// Create or modify an output with specified group
        Output& operator[](std::pair<std::string, std::string> groupKey);
    };

    // Input is a thing layer over a MessageQueue which carries meta data and contains the actual queue as a shared_ptr
    class Input {
       public:
        enum class Type { SReceiver, MReceiver };

       private:
        Node& parent;
        static constexpr bool defaultBlocking{true};
        static constexpr int defaultMaxQueueSize{8};

        // Options - more information about the input
        bool waitForMessage{false};
        std::string group;
        std::string name;
        Type type;

       public:
        // Queue is a shared_ptr as both the node outputs and the node inputs need access and it needs to stay alive as long as the last node is alive
        std::shared_ptr<MessageQueue> queue;
        std::vector<DatatypeHierarchy> possibleDatatypes;

        // Create a default input type
        Input(Node& par, bool inputRef = true)
            : Input(par, "", Type::SReceiver, defaultBlocking, defaultMaxQueueSize, false, {{DatatypeEnum::Buffer, true}}, inputRef) {}

        Input(Node& par, std::string n, bool inputRef = true)
            : Input(par, "", n, Type::SReceiver, defaultBlocking, defaultMaxQueueSize, false, {{DatatypeEnum::Buffer, true}}, inputRef) {}

        Input(Node& par, std::string n, Type t, std::vector<DatatypeHierarchy> types, bool inputRef = true)
            : Input(par, "", n, t, defaultBlocking, defaultMaxQueueSize, false, std::move(types), inputRef) {}

        Input(Node& par, std::string n, Type t, bool blocking, int queueSize, std::vector<DatatypeHierarchy> types, bool inputRef = true)
            : Input(par, "", n, t, blocking, queueSize, false, std::move(types), inputRef) {}

        Input(Node& par, std::string n, Type t, bool blocking, int queueSize, bool waitForMessage, std::vector<DatatypeHierarchy> types, bool inputRef = true)
            : Input(par, "", n, t, blocking, queueSize, waitForMessage, std::move(types), inputRef) {}

        Input(Node& par,
              std::string group,
              std::string n,
              Type t,
              bool blocking,
              int queueSize,
              bool waitForMessage,
              std::vector<DatatypeHierarchy> types,
              bool inputRef = true)
            : parent(par),
              waitForMessage(waitForMessage),
              group(std::move(group)),
              name(std::move(n)),
              type(t),
              queue(std::make_shared<MessageQueue>(queueSize, blocking)),
              possibleDatatypes(std::move(types)) {
            if(inputRef) {
                parent.setInputRefs(this);
            }
        }

        bool operator==(const Input& rhs) const {
            return &parent == &rhs.parent && group == rhs.group && name == rhs.name && type == rhs.type && getBlocking() == rhs.getBlocking()
                   && getQueueSize() == rhs.getQueueSize() && getWaitForMessage() == rhs.getWaitForMessage()
                   && getReusePreviousMessage() == rhs.getReusePreviousMessage();
        }

        void setGroup(std::string group) {
            this->group = std::move(group);
        }

        void setName(std::string name) {
            this->name = std::move(name);
        }

        std::string getGroup() const {
            return group;
        }

        std::string getName() const {
            return name;
        }

        Type getType() const {
            return type;
        }

        Node& getParent() {
            return parent;
        }
        const Node& getParent() const {
            return parent;
        }

        /// Input to string representation
        std::string toString() const;

        /**
         * Overrides default input queue behavior.
         * @param blocking True blocking, false overwriting
         */
        void setBlocking(bool blocking);

        /**
         * Get input queue behavior
         * @returns True blocking, false overwriting
         */
        bool getBlocking() const;

        /**
         * Overrides default input queue size.
         * If queue size fills up, behavior depends on `blocking` attribute
         * @param size Maximum input queue size
         */
        void setQueueSize(int size);

        /**
         * Get input queue size.
         * @returns Maximum input queue size
         */
        int getQueueSize() const;

        /**
         * Overrides default wait for message behavior.
         * Applicable for nodes with multiple inputs.
         * Specifies behavior whether to wait for this input when a Node processes certain data or not.
         * @param waitForMessage Whether to wait for message to arrive to this input or not
         */
        void setWaitForMessage(bool waitForMessage);

        /**
         * Get behavior whether to wait for this input when a Node processes certain data or not
         * @returns Whether to wait for message to arrive to this input or not
         */
        bool getWaitForMessage() const;

        /**
         * Equivalent to setWaitForMessage but with inverted logic.
         */
        void setReusePreviousMessage(bool reusePreviousMessage);

        /**
         * Equivalent to getWaitForMessage but with inverted logic.
         */
        bool getReusePreviousMessage() const;

        // TODO(themarpe) - refactor, rather extend from MessageQueue

        /**
         * Check whether front of the queue has message of type T
         * @returns True if queue isn't empty and the first element is of type T, false otherwise
         */
        template <class T>
        bool has() {
            return queue->has<T>();
        }

        /**
         * Check whether front of the queue has a message (isn't empty)
         * @returns True if queue isn't empty, false otherwise
         */
        bool has() {
            return queue->has();
        }

        /**
         * Try to retrieve message T from queue. If message isn't of type T it returns nullptr
         *
         * @returns Message of type T or nullptr if no message available
         */
        template <class T>
        std::shared_ptr<T> tryGet() {
            return queue->tryGet<T>();
        }

        /**
         * Try to retrieve message from queue. If no message available, return immediately with nullptr
         *
         * @returns Message or nullptr if no message available
         */
        std::shared_ptr<ADatatype> tryGet() {
            return tryGet<ADatatype>();
        }

        /**
         * Block until a message is available.
         *
         * @returns Message of type T or nullptr if no message available
         */
        template <class T>
        std::shared_ptr<T> get() {
            return queue->get<T>();
        }

        /**
         * Block until a message is available.
         *
         * @returns Message or nullptr if no message available
         */
        std::shared_ptr<ADatatype> get() {
            return get<ADatatype>();
        }

        /**
         * Gets first message in the queue.
         *
         * @returns Message of type T or nullptr if no message available
         */
        template <class T>
        std::shared_ptr<T> front() {
            return queue->front<T>();
        }

        /**
         * Gets first message in the queue.
         *
         * @returns Message or nullptr if no message available
         */
        std::shared_ptr<ADatatype> front() {
            return front<ADatatype>();
        }

        /**
         * Block until a message is available with a timeout.
         *
         * @param timeout Duration for which the function should block
         * @param[out] hasTimedout Outputs true if timeout occurred, false otherwise
         * @returns Message of type T otherwise nullptr if message isn't type T or timeout occurred
         */
        template <class T, typename Rep, typename Period>
        std::shared_ptr<T> get(std::chrono::duration<Rep, Period> timeout, bool& hasTimedout) {
            return queue->get<T, Rep, Period>(timeout, hasTimedout);
        }

        /**
         * Block until a message is available with a timeout.
         *
         * @param timeout Duration for which the function should block
         * @param[out] hasTimedout Outputs true if timeout occurred, false otherwise
         * @returns Message of type T otherwise nullptr if message isn't type T or timeout occurred
         */
        template <typename Rep, typename Period>
        std::shared_ptr<ADatatype> get(std::chrono::duration<Rep, Period> timeout, bool& hasTimedout) {
            return get<ADatatype>(timeout, hasTimedout);
        }

        /**
         * Try to retrieve all messages in the queue.
         *
         * @returns Vector of messages which can either be of type T or nullptr
         */
        template <class T>
        std::vector<std::shared_ptr<T>> tryGetAll() {
            return queue->tryGetAll<T>();
        }

        /**
         * Try to retrieve all messages in the queue.
         *
         * @returns Vector of messages
         */
        std::vector<std::shared_ptr<ADatatype>> tryGetAll() {
            return tryGetAll<ADatatype>();
        }

        /**
         * Block until at least one message in the queue.
         * Then return all messages from the queue.
         *
         * @returns Vector of messages which can either be of type T or nullptr
         */
        template <class T>
        std::vector<std::shared_ptr<T>> getAll() {
            return queue->getAll<T>();
        }

        /**
         * Block until at least one message in the queue.
         * Then return all messages from the queue.
         *
         * @returns Vector of messages
         */
        std::vector<std::shared_ptr<ADatatype>> getAll() {
            return getAll<ADatatype>();
        }

        /**
         * Block for maximum timeout duration.
         * Then return all messages from the queue.
         * @param timeout Maximum duration to block
         * @param[out] hasTimedout Outputs true if timeout occurred, false otherwise
         * @returns Vector of messages which can either be of type T or nullptr
         */
        template <class T, typename Rep, typename Period>
        std::vector<std::shared_ptr<T>> getAll(std::chrono::duration<Rep, Period> timeout, bool& hasTimedout) {
            return queue->getAll<T, Rep, Period>(timeout, hasTimedout);
        }

        /**
         * Block for maximum timeout duration.
         * Then return all messages from the queue.
         * @param timeout Maximum duration to block
         * @param[out] hasTimedout Outputs true if timeout occurred, false otherwise
         * @returns Vector of messages
         */
        template <typename Rep, typename Period>
        std::vector<std::shared_ptr<ADatatype>> getAll(std::chrono::duration<Rep, Period> timeout, bool& hasTimedout) {
            return getAll<ADatatype>(timeout, hasTimedout);
        }
    };

    /**
     * Input map which keeps track of inputs assigned to a node
     * Extends std::unordered_map<std::string, dai::Node::Input>
     */
    class InputMap : public std::unordered_map<std::pair<std::string, std::string>, Input, PairHash> {
        Input defaultInput;

       public:
        std::string name;
        // InputMap(Input defaultInput);
        // InputMap(std::string name, Input defaultInput);
        InputMap(Node& parent, Input defaultInput);
        InputMap(Node& parent, std::string name, Input defaultInput);
        /// Create or modify an input
        Input& operator[](const std::string& key);
        /// Create or modify an input with specified group
        Input& operator[](std::pair<std::string, std::string> groupKey);
        // Check if the input exists
        bool has(const std::string& key) const;
    };

    /// Connection between an Input and Output internal
    struct ConnectionInternal {
        ConnectionInternal(Output& out, Input& in);
        std::weak_ptr<Node> outputNode;
        std::string outputName;
        std::string outputGroup;
        std::weak_ptr<Node> inputNode;
        std::string inputName;
        std::string inputGroup;
        Output* out;
        Input* in;
        bool operator==(const ConnectionInternal& rhs) const;
        struct Hash {
            size_t operator()(const dai::Node::ConnectionInternal& obj) const;
        };
    };

    /// Connection between an Input and Output
    struct Connection {
        friend struct std::hash<Connection>;
        Connection(Output out, Input in);
        Connection(ConnectionInternal c);
        Id outputId;
        std::string outputName;
        std::string outputGroup;
        Id inputId;
        std::string inputName;
        std::string inputGroup;
        bool operator==(const Connection& rhs) const;
    };

   protected:
    bool configureMode{false};

    // when Pipeline tries to serialize and construct on remote, it will check if all connected nodes are on same pipeline
    std::weak_ptr<PipelineImpl> parent;
    std::weak_ptr<Node> parentNode;

   public:
    // TODO(themarpe) - restrict access
    /// Id of node. Assigned after being placed on the pipeline
    Id id{-1};

    /// alias or name
    std::string alias;

   protected:
    AssetManager assetManager;

    virtual std::optional<OpenVINO::Version> getRequiredOpenVINOVersion();

    // Optimized for adding, searching and removing connections
    // using NodeMap = std::unordered_map<Node::Id, std::shared_ptr<Node>>;
    using NodeMap = std::vector<std::shared_ptr<Node>>;
    NodeMap nodeMap;

    // Connection map, NodeId represents id of node connected TO (input)
    // using NodeConnectionMap = std::unordered_map<Node::Id, std::unordered_set<Node::Connection>>;
    using SetConnectionInternal = std::unordered_set<ConnectionInternal, ConnectionInternal::Hash>;
    SetConnectionInternal connections;
    using ConnectionMap = std::unordered_map<std::shared_ptr<Node>, SetConnectionInternal>;

   public:
    // access
    Pipeline getParentPipeline();
    const Pipeline getParentPipeline() const;

    /// Get alias
    std::string getAlias() const {
        return alias;
    }
    /// Set alias
    void setAlias(std::string alias) {
        this->alias = std::move(alias);
    }

    // /// Deep copy the node
    // virtual std::unique_ptr<Node> clone() const = 0;

    /// Retrieves nodes name
    virtual const char* getName() const = 0;

    // TBD - might be an default impl instead
    /// Start node execution
    virtual void start(){};

    /// Wait for node to finish execution
    virtual void wait(){};

    /// Stop node execution
    virtual void stop(){};

    void stopPipeline();

    /// Build stages;
    virtual void buildStage1();
    virtual void buildStage2();
    virtual void buildStage3();

    /// Retrieves all nodes outputs
    std::vector<Output> getOutputs();

    /// Retrieves all nodes inputs
    std::vector<Input> getInputs();

    /// Retrieves reference to node outputs
    std::vector<Output*> getOutputRefs();

    /// Retrieves reference to node outputs
    std::vector<const Output*> getOutputRefs() const;

    /// Retrieves reference to node inputs
    std::vector<Input*> getInputRefs();

    /// Retrieves reference to node inputs
    std::vector<const Input*> getInputRefs() const;

    /// Retrieves reference to node outputs
    std::vector<OutputMap*> getOutputMapRefs();

    /// Retrieves reference to node inputs
    std::vector<InputMap*> getInputMapRefs();

    /// Retrieves reference to specific output
    Output* getOutputRef(std::string name);
    Output* getOutputRef(std::string group, std::string name);

    /// Retrieves reference to specific input
    Input* getInputRef(std::string name);
    Input* getInputRef(std::string group, std::string name);

    /// Retrieves reference to specific output map
    OutputMap* getOutputMapRef(std::string group);

    /// Retrieves reference to specific input map
    InputMap* getInputMapRef(std::string group);

   protected:
    Node() = default;
    Node(bool conf);
    void build();
    void removeConnectionToNode(std::shared_ptr<Node> node);

   public:
    virtual ~Node() = default;

    /// Get node AssetManager as a const reference
    const AssetManager& getAssetManager() const;

    /// Get node AssetManager as a reference
    AssetManager& getAssetManager();

    /// Loads resource specified by URI and returns its data
    std::vector<uint8_t> loadResource(dai::Path uri);

    /// Create and place Node to this Node
    template <class N>
    std::shared_ptr<N> create() {
        // Check that passed type 'N' is subclass of Node
        static_assert(std::is_base_of<Node, N>::value, "Specified class is not a subclass of Node");
        // Create and store the node in the map
        auto node = std::make_shared<N>();
        // Add
        add(node);
        // Return shared pointer to this node
        return node;
    }

    /// Add existing node to nodeMap
    void add(std::shared_ptr<Node> node);

    // Access to nodes
    std::vector<std::shared_ptr<Node>> getAllNodes() const;
    std::shared_ptr<const Node> getNode(Node::Id id) const;
    std::shared_ptr<Node> getNode(Node::Id id);
    void remove(std::shared_ptr<Node> node);
    ConnectionMap getConnectionMap();
    void link(const Node::Output& out, const Node::Input& in);
    void unlink(const Node::Output& out, const Node::Input& in);
    /// Get a reference to internal node map
    const NodeMap& getNodeMap() const {
        return nodeMap;
    }
};

// Node CRTP class
template <typename Base, typename Derived>
class NodeCRTP : public Base {
   public:
    virtual ~NodeCRTP() = default;

    const char* getName() const override {
        return Derived::NAME;
    };
    // std::unique_ptr<Node> clone() const override {
    //     return std::make_unique<Derived>(static_cast<const Derived&>(*this));
    // };
    void build() {}

    // No public constructor, only a factory function.
    template <typename... Args>
    [[nodiscard]] static std::shared_ptr<Derived> create(Args&&... args) {
        auto n = std::make_shared<Derived>(std::forward<Args>(args)...);
        n->build();
        return n;
    }
    [[nodiscard]] static std::shared_ptr<Derived> create(std::unique_ptr<Properties> props) {
        auto n = std::shared_ptr<Derived>(new Derived(props));
        // Configure mode, don't build
        // n->build();
        return n;
    }

    friend Derived;
    friend Base;
};

}  // namespace dai

// Specialization of std::hash for Node::Connection
namespace std {
template <>
struct hash<dai::Node::Connection> {
    size_t operator()(const dai::Node::Connection& obj) const {
        size_t seed = 0;
        std::hash<dai::Node::Id> hId;
        std::hash<std::string> hStr;
        seed ^= hId(obj.outputId) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= hStr(obj.outputName) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= hId(obj.inputId) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= hStr(obj.outputName) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

}  // namespace std
