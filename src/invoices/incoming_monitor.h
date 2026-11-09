#pragma once

#include <functional>
#include <memory>
#include <string>

class IncomingMonitor
{
public:
    using Completion = std::function<void(std::string)>;
    using Read = std::function<void(const std::string&, Completion)>;
    using Notify = std::function<void(const std::string&)>;
    virtual ~IncomingMonitor() = default;
    virtual std::string snapshot() const = 0;
    virtual void refresh() = 0;
    virtual void stop() = 0;
};

std::shared_ptr<IncomingMonitor> makeIncomingMonitor(IncomingMonitor::Read read,
                                                     IncomingMonitor::Notify notify);
