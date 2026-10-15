#pragma once
namespace PowerRuntime {
void begin();
void activity();
void service(bool active);
bool serialWake(bool setupActive);
}
