#pragma once
#include <concepts>

namespace fm {

/**
 * @brief 平台服务抽象接口。
 *
 * 仅管理各平台非全局专有的资源，如事件、外部依赖等。
 * @note 不含全局资源：Config、MinerLauncher 由独立单例/静态宿主管理。
 */
class IPlatformService {
public:
    virtual ~IPlatformService() = default;

    virtual bool init() = 0;

    virtual bool destroy() = 0;

    template <std::derived_from<IPlatformService> T>
    T& as() {
        return static_cast<T&>(*this);
    }
};

} // namespace fm