// 声明 OTA 下载期间复用的显示静默和任务看门狗作用域守卫。
#pragma once

class OtaDisplayQuietGuard {
public:
    OtaDisplayQuietGuard();
    ~OtaDisplayQuietGuard();

    OtaDisplayQuietGuard(const OtaDisplayQuietGuard &) = delete;
    OtaDisplayQuietGuard &operator=(const OtaDisplayQuietGuard &) = delete;
};

class OtaTaskWatchdogGuard {
public:
    OtaTaskWatchdogGuard();
    ~OtaTaskWatchdogGuard();

    OtaTaskWatchdogGuard(const OtaTaskWatchdogGuard &) = delete;
    OtaTaskWatchdogGuard &operator=(const OtaTaskWatchdogGuard &) = delete;

    void reset();

private:
    bool active_ = false;
    bool added_ = false;
};
