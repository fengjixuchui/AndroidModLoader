#ifndef _LOGGER_H
#define _LOGGER_H

#include <stdio.h>

enum eLogPrio
{
    LogP_Unk = 0,
    LogP_Default,
    LogP_Verbose,
    LogP_Debug,
    LogP_Info,
    LogP_Warn,
    LogP_Error,
    LogP_Fatal,
    LogP_Silent,
};

class Logger
{
public:
    Logger();
    void SetTag(const char* szTag);
    void Print(eLogPrio prio, const char* szMessage, ...);
    void PrintV(eLogPrio prio, const char* szMessage, va_list args);
    void PrintTag(eLogPrio prio, const char* szTag, const char* szMessage, ...);
    void PrintTagV(eLogPrio prio, const char* szTag, const char* szMessage, va_list args);
    void Info(const char* szMessage, ...);
    void InfoV(const char* szMessage, va_list args);
    void Error(const char* szMessage, ...);
    void ErrorV(const char* szMessage, va_list args);
    static Logger* GetLogger();
private:
    const char* m_szTag;
};
extern Logger* logger;

#endif // _LOGGER_H
