#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "engine/log.hpp"
#include "engine/networking/session.hpp"
#include "simpleEvent.hpp"

namespace engine { class Scene; }

namespace engine::events {

    /** Queues events and dispatches each to the one handler subscribed to its type.
     *
     *  Events run in order of (tick, priority, raise order). A handler may raise more
     *  events; ones due by the tick being run are dispatched in the same run().
     *
     *  Networking: sending is up to the game (encode() an event and send it through its
     *  session). receive() turns messages back into queued events for every networked
     *  type this process subscribes to.
     *
     *  @thread_safety Not threadsafe. Use from the thread that owns the Scene (the sim
     *  thread), as with sceneReplicator. */
    class eventManager {
        public:
        /** maxEventsPerRun bounds one run() so a raise cycle can't hang the sim thread. */
        explicit eventManager(std::size_t maxEventsPerRun = 4096);

        /** Sets the handler for T. One handler per type: a second subscribe for the
         *  same type is an error and is ignored. For a networked T this also lets
         *  receive() decode it. */
        template <eventType T>
        void subscribe(std::function<void(const T&, Scene&)> handler)
        {
            const std::type_index type(typeid(T));
            if(handlers_.contains(type))
            {
                log::error("eventManager: {} already has a handler; ignoring the new one", type.name());
                assert(false && "one handler per event type");
                return;
            }
            handlers_.emplace(type, [h = std::move(handler)](const simpleEvent& e, Scene& scene) {
                h(static_cast<const T&>(e), scene);
            });

            if constexpr (networkedEvent<T>)
            {
                const std::uint16_t kind = T::kType;
                if(decoders_.contains(kind))
                {
                    log::error("eventManager: network type {} is used by more than one event", kind);
                }
                decoders_.insert_or_assign(kind, decoder{&decodeAs<T>, type});
            }
        }

        /** Queues event for tick, a gameTime tick: ctx.tick to run it this tick,
         *  ctx.tick + n to delay it n ticks. */
        template <eventType T>
        void raise(T event, std::int64_t tick, int priority = 0)
        {
            event.tick = tick;
            event.priority = priority;
            push(eventPtr(new T(std::move(event)), &destroyAs<T>), std::type_index(typeid(T)));
        }

        /** Dispatches every event due by tick. Events with no handler are dropped; the
         *  first of each type is logged. */
        void run(Scene& scene, std::int64_t tick);

        /** Decodes messages from sessionClient/sessionServer::drainMessages() and queues
         *  them for tick, marked remote. Unknown message types are logged and skipped. */
        void receive(std::deque<networking::receivedMessage> messages, std::int64_t tick);

        /** The tick of the run() in progress (or the last one); for handlers that raise
         *  follow-up events. */
        std::int64_t now() const {return now_;}

        std::size_t queued() const {return queue_.size();}
        /** Drops every queued event. */
        void clear() {queue_.clear();}

        private:
        using handlerFn = std::function<void(const simpleEvent&, Scene&)>;
        // simpleEvent has no virtual destructor, so each event carries its own deleter
        using eventPtr = std::unique_ptr<simpleEvent, void (*)(simpleEvent*)>;

        struct decoder {
            eventPtr (*decode)(networking::ByteView);
            std::type_index type;
        };

        struct queuedEvent {
            eventPtr event;
            std::type_index type;
            std::uint64_t seq;          // raise order; ties break FIFO
        };

        /** Heap comparator: true if a runs after b. */
        static bool runsLater(const queuedEvent& a, const queuedEvent& b);

        template <eventType T>
        static void destroyAs(simpleEvent* event)
        {
            delete static_cast<T*>(event);
        }

        template <networkedEvent T>
        static eventPtr decodeAs(networking::ByteView payload)
        {
            auto event = T::decode(payload);
            if(!event) {return eventPtr(nullptr, &destroyAs<T>);}
            return eventPtr(new T(std::move(*event)), &destroyAs<T>);
        }

        void push(eventPtr event, std::type_index type);

        std::unordered_map<std::type_index, handlerFn> handlers_;
        std::unordered_map<std::uint16_t, decoder> decoders_;    // by kType
        std::unordered_set<std::type_index> warnedUnhandled_;
        std::vector<queuedEvent> queue_;    // binary heap; priority_queue can't move a unique_ptr out

        std::size_t maxEventsPerRun_;
        std::uint64_t nextSeq_ = 0;
        std::int64_t now_ = 0;
    };

}
