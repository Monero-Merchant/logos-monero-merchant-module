#pragma once
#include "pos_service.h"
#include <memory>
struct LogosModules;
namespace merchant::pos
{
class Delivery
{
public:
    virtual ~Delivery() = default;
    virtual Json snapshot() = 0;
    virtual Json invitation() = 0;
    virtual Json authorize(const std::string& peer, bool approve) = 0;
    virtual void stop() = 0;
};
std::shared_ptr<Delivery> startDelivery(LogosModules& modules, const std::string& directory,
                                        Service::Backend backend);
} // namespace merchant::pos
