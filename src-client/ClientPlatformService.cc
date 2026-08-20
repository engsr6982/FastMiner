#include "ClientPlatformService.h"

#include "Global.h"
#include "command/FastMinerCommand.h"
#include "config/ClientConfigImpl.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/input/KeyRegistry.h"

namespace fm {
namespace client {

struct ClientPlatformService::Impl {
    ll::event::ListenerPtr mClientJoinLevelListener{nullptr};
    ll::event::ListenerPtr mKeyInputListener{nullptr};
    bool                   mKeyActivated{false};
};

ClientPlatformService::ClientPlatformService() : impl(std::make_unique<Impl>()) {}

ClientPlatformService::~ClientPlatformService() = default;

bool ClientPlatformService::init() {
    impl->mClientJoinLevelListener =
        ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientJoinLevelEvent>(
            [](ll::event::ClientJoinLevelEvent&) {
                FM_TRACE("Client joined level, registering commands");
                FastMinerCommand::setup();
            }
        );

    auto& key_registry = ll::input::KeyRegistry::getInstance();
    key_registry.getOrCreateKey("FastMiner Toggle", {ClientConfigImpl::model.bindKey});

    impl->mKeyInputListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::KeyInputEvent>(
        [this](ll::event::KeyInputEvent& event) {
            if (event.keyCode() == ClientConfigImpl::model.bindKey) {
                impl->mKeyActivated = event.isDown();
            }
        }
    );

    return true;
}

bool ClientPlatformService::destroy() {
    ll::event::EventBus::getInstance().removeListener(impl->mClientJoinLevelListener);
    ll::event::EventBus::getInstance().removeListener(impl->mKeyInputListener);
    return true;
}

bool ClientPlatformService::isKeyActivated() const { return impl->mKeyActivated; }


} // namespace client
} // namespace fm