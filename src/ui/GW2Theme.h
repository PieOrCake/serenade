#pragma once

void BuildGW2Theme();
void PushGW2Theme();
void PopGW2Theme();

struct ThemeGuard {
    ThemeGuard()  { PushGW2Theme(); }
    ~ThemeGuard() { PopGW2Theme(); }
};
