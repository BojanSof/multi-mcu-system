#ifndef SIGNALS_TEST_LOG_H
#define SIGNALS_TEST_LOG_H
#define LOG_MODULE_REGISTER(name, level) typedef int Test_LogRegistration
void Test_Log(const char *format, ...);
#define LOG_WRN(...) Test_Log(__VA_ARGS__)
#endif
