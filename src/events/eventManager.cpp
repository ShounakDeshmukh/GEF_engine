#include "engine/events/eventManager.hpp"

namespace engine::events {

    eventManager::eventManager(std::size_t maxEventsPerRun): maxEventsPerRun_(maxEventsPerRun) {}

    bool eventManager::runsLater(const queuedEvent& a, const queuedEvent& b)
    {
        if(a.event->tick != b.event->tick) {return a.event->tick > b.event->tick;}
        if(a.event->priority != b.event->priority) {return a.event->priority > b.event->priority;}
        return a.seq > b.seq;
    }

    void eventManager::push(eventPtr event, std::type_index type)
    {
        queue_.push_back(queuedEvent{std::move(event), type, nextSeq_++});
        std::push_heap(queue_.begin(), queue_.end(), runsLater);
    }

    void eventManager::run(Scene& scene, std::int64_t tick)
    {
        now_ = tick;
        std::size_t dispatched = 0;
        while(!queue_.empty() && queue_.front().event->tick <= tick)
        {
            if(dispatched++ == maxEventsPerRun_)
            {
                // leftovers keep their tick, so they run first next time, in order
                log::warn("eventManager: {} events in one run; deferring the rest (next: {})",
                          maxEventsPerRun_, queue_.front().type.name());
                break;
            }

            std::pop_heap(queue_.begin(), queue_.end(), runsLater);
            queuedEvent next = std::move(queue_.back());
            queue_.pop_back();

            auto it = handlers_.find(next.type);
            if(it != handlers_.end())
            {
                it->second(*next.event, scene);
            }
            else if(warnedUnhandled_.insert(next.type).second)
            {
                log::error("eventManager: no handler for {}; further ones are dropped silently",
                           next.type.name());
            }
        }
    }

    void eventManager::receive(std::deque<networking::receivedMessage> messages, std::int64_t tick)
    {
        for(auto& message : messages)
        {
            auto it = decoders_.find(message.type);
            if(it == decoders_.end())
            {
                log::error("eventManager: network type {} is not accepted (unknown, or subscribed without fromNetwork); dropped", message.type);
                continue;
            }

            eventPtr event = it->second.decode(message.payload);
            if(!event)
            {
                log::error("eventManager: malformed payload for network type {}; dropped", message.type);
                continue;
            }
            event->tick = tick;
            event->remote = true;
            event->from = message.from;
            push(std::move(event), it->second.type);
        }
    }

}
