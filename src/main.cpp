#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <future>
#include <queue>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <condition_variable>

const int MAX_COMPONENTS = 32;
using Entity = std::size_t;

std::mutex coutMutex;

// ====================================================
// THREAD POOL FOR MULTITHREADING
// ====================================================
class ThreadPool 
{
public:
    ThreadPool(size_t numThreads);
    ~ThreadPool();
    template<typename Func>
    auto enqueue(Func task) -> std::future<decltype(task())>;

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop;
};

ThreadPool::ThreadPool(size_t numThreads) : stop(false) 
{
    for (size_t i = 0; i < numThreads; i++) 
    {
        workers.emplace_back([this]() 
        {
            while (true) 
            {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    condition.wait(lock, [this] { return stop || !tasks.empty(); });
                    if (stop && tasks.empty()) return;
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() 
{
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop = true;
    }
    condition.notify_all();
    for (std::thread& worker : workers)
        worker.join();
}

template<typename Func>
auto ThreadPool::enqueue(Func task) -> std::future<decltype(task())> 
{
    auto taskPtr = std::make_shared<std::packaged_task<decltype(task())()>>(std::move(task));
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        tasks.push([taskPtr]() { (*taskPtr)(); });
    }
    condition.notify_one();
    return taskPtr->get_future();
}

// ====================================================
// COMPONENT, COMPONENT MANAGER & COMPONENTS
// ====================================================

// Base Component Class
class Component 
{
public:
    virtual ~Component() = default;
};

// Component Manager
class ComponentManager 
{
private:
    std::unordered_map<std::size_t, std::vector<std::unique_ptr<Component>>> components;

public:
    template<typename T, typename... Args>
    void addComponent(Entity entity, Args&&... args) 
    {
        std::size_t typeId = getComponentTypeID<T>();
        if (components[typeId].size() <= entity) 
        {
            components[typeId].resize(entity + 1);
        }
        components[typeId][entity] = std::make_unique<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    T* getComponent(Entity entity) 
    {
        std::size_t typeId = getComponentTypeID<T>();
        if (components.find(typeId) != components.end() && entity < components[typeId].size()) 
        {
            return static_cast<T*>(components[typeId][entity].get());
        }
        return nullptr;
    }

    template<typename T>
    static std::size_t getComponentTypeID() 
    {
        static std::size_t typeID = typeCounter++;
        return typeID;
    }

private:
    static inline std::size_t typeCounter = 0;
};

class Position : public Component 
{
public:
    float x, y, z;
    Position(float x, float y, float z) : x(x), y(y), z(z) {}
    void update(float dx, float dy, float dz) { x += dx; y += dy; z += dz; }
};

class Velocity : public Component 
{
public:
    float dx, dy, dz;
    Velocity(float dx, float dy, float dz) : dx(dx), dy(dy), dz(dz) {}
};

class AI : public Component 
{
public:
    std::string state;
    AI(const std::string& initialState) : state(initialState) {}
};


// ====================================================
// SYSTEMS
// ====================================================

// Rendering System (Separate Thread)
class RenderingSystem 
{
public:
    void render(std::vector<Entity>& entities, ComponentManager& cm) 
    {
        while (true) 
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));  // Simulate 60 FPS
            std::lock_guard<std::mutex> lock(coutMutex);
            std::cout << "Rendering Frame...\n";
        }
    }
};

// Input System (Separate Thread)
class InputSystem 
{
public:
    void processInput() 
    {
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(32));  // Simulate input polling
            std::lock_guard<std::mutex> lock(coutMutex);
            std::cout << "Processing Input...\n";
        }
    }
};

// Physics System (Thread Pooled)
class PhysicsSystem 
{
public:
    void update(ComponentManager& cm, std::vector<Entity>& entities, ThreadPool& pool, float deltaTime) 
    {
        std::vector<std::future<void>> futures;
        for (Entity entity : entities) 
        {
            futures.push_back(pool.enqueue([&, entity]() {
                Position* pos = cm.getComponent<Position>(entity);
                Velocity* vel = cm.getComponent<Velocity>(entity);
                if (pos && vel) 
                {
                    pos->x += vel->dx * deltaTime;
                    pos->y += vel->dy * deltaTime;
                    pos->z += vel->dz * deltaTime;
                    std::lock_guard<std::mutex> lock(coutMutex);
                    std::cout << "Physics: Entity " << entity << " at ("
                        << pos->x << ", " << pos->y << ", " << pos->z << ")\n";
                }
                }));
        }
        for (auto& f : futures) f.get();
    }
};

// AI System (Thread Pooled)
class AISystem 
{
public:
    void update(ComponentManager& cm, std::vector<Entity>& entities, ThreadPool& pool) 
    {
        std::vector<std::future<void>> futures;
        for (Entity entity : entities) 
        {
            futures.push_back(pool.enqueue([&, entity]() {
                AI* ai = cm.getComponent<AI>(entity);
                if (ai) 
                {
                    ai->state = "Patrolling";
                    std::lock_guard<std::mutex> lock(coutMutex);
                    std::cout << "AI: Entity " << entity << " is now " << ai->state << "\n";
                }
                }));
        }
        for (auto& f : futures) f.get();
    }
};

// ====================================================
// MAIN GAME
// ====================================================
int main() 
{
    ComponentManager cm;
    PhysicsSystem physicsSystem;
    AISystem aiSystem;
    RenderingSystem renderingSystem;
    InputSystem inputSystem;

    // Create Thread Pool
    ThreadPool pool(4);

    // Entities
    const int entityCount = 5;
    std::vector<Entity> entities(entityCount);
    for (int i = 0; i < entityCount; i++) 
    {
        entities[i] = i;
        cm.addComponent<Position>(i, 0.0f, 0.0f, 0.0f);
        cm.addComponent<Velocity>(i, 1.0f, 0.5f, 0.2f);
        cm.addComponent<AI>(i, "Idle");
    }

    float deltaTime = 0.016f;
    bool running = true;

    // Start Rendering & Input Threads
    std::thread renderThread(&RenderingSystem::render, &renderingSystem, std::ref(entities), std::ref(cm));
    std::thread inputThread(&InputSystem::processInput, &inputSystem);

    // MAIN GAME LOOP
    while (running) 
    {
        auto frameStart = std::chrono::high_resolution_clock::now();

        // Run Physics & AI Updates in Parallel
        std::thread physicsThread([&]() { physicsSystem.update(cm, entities, pool, deltaTime); });
        std::thread aiThread([&]() { aiSystem.update(cm, entities, pool); });

        physicsThread.join();
        aiThread.join();

        // Frame Rate (60 FPS)
        auto frameEnd = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float> frameTime = frameEnd - frameStart;
        std::this_thread::sleep_for(std::chrono::milliseconds(16) - frameTime);
    }

    renderThread.join();
    inputThread.join();

    return 0;
}
